#pragma once
#include "SineModel.h"
#include <cmath>
#include <vector>

// Plays a SineModel: one sine oscillator per track, with amplitude and frequency following the analysis.
// The position in the sound moves by `advance` source samples per output sample (stretch, freeze), the pitch is
// scaled by `ratio` independently of that. Phases are the voice's own (random at the start), so it is not
// the original waveform, but the same partials. No allocation while playing: call prepare() first.
class AdditiveVoice
{
public:
    void prepare()
    {
        for (auto& p : phase)
            p.assign ((size_t) SineModel::kMaxTracks, 0.0);
    }

    void start (juce::uint32 seed)
    {
        pos = (double) SineModel::kFirstCentre;
        juce::Random rng ((juce::int64) seed);
        for (auto& p : phase)
            for (auto& v : p) v = (double) rng.nextFloat();
    }

    // Adds to outL / outR (n samples). Returns nothing; silent if the model has no tracks.
    void process (const SineModel& m, double advance, double ratio, float* outL, float* outR, int n)
    {
        if (m.ch.empty() || m.ch[0].numFrames < 2)
            return;
        constexpr int H = SineModel::kHop;
        const int frames = m.ch[0].numFrames;
        const double invSr = 1.0 / m.rate;
        const double fMax = 0.46 * m.rate;
        int done = 0;
        while (done < n)
        {
            double rel = (pos - (double) SineModel::kFirstCentre) / (double) H;
            rel = juce::jlimit (0.0, (double) (frames - 1) - 1.0e-6, rel); // at the end the last frame is held
            const int seg = (int) rel;
            const double u0 = rel - (double) seg;
            int nb = juce::jmin (kBlock, n - done);
            if (advance > 1.0e-9)
            {
                const double left = (1.0 - u0) * (double) H / advance;
                nb = juce::jmin (nb, juce::jmax (1, (int) std::ceil (left)));
            }
            const double u1 = juce::jmin (1.0, u0 + (double) nb * advance / (double) H);
            const double um = 0.5 * (u0 + u1);
            const float inv = 1.0f / (float) nb;

            for (int c = 0; c < 2; ++c)
            {
                const auto& chn = m.ch[(size_t) juce::jmin (c, (int) m.ch.size() - 1)];
                float* out = c == 0 ? outL : outR;
                if (out == nullptr || seg >= chn.numFrames)
                    continue;
                auto& ph = phase[(size_t) c];
                for (int s = chn.segStart[(size_t) seg]; s < chn.segStart[(size_t) seg + 1]; ++s)
                {
                    const auto& q = chn.segs[(size_t) s];
                    const double f = ((double) q.fa + ((double) q.fb - (double) q.fa) * um) * ratio;
                    if (f >= fMax)
                        continue;
                    const float a0 = q.aa + (q.ab - q.aa) * (float) u0;
                    const float a1 = q.aa + (q.ab - q.aa) * (float) u1;
                    double p = ph[(size_t) q.track];
                    const double w = f * invSr;
                    const float twoPi = 6.2831853f;
                    float zr = std::cos (twoPi * (float) p), zi = std::sin (twoPi * (float) p);
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
                    p += w * (double) nb;
                    ph[(size_t) q.track] = p - std::floor (p);
                }
            }
            pos += advance * (double) nb;
            done += nb;
        }
    }

private:
    static constexpr int kBlock = 64;
    std::vector<double> phase[2];
    double pos = (double) SineModel::kFirstCentre;
};
