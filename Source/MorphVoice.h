#pragma once
#include "AdditiveVoice.h"
#include <algorithm>

// Morphs between two sounds described as SineModels (the sound A of the slot and a partner B), at the pitch of A.
// Method (after the description of Caetano 2021, simplified, and with own choices where that is silent):
//  - the partials of each sound are numbered by harmonic (SineModel::harm, only for nearly harmonic sounds);
//  - partials with the same number are paired: frequency is interpolated in the log domain (equal steps in cents),
//    amplitude linearly; a partial that only one sound has fades with weight (1 - m) or m;
//  - sound B is first moved to the pitch of A (f0 ratio), its own inharmonicity is kept;
//  - tracks without a number (noise-like or chord notes) are not paired but cross-faded;
//  - when the sounds cannot be paired by harmonic number, every partial glides instead: each partial of A moves towards the
//    partial of B that a one-dimensional optimal transport of the two spectra assigns to it (and each partial of B comes from
//    its partial of A); the idea of gliding pitches by 1-D optimal transport is from Henderson & Solomon, "Audio Transport",
//    DAFx 2019 - here it is applied to the partial tracks of the sine models, not to STFT bins as in the paper;
//  - the residual noises of both sounds are cross-faded.
// Both sounds are read at the same time position (no time alignment). m = 0 is sound A alone, m = 1 is B at A's pitch.
class MorphVoice
{
public:
    void prepare()
    {
        for (int c = 0; c < 2; ++c)
        {
            phaseA[c].assign ((size_t) SineModel::kMaxTracks, 0.0);
            phaseB[c].assign ((size_t) SineModel::kMaxTracks, 0.0);
            phaseH[c].assign ((size_t) SineModel::kMaxHarm + 1, 0.0);
        }
        noiseA.prepare (false);
        noiseB.prepare (false);
        la.reserve ((size_t) SineModel::kMaxTracks);
        lb.reserve ((size_t) SineModel::kMaxTracks);
    }

    void start (juce::uint32 seed)
    {
        pos = (double) SineModel::kFirstCentre;
        juce::Random rng ((juce::int64) seed);
        for (int c = 0; c < 2; ++c)
        {
            for (auto& v : phaseA[c]) v = (double) rng.nextFloat();
            for (auto& v : phaseB[c]) v = (double) rng.nextFloat();
            for (auto& v : phaseH[c]) v = (double) rng.nextFloat();
        }
        noiseA.start (seed ^ 0x9e3779b9u);
        noiseB.start (seed ^ 0x85ebca6bu);
    }

    // True if the two sounds can be paired by harmonic number (both have a fundamental and are mostly harmonic).
    static bool pairable (const SineModel& a, const SineModel& b)
    {
        if (a.ch.empty() || b.ch.empty()) return false;
        for (const auto* m : { &a, &b })
            for (const auto& c : m->ch)
                if (c.f0 <= 0.0f || c.harmonicity < 0.5f) return false;
        return true;
    }

    void process (const SineModel& A, const SineModel& B, double advance, double ratio, float morph, float noiseAmount,
                  float* outL, float* outR, int n)
    {
        if (A.ch.empty() || B.ch.empty() || A.ch[0].numFrames < 2 || B.ch[0].numFrames < 2)
            return;
        const bool pair = pairable (A, B);
        noiseA.process (A, advance, ratio, outL, outR, n, noiseAmount * (1.0f - morph));
        noiseB.process (B, advance, ratio, outL, outR, n, noiseAmount * morph);

        constexpr int H = SineModel::kHop;
        const int framesA = A.ch[0].numFrames, framesB = B.ch[0].numFrames;
        const double invSr = 1.0 / A.rate, fMax = 0.46 * A.rate;
        const double m = morph;
        int done = 0;
        while (done < n)
        {
            double rel = (pos - (double) SineModel::kFirstCentre) / (double) H;
            rel = juce::jlimit (0.0, (double) (framesA - 1) - 1.0e-6, rel);
            const int seg = (int) rel;
            const double u0 = rel - (double) seg;
            int nb = juce::jmin (kBlock, n - done);
            if (advance > 1.0e-9)
                nb = juce::jmin (nb, juce::jmax (1, (int) std::ceil ((1.0 - u0) * (double) H / advance)));
            const double u1 = juce::jmin (1.0, u0 + (double) nb * advance / (double) H);
            const double um = 0.5 * (u0 + u1);
            const double limB = (double) (framesB - 1) - 1.0e-6;
            const double rb0 = juce::jmin (rel, limB), rb1 = juce::jmin (rel + (u1 - u0), limB);
            const int segB = (int) rb0;
            const double v0 = rb0 - (double) segB, v1 = juce::jmin (1.0, rb1 - (double) segB), vm = 0.5 * (v0 + v1);
            const float inv = 1.0f / (float) nb;

            for (int c = 0; c < 2; ++c)
            {
                float* out = c == 0 ? outL : outR;
                if (out == nullptr) continue;
                const auto& ca = A.ch[(size_t) juce::jmin (c, (int) A.ch.size() - 1)];
                const auto& cb = B.ch[(size_t) juce::jmin (c, (int) B.ch.size() - 1)];
                if (! pair && glide)
                {
                    transportBlock (out, c, ca, cb, seg, segB, u0, u1, um, v0, v1, vm, ratio, m, nb, done, invSr, fMax, inv);
                    continue;
                }
                const double align = pair ? (double) ca.f0 / (double) cb.f0 : 1.0; // moves B to the pitch of A
                std::fill (hasA, hasA + kH, 0);
                std::fill (hasB, hasB + kH, 0);

                // sound A
                if (seg < ca.numFrames)
                    for (int s = ca.segStart[(size_t) seg]; s < ca.segStart[(size_t) seg + 1]; ++s)
                    {
                        const auto& q = ca.segs[(size_t) s];
                        const int h = pair ? ca.harm[(size_t) q.track] : 0;
                        const double f = ((double) q.fa + ((double) q.fb - (double) q.fa) * um) * ratio;
                        const float a0 = q.aa + (q.ab - q.aa) * (float) u0, a1 = q.aa + (q.ab - q.aa) * (float) u1;
                        if (h > 0 && ! hasA[h]) { hasA[h] = 1; fA[h] = f; aA0[h] = a0; aA1[h] = a1; }
                        else osc (out, phaseA[c][(size_t) q.track], f, a0 * (1.0f - (float) m), a1 * (1.0f - (float) m), nb, done, invSr, fMax, inv);
                    }
                // sound B
                if (segB < cb.numFrames)
                    for (int s = cb.segStart[(size_t) segB]; s < cb.segStart[(size_t) segB + 1]; ++s)
                    {
                        const auto& q = cb.segs[(size_t) s];
                        const int h = pair ? cb.harm[(size_t) q.track] : 0;
                        const double f = ((double) q.fa + ((double) q.fb - (double) q.fa) * vm) * ratio * align;
                        const float a0 = q.aa + (q.ab - q.aa) * (float) v0, a1 = q.aa + (q.ab - q.aa) * (float) v1;
                        if (h > 0 && ! hasB[h]) { hasB[h] = 1; fB[h] = f; aB0[h] = a0; aB1[h] = a1; }
                        else osc (out, phaseB[c][(size_t) q.track], f, a0 * (float) m, a1 * (float) m, nb, done, invSr, fMax, inv);
                    }
                // paired harmonics
                for (int h = 1; h < kH; ++h)
                {
                    if (! hasA[h] && ! hasB[h]) continue;
                    double f;
                    float a0, a1;
                    if (hasA[h] && hasB[h])
                    {
                        f = std::exp ((1.0 - m) * std::log (fA[h]) + m * std::log (fB[h])); // equal steps in cents
                        a0 = (float) ((1.0 - m) * aA0[h] + m * aB0[h]);
                        a1 = (float) ((1.0 - m) * aA1[h] + m * aB1[h]);
                    }
                    else if (hasA[h]) { f = fA[h]; a0 = aA0[h] * (1.0f - (float) m); a1 = aA1[h] * (1.0f - (float) m); }
                    else { f = fB[h]; a0 = aB0[h] * (float) m; a1 = aB1[h] * (float) m; }
                    osc (out, phaseH[c][(size_t) h], f, a0, a1, nb, done, invSr, fMax, inv);
                }
            }
            pos += advance * (double) nb;
            done += nb;
        }
    }

    bool glide = true; // partials glide by optimal transport when the sounds cannot be paired by harmonic number
    double position() const noexcept { return pos; }

private:
    struct P { double f; float a0, a1; int track; double c0, c1; };
    std::vector<P> la, lb;

    static void cdf (std::vector<P>& v)
    {
        std::sort (v.begin(), v.end(), [] (const P& x, const P& y) { return x.f < y.f; });
        double sum = 0.0;
        for (auto& p : v) sum += 0.5 * (double) (p.a0 + p.a1);
        double acc = 0.0;
        for (auto& p : v) { p.c0 = acc; acc += sum > 0.0 ? 0.5 * (double) (p.a0 + p.a1) / sum : 0.0; p.c1 = acc; }
    }
    // the entry of v whose share of the total level contains the point c (0..1)
    static const P& at (const std::vector<P>& v, double c)
    {
        size_t lo = 0, hi = v.size() - 1;
        while (lo < hi) { const size_t mid = (lo + hi) / 2; if (v[mid].c1 < c) lo = mid + 1; else hi = mid; }
        return v[lo];
    }

    template <typename Ch>
    void transportBlock (float* out, int c, const Ch& ca, const Ch& cb, int seg, int segB, double u0, double u1, double um,
                         double v0, double v1, double vm, double ratio, double m, int nb, int done, double invSr, double fMax, float inv)
    {
        la.clear(); lb.clear();
        if (seg < ca.numFrames)
            for (int s = ca.segStart[(size_t) seg]; s < ca.segStart[(size_t) seg + 1]; ++s)
            {
                const auto& q = ca.segs[(size_t) s];
                la.push_back ({ ((double) q.fa + ((double) q.fb - (double) q.fa) * um) * ratio, q.aa + (q.ab - q.aa) * (float) u0, q.aa + (q.ab - q.aa) * (float) u1, q.track, 0.0, 0.0 });
            }
        if (segB < cb.numFrames)
            for (int s = cb.segStart[(size_t) segB]; s < cb.segStart[(size_t) segB + 1]; ++s)
            {
                const auto& q = cb.segs[(size_t) s];
                lb.push_back ({ ((double) q.fa + ((double) q.fb - (double) q.fa) * vm) * ratio, q.aa + (q.ab - q.aa) * (float) v0, q.aa + (q.ab - q.aa) * (float) v1, q.track, 0.0, 0.0 });
            }
        cdf (la); cdf (lb);
        const float wa = 1.0f - (float) m, wb = (float) m;
        for (const auto& p : la)
        {
            double f = p.f;
            if (! lb.empty() && p.f > 1.0) { const auto& t = at (lb, 0.5 * (p.c0 + p.c1)); if (t.f > 1.0) f = std::exp ((1.0 - m) * std::log (p.f) + m * std::log (t.f)); }
            osc (out, phaseA[c][(size_t) p.track], f, p.a0 * wa, p.a1 * wa, nb, done, invSr, fMax, inv);
        }
        for (const auto& p : lb)
        {
            double f = p.f;
            if (! la.empty() && p.f > 1.0) { const auto& t = at (la, 0.5 * (p.c0 + p.c1)); if (t.f > 1.0) f = std::exp ((1.0 - m) * std::log (t.f) + m * std::log (p.f)); }
            osc (out, phaseB[c][(size_t) p.track], f, p.a0 * wb, p.a1 * wb, nb, done, invSr, fMax, inv);
        }
    }

    static constexpr int kBlock = 64;
    static constexpr int kH = SineModel::kMaxHarm + 1;

    static void osc (float* out, double& ph, double f, float a0, float a1, int nb, int done, double invSr, double fMax, float inv)
    {
        if (f >= fMax || f <= 0.0 || (a0 <= 0.0f && a1 <= 0.0f)) return;
        const double w = f * invSr;
        const float twoPi = 6.2831853f;
        float zr = std::cos (twoPi * (float) ph), zi = std::sin (twoPi * (float) ph);
        const float rr = std::cos (twoPi * (float) w), ri = std::sin (twoPi * (float) w);
        float a = a0;
        const float da = (a1 - a0) * inv;
        for (int i = 0; i < nb; ++i)
        {
            out[done + i] += a * zr;
            const float nr = zr * rr - zi * ri;
            zi = zr * ri + zi * rr;
            zr = nr;
            a += da;
        }
        ph += w * (double) nb;
        ph -= std::floor (ph);
    }

    std::vector<double> phaseA[2], phaseB[2], phaseH[2];
    char hasA[kH] {}, hasB[kH] {};
    double fA[kH] {}, fB[kH] {};
    float aA0[kH] {}, aA1[kH] {}, aB0[kH] {}, aB1[kH] {};
    AdditiveVoice noiseA, noiseB;
    double pos = (double) SineModel::kFirstCentre;
};
