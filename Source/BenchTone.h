#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// The bench's Timbre stage, live (no baking): Light = a tilt of the whole spectrum around 1 kHz, Body = a shelf under 220 Hz,
// Air = a shelf above 8 kHz. Each knob is 0..1 with 0.5 = untouched; the filters follow the knob in a few milliseconds,
// so turning it is heard at once. Shelves: RBJ cookbook, slope 1.
class BenchTone
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& c : st) for (auto& s : c) for (auto& z : s) z = {};
        for (int i = 0; i < 4; ++i) { now[i] = 0.0f; set[i] = 1.0e9f; }
    }

    // light, body, air: 0..1 (0.5 = untouched)
    void process (juce::AudioBuffer<float>& b, int n, float light, float body, float air)
    {
        const float target[4] = { -(light - 0.5f) * 2.0f * 9.0f, (light - 0.5f) * 2.0f * 9.0f, (body - 0.5f) * 2.0f * 11.0f, (air - 0.5f) * 2.0f * 11.0f };
        // (0: low half of the tilt, 1: high half of the tilt, 2: body, 3: air)
        bool idle = true;
        for (int i = 0; i < 4; ++i) if (std::abs (target[i]) > 0.02f || std::abs (now[i]) > 0.02f) idle = false;
        if (idle) { for (int i = 0; i < 4; ++i) now[i] = 0.0f; return; }
        const int chans = juce::jmin (2, b.getNumChannels());
        for (int s0 = 0; s0 < n; s0 += kSub)
        {
            const int len = juce::jmin (kSub, n - s0);
            for (int i = 0; i < 4; ++i)
            {
                now[i] += (target[i] - now[i]) * 0.2f;
                if (std::abs (now[i] - set[i]) > 0.01f)
                {
                    set[i] = now[i];
                    switch (i)
                    {
                        case 0: shelf (k[0], 1000.0, now[0], false); break;
                        case 1: shelf (k[1], 1000.0, now[1], true); break;
                        case 2: shelf (k[2], 220.0, now[2], false); break;
                        default: shelf (k[3], 8000.0, now[3], true); break;
                    }
                }
            }
            for (int c = 0; c < chans; ++c)
            {
                float* d = b.getWritePointer (c) + s0;
                for (int j = 0; j < len; ++j)
                {
                    double x = d[j];
                    for (int i = 0; i < 4; ++i) x = run (k[i], st[c][i][0], x);
                    d[j] = (float) x;
                }
            }
        }
    }

private:
    static constexpr int kSub = 32;
    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { double z1 = 0, z2 = 0; };

    static double run (const Coef& c, State& s, double x)
    {
        const double y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void shelf (Coef& c, double f0, double gainDb, bool high) const
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979323846 * juce::jmin (f0, 0.45 * sr) / sr;
        const double cs = std::cos (w), sn = std::sin (w), al = sn / 2.0 * std::sqrt (2.0);
        const double tsa = 2.0 * std::sqrt (A) * al;
        double b0, b1, b2, a0, a1, a2;
        if (! high)
        {
            b0 = A * ((A + 1) - (A - 1) * cs + tsa); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - tsa);
            a0 = (A + 1) + (A - 1) * cs + tsa;       a1 = -2 * ((A - 1) + (A + 1) * cs);    a2 = (A + 1) + (A - 1) * cs - tsa;
        }
        else
        {
            b0 = A * ((A + 1) + (A - 1) * cs + tsa);  b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - tsa);
            a0 = (A + 1) - (A - 1) * cs + tsa;        a1 = 2 * ((A - 1) - (A + 1) * cs);      a2 = (A + 1) - (A - 1) * cs - tsa;
        }
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
    }

    double sr = 44100.0;
    float now[4] {}, set[4] { 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f };
    Coef k[4];
    State st[2][4][1];
};
