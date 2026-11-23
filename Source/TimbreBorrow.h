#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Conversation, vertical: one sound takes on the colour of the other (its timbre, not its notes or its rhythm).
// Ten bands, 120 Hz .. 10 kHz: the average level of each band is measured in both sounds (about 120 ms), and the sound that
// receives is lifted or lowered band by band towards the other's balance (never more than 9 dB, and the whole level is kept:
// only the balance moves). `amount` 0..1 says how far. Our own design; the numbers were chosen by ear.
class TimbreBorrow
{
public:
    static constexpr int kBands = 10;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static const double fc[kBands] = { 120, 200, 330, 550, 900, 1500, 2500, 4000, 6500, 10000 };
        for (int b = 0; b < kBands; ++b)
        {
            centre[b] = juce::jmin (fc[b], 0.45 * sr);
            bandpass (bpC[b], centre[b], 1.2);
            peak (pk[b], centre[b], 0.0);
        }
        reset();
    }

    void reset()
    {
        for (auto& x : bpS) for (auto& s : x) s = {};
        for (auto& c : pkS) for (auto& bnd : c) for (auto& s : bnd) s = {};
        for (int b = 0; b < kBands; ++b) { es[b] = et[b] = 1.0e-9; gDb[b] = 0.0f; gSet[b] = 0.0f; }
    }

    bool isIdle() const noexcept
    {
        for (int b = 0; b < kBands; ++b) if (std::abs (gDb[b]) > 0.05f) return false;
        return true;
    }

    void process (const juce::AudioBuffer<float>& src, juce::AudioBuffer<float>& tgt, int n, float amount)
    {
        if (amount <= 0.0005f && isIdle()) return;
        const int chT = juce::jmin (2, tgt.getNumChannels()), chS = juce::jmin (2, src.getNumChannels());
        const double avg = 1.0 - std::exp (-1.0 / (0.12 * sr));
        for (int s0 = 0; s0 < n; s0 += kSub)
        {
            const int len = juce::jmin (kSub, n - s0);
            for (int j = 0; j < len; ++j)
            {
                double xs = 0.0, xt = 0.0;
                for (int c = 0; c < chS; ++c) xs += src.getSample (c, s0 + j);
                for (int c = 0; c < chT; ++c) xt += tgt.getSample (c, s0 + j);
                xs /= juce::jmax (1, chS); xt /= juce::jmax (1, chT);
                for (int b = 0; b < kBands; ++b)
                {
                    const double ys = run (bpC[b], bpS[b][0], xs), yt = run (bpC[b], bpS[b][1], xt);
                    es[b] += (ys * ys - es[b]) * avg;
                    et[b] += (yt * yt - et[b]) * avg;
                }
            }
            // where the balance of the other sound lies against this one's (in dB), without moving the whole level
            double want[kBands], mean = 0.0;
            double total = 0.0; for (int b = 0; b < kBands; ++b) total += es[b];
            const bool live = total > 1.0e-9;
            for (int b = 0; b < kBands; ++b)
            {
                want[b] = live ? juce::jlimit (-20.0, 20.0, 10.0 * std::log10 ((es[b] + 1.0e-10) / (et[b] + 1.0e-10))) : 0.0;
                mean += want[b] / kBands;
            }
            double goalDb[kBands], pBefore = 1.0e-12, pAfter = 1.0e-12;
            for (int b = 0; b < kBands; ++b)
            {
                goalDb[b] = juce::jlimit (-9.0, 9.0, (double) amount * (want[b] - mean));
                pBefore += et[b]; pAfter += et[b] * std::pow (10.0, goalDb[b] / 10.0);
            }
            const double keep = 10.0 * std::log10 (pAfter / pBefore); // the whole level stays: only the balance moves
            for (int b = 0; b < kBands; ++b)
            {
                const float goal = (float) (goalDb[b] - keep);
                gDb[b] += (goal - gDb[b]) * 0.08f;
                if (std::abs (gDb[b] - gSet[b]) > 0.05f) { gSet[b] = gDb[b]; peak (pk[b], centre[b], gDb[b]); }
            }
            for (int c = 0; c < chT; ++c)
            {
                float* d = tgt.getWritePointer (c) + s0;
                for (int j = 0; j < len; ++j)
                {
                    double x = d[j];
                    for (int b = 0; b < kBands; ++b) x = run (pk[b], pkS[c][b][0], x);
                    d[j] = (float) x;
                }
            }
        }
    }

private:
    static constexpr int kSub = 64;
    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { double z1 = 0, z2 = 0; };
    static double run (const Coef& c, State& s, double x)
    {
        const double y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void bandpass (Coef& k, double f0, double q) const
    {
        const double w = 2.0 * 3.14159265358979323846 * f0 / sr, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        k.b0 = al / a0; k.b1 = 0.0; k.b2 = -al / a0; k.a1 = -2.0 * std::cos (w) / a0; k.a2 = (1.0 - al) / a0;
    }
    void peak (Coef& k, double f0, double gainDb) const
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979323846 * f0 / sr, al = std::sin (w) / (2.0 * 1.2), a0 = 1.0 + al / A;
        k.b0 = (1.0 + al * A) / a0; k.b1 = -2.0 * std::cos (w) / a0; k.b2 = (1.0 - al * A) / a0; k.a1 = k.b1; k.a2 = (1.0 - al / A) / a0;
    }

    double sr = 44100.0, centre[kBands] {}, es[kBands] {}, et[kBands] {};
    float gDb[kBands] {}, gSet[kBands] {};
    Coef bpC[kBands], pk[kBands];
    State bpS[kBands][2], pkS[2][kBands][1];
};
