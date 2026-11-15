#include "SineModel.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace
{
struct Peak { float f, a; };
struct Entry { int track; float f, a; };

struct Active { int id; float f, a; };

// Analyses one channel; ids are local to the channel.
bool analyseChannel (const float* x, int n, double rate, SineModel::Channel& out, int& tracksOut, const std::atomic<bool>* cancel)
{
    constexpr int N = SineModel::kFft, H = SineModel::kHop;
    const int numFrames = n >= N ? (n - N) / H + 1 : 0;
    out.numFrames = numFrames;
    out.segs.clear();
    out.segStart.assign ((size_t) numFrames + 1, 0);
    tracksOut = 0;
    if (numFrames < 2)
        return true;

    // Blackman-Harris window (4 terms)
    std::vector<float> win ((size_t) N);
    double winSum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double t = 2.0 * juce::MathConstants<double>::pi * i / (N - 1);
        win[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t));
        winSum += win[(size_t) i];
    }
    const double ampScale = 2.0 / winSum; // peak magnitude -> sine amplitude

    juce::dsp::FFT fft (12);
    std::vector<float> buf ((size_t) N * 2);
    std::vector<float> mag ((size_t) N / 2 + 1);

    // 1. peaks of every frame
    std::vector<std::vector<Peak>> peaks ((size_t) numFrames);
    float globalMax = 0.0f;
    for (int m = 0; m < numFrames; ++m)
    {
        if (cancel != nullptr && cancel->load())
            return false;
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[m * H + i] * win[(size_t) i];
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft.performRealOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= N / 2; ++k)
            mag[(size_t) k] = std::hypot (buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)]);

        auto& pk = peaks[(size_t) m];
        for (int k = 2; k < N / 2 - 1; ++k)
        {
            if (! (mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1]))
                continue;
            const float a = std::log (mag[(size_t) k - 1] + 1.0e-12f), b = std::log (mag[(size_t) k] + 1.0e-12f), c = std::log (mag[(size_t) k + 1] + 1.0e-12f);
            const float den = a - 2.0f * b + c;
            const float p = den < -1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den) : 0.0f;
            const float amp = std::exp (b - 0.25f * (a - c) * p) * (float) ampScale;
            const float f = (float) (((double) k + (double) p) * rate / N);
            if (f < 20.0f || f > 0.46f * (float) rate)
                continue;
            pk.push_back ({ f, amp });
            globalMax = std::max (globalMax, amp);
        }
    }
    if (globalMax <= 0.0f)
        return true;
    const float floorAmp = std::max (2.0e-4f, globalMax * 0.00032f); // about -70 dB under the loudest partial
    for (auto& pk : peaks)
    {
        pk.erase (std::remove_if (pk.begin(), pk.end(), [&] (const Peak& p) { return p.a < floorAmp; }), pk.end());
        if ((int) pk.size() > SineModel::kMaxPerFrame)
        {
            std::nth_element (pk.begin(), pk.begin() + SineModel::kMaxPerFrame, pk.end(), [] (const Peak& a, const Peak& b) { return a.a > b.a; });
            pk.resize ((size_t) SineModel::kMaxPerFrame);
            std::sort (pk.begin(), pk.end(), [] (const Peak& a, const Peak& b) { return a.f < b.f; });
        }
    }

    // 2. peak continuation: each track takes the nearest free peak within 30 Hz, strongest tracks first
    std::vector<std::vector<Entry>> frames ((size_t) numFrames);
    std::vector<Active> active, next;
    std::vector<int> trackLen;
    std::vector<char> used;
    for (int m = 0; m < numFrames; ++m)
    {
        if (cancel != nullptr && cancel->load())
            return false;
        const auto& pk = peaks[(size_t) m];
        used.assign (pk.size(), 0);
        next.clear();
        std::sort (active.begin(), active.end(), [] (const Active& a, const Active& b) { return a.a > b.a; });
        for (auto& t : active)
        {
            int j = (int) (std::lower_bound (pk.begin(), pk.end(), t.f, [] (const Peak& p, float v) { return p.f < v; }) - pk.begin());
            int best = -1;
            float bd = 30.0f;
            for (int s = j - 1; s >= 0; --s)
            {
                const float d = t.f - pk[(size_t) s].f;
                if (d > bd) break;
                if (! used[(size_t) s] && d < bd) { bd = d; best = s; }
            }
            for (int s = j; s < (int) pk.size(); ++s)
            {
                const float d = pk[(size_t) s].f - t.f;
                if (d > bd) break;
                if (! used[(size_t) s] && d < bd) { bd = d; best = s; }
            }
            if (best >= 0)
            {
                used[(size_t) best] = 1;
                frames[(size_t) m].push_back ({ t.id, pk[(size_t) best].f, pk[(size_t) best].a });
                ++trackLen[(size_t) t.id];
                next.push_back ({ t.id, pk[(size_t) best].f, pk[(size_t) best].a });
            }
        }
        for (size_t j = 0; j < pk.size(); ++j)
            if (! used[j])
            {
                const int id = (int) trackLen.size();
                trackLen.push_back (1);
                frames[(size_t) m].push_back ({ id, pk[j].f, pk[j].a });
                next.push_back ({ id, pk[j].f, pk[j].a });
            }
        active.swap (next);
    }

    // 3. drop short tracks, keep the strongest kMaxTracks, renumber
    const int total = (int) trackLen.size();
    std::vector<double> energy ((size_t) total, 0.0);
    for (auto& fr : frames)
        for (auto& e : fr) energy[(size_t) e.track] += (double) e.a * e.a;
    std::vector<int> keep;
    for (int i = 0; i < total; ++i)
        if (trackLen[(size_t) i] >= SineModel::kMinTrackFrames) keep.push_back (i);
    if ((int) keep.size() > SineModel::kMaxTracks)
    {
        std::nth_element (keep.begin(), keep.begin() + SineModel::kMaxTracks, keep.end(),
                          [&] (int a, int b) { return energy[(size_t) a] > energy[(size_t) b]; });
        keep.resize ((size_t) SineModel::kMaxTracks);
    }
    std::vector<int> remap ((size_t) total, -1);
    for (size_t i = 0; i < keep.size(); ++i) remap[(size_t) keep[i]] = (int) i;
    tracksOut = (int) keep.size();
    for (auto& fr : frames)
    {
        std::vector<Entry> kept;
        for (auto& e : fr)
            if (remap[(size_t) e.track] >= 0) kept.push_back ({ remap[(size_t) e.track], e.f, e.a });
        fr.swap (kept);
    }

    // 4. segments between frame m and m + 1 (births ramp up from zero, deaths ramp down to zero)
    std::vector<int> stampNext ((size_t) tracksOut, -1), idxNext ((size_t) tracksOut, 0), stampCur ((size_t) tracksOut, -1);
    for (int m = 0; m < numFrames; ++m)
    {
        out.segStart[(size_t) m] = (int) out.segs.size();
        const auto& cur = frames[(size_t) m];
        if (m + 1 < numFrames)
        {
            const auto& nx = frames[(size_t) m + 1];
            for (size_t j = 0; j < nx.size(); ++j) { stampNext[(size_t) nx[j].track] = m + 1; idxNext[(size_t) nx[j].track] = (int) j; }
            for (auto& e : cur) stampCur[(size_t) e.track] = m;
            for (auto& e : cur)
            {
                if (stampNext[(size_t) e.track] == m + 1)
                {
                    const auto& q = nx[(size_t) idxNext[(size_t) e.track]];
                    out.segs.push_back ({ e.track, e.f, e.a, q.f, q.a });
                }
                else
                    out.segs.push_back ({ e.track, e.f, e.a, e.f, 0.0f });
            }
            for (auto& q : nx)
                if (stampCur[(size_t) q.track] != m)
                    out.segs.push_back ({ q.track, q.f, 0.0f, q.f, q.a });
        }
    }
    out.segStart[(size_t) numFrames] = (int) out.segs.size();

    // 5. residual: for each frame the mean power of the bins that are not under a partial peak, per band
    {
        constexpr int B = SineModel::kBands;
        const double fTop = 0.46 * rate;
        const double ratio = std::pow (fTop / SineModel::kBandLow, 1.0 / B);
        double winSq = 0.0;
        for (int i = 0; i < N; ++i) winSq += (double) win[(size_t) i] * win[(size_t) i];
        out.noise.assign ((size_t) numFrames * B, 0.0f);
        std::vector<char> masked ((size_t) N / 2 + 1);
        std::vector<double> sum (B), cnt (B);
        std::vector<float> pw ((size_t) N / 2 + 1);
        for (int m = 0; m < numFrames; ++m)
        {
            if (cancel != nullptr && cancel->load())
                return false;
            for (int i = 0; i < N; ++i) buf[(size_t) i] = x[m * H + i] * win[(size_t) i];
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k <= N / 2; ++k)
                pw[(size_t) k] = buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)];
            std::fill (masked.begin(), masked.end(), 0);
            for (auto& e : frames[(size_t) m])
            {
                const int kc = (int) std::lround ((double) e.f * N / rate);
                for (int k = std::max (0, kc - 5); k <= std::min (N / 2, kc + 5); ++k) masked[(size_t) k] = 1;
            }
            std::fill (sum.begin(), sum.end(), 0.0);
            std::fill (cnt.begin(), cnt.end(), 0.0);
            for (int k = 1; k <= N / 2; ++k)
            {
                if (masked[(size_t) k]) continue;
                const double f = (double) k * rate / N;
                if (f > fTop) break;
                const int b = f <= SineModel::kBandLow ? 0 : juce::jmin (B - 1, (int) (std::log (f / SineModel::kBandLow) / std::log (ratio)));
                sum[(size_t) b] += pw[(size_t) k];
                cnt[(size_t) b] += 1.0;
            }
            float* dst = out.noise.data() + (size_t) m * B;
            float last = 0.0f;
            bool haveLast = false;
            for (int b = 0; b < B; ++b)
            {
                if (cnt[(size_t) b] >= 2.0)
                {
                    last = (float) std::sqrt (sum[(size_t) b] / cnt[(size_t) b] / winSq);
                    haveLast = true;
                }
                dst[b] = haveLast ? last : 0.0f; // bands with too few free bins take the one below
            }
            // first bands that had no value take the first one that has
            int fb = 0;
            while (fb < B && cnt[(size_t) fb] < 2.0) ++fb;
            for (int b = 0; b < fb && fb < B; ++b) dst[b] = dst[fb];
        }
    }
    return true;
}

// Finds one fundamental for the whole sound and gives each track the number of the harmonic it sits on.
// Own design (harmonic-sum with a presence check, so that f0 / 2 does not win), not a published method.
void labelHarmonics (SineModel::Channel& ch, int tracks, double rate)
{
    ch.harm.assign ((size_t) tracks, 0);
    ch.f0 = 0.0f;
    ch.harmonicity = 0.0f;
    const int M = ch.numFrames;
    if (M < 2 || tracks < 1)
        return;
    struct Pk { float f, a; };
    struct Res { float f0, w; };
    std::vector<Res> res;
    double totalW = 0.0;
    std::vector<Pk> pk;
    const int step = std::max (1, M / 300);
    for (int m = 0; m < M - 1; m += step)
    {
        pk.clear();
        for (int s = ch.segStart[(size_t) m]; s < ch.segStart[(size_t) m + 1]; ++s)
            if (ch.segs[(size_t) s].aa > 0.0f) pk.push_back ({ ch.segs[(size_t) s].fa, ch.segs[(size_t) s].aa });
        if (pk.size() < 3)
            continue;
        std::sort (pk.begin(), pk.end(), [] (const Pk& a, const Pk& b) { return a.a > b.a; });
        if (pk.size() > 24) pk.resize (24);
        double tot = 0.0;
        for (auto& p : pk) tot += (double) p.a * p.a;
        totalW += tot;
        float bestScore = 0.0f, bestF0 = 0.0f;
        for (size_t i = 0; i < std::min<size_t> (4, pk.size()); ++i)
            for (int k = 1; k <= 4; ++k)
            {
                const double f0c = pk[i].f / k;
                if (f0c < 40.0 || f0c > 2000.0) continue;
                double matched = 0.0, wsum = 0.0, fsum = 0.0;
                for (auto& p : pk)
                {
                    const int n = (int) std::lround (p.f / f0c);
                    if (n >= 1 && std::abs (p.f / f0c - n) < std::min (0.025 * n, 0.3))
                    {
                        matched += (double) p.a * p.a;
                        wsum += (double) p.a * p.a;
                        fsum += (double) p.a * p.a * (p.f / n);
                    }
                }
                const int K = (int) std::min (8.0, std::floor (0.45 * rate / f0c));
                int present = 0;
                for (int n = 1; n <= K; ++n)
                    for (auto& p : pk)
                        if (std::abs (p.f / f0c - n) < std::min (0.025 * n, 0.3)) { ++present; break; }
                const float score = (float) ((matched / tot) * ((double) present / std::max (1, K)));
                if (score > bestScore && wsum > 0.0) { bestScore = score; bestF0 = (float) (fsum / wsum); }
            }
        if (bestScore > 0.4f)
            res.push_back ({ bestF0, (float) tot });
    }
    double qualW = 0.0;
    for (auto& r : res) qualW += r.w;
    if (res.empty() || qualW < 0.3 * totalW)
        return;
    std::sort (res.begin(), res.end(), [] (const Res& a, const Res& b) { return a.f0 < b.f0; });
    double acc = 0.0;
    float f0 = res.back().f0;
    for (auto& r : res) { acc += r.w; if (acc >= 0.5 * qualW) { f0 = r.f0; break; } }
    ch.f0 = f0;

    std::vector<double> sf ((size_t) tracks, 0.0), sw ((size_t) tracks, 0.0), se ((size_t) tracks, 0.0);
    for (auto& q : ch.segs)
    {
        sf[(size_t) q.track] += (double) q.fa * q.aa;
        sw[(size_t) q.track] += q.aa;
        se[(size_t) q.track] += (double) q.aa * q.aa;
    }
    double eAll = 0.0, eHarm = 0.0;
    for (int t = 0; t < tracks; ++t)
    {
        eAll += se[(size_t) t];
        if (sw[(size_t) t] <= 0.0) continue;
        const double mean = sf[(size_t) t] / sw[(size_t) t];
        const int n = (int) std::lround (mean / f0);
        if (n >= 1 && n <= SineModel::kMaxHarm && std::abs (mean / (double) f0 - n) < std::min (0.03 * n, 0.3)) // never wider than 0.3 of the harmonic spacing
        {
            ch.harm[(size_t) t] = (unsigned char) n;
            eHarm += se[(size_t) t];
        }
    }
    ch.harmonicity = eAll > 0.0 ? (float) (eHarm / eAll) : 0.0f;
}
} // namespace

std::shared_ptr<SineModel> SineModel::analyse (const juce::AudioBuffer<float>& x, double rate, const std::atomic<bool>* cancel)
{
    auto m = std::make_shared<SineModel>();
    m->rate = rate;
    const int n = juce::jmin (x.getNumSamples(), (int) (90.0 * rate)); // at most the first 90 s
    const int chans = juce::jmin (2, x.getNumChannels());
    if (chans < 1 || n < kFft * 2)
        return m; // too short: no tracks, the voice falls back to the normal engine
    m->ch.resize ((size_t) chans);
    m->bandRatio = std::pow (0.46 * rate / kBandLow, 1.0 / kBands);
    for (int c = 0; c < chans; ++c)
    {
        int tracks = 0;
        if (! analyseChannel (x.getReadPointer (c), n, rate, m->ch[(size_t) c], tracks, cancel))
            return nullptr;
        labelHarmonics (m->ch[(size_t) c], tracks, rate);
        m->numTracks = std::max (m->numTracks, tracks);
    }
    // the body: where the partials put their energy, averaged over the whole sound (used by Chromophonic)
    {
        std::vector<double> e ((size_t) kBodyBins, 0.0);
        for (const auto& chn : m->ch)
            for (const auto& q : chn.segs)
            {
                if (q.fa < (float) kBodyLow) continue;
                const int b = (int) std::floor (12.0 * std::log2 ((double) q.fa / kBodyLow));
                if (b >= 0 && b < kBodyBins) e[(size_t) b] += (double) q.aa * (double) q.aa;
            }
        m->body.assign ((size_t) kBodyBins, 0.0f);
        double mx = 0.0;
        std::vector<double> sm ((size_t) kBodyBins, 0.0);
        for (int i = 0; i < kBodyBins; ++i)
        {
            double a = 0.0, w = 0.0;
            for (int j = -4; j <= 4; ++j)
            {
                const int k = i + j;
                if (k < 0 || k >= kBodyBins) continue;
                const double g = std::exp (-0.5 * (double) (j * j) / 4.0);
                a += g * e[(size_t) k];
                w += g;
            }
            sm[(size_t) i] = a / w;
            mx = std::max (mx, sm[(size_t) i]);
        }
        if (mx > 0.0)
            for (int i = 0; i < kBodyBins; ++i)
                m->body[(size_t) i] = (float) std::max (sm[(size_t) i], mx * 1.0e-6);
    }
    return m;
}
