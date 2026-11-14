#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// "Bright": a gentle tilt of the whole spectrum around 1 kHz (low part down, high part up, or the other way round).
// Two shelving filters (RBJ cookbook). Brightness of a sound goes together with the position of its spectral centre
// (the "spectral centroid"), and a tilt moves that centre.
class TiltFilter
{
public:
    void prepare (double sampleRate) { sr = sampleRate; setTilt (0.0f); reset(); }
    void reset() { for (auto& c : st) for (auto& s : c) s = {}; }

    // dB at each end of the tilt: positive = brighter. 0 = off.
    void setTilt (float db)
    {
        if (std::abs (db - tilt) < 0.001f && ready)
            return;
        tilt = db;
        ready = true;
        shelf (lo, 1000.0, -db, false);
        shelf (hi, 1000.0, db, true);
    }
    bool isOff() const noexcept { return std::abs (tilt) < 0.05f; }

    void process (juce::AudioBuffer<float>& b)
    {
        if (isOff())
            return;
        for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
        {
            float* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                double x = d[i];
                x = run (lo, st[c][0], x);
                x = run (hi, st[c][1], x);
                d[i] = (float) x;
            }
        }
    }

private:
    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { double z1 = 0, z2 = 0; };

    static double run (const Coef& k, State& s, double x)
    {
        const double y = k.b0 * x + s.z1;
        s.z1 = k.b1 * x - k.a1 * y + s.z2;
        s.z2 = k.b2 * x - k.a2 * y;
        return y;
    }

    void shelf (Coef& k, double f0, double gainDb, bool high) const
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979323846 * f0 / sr;
        const double cs = std::cos (w), sn = std::sin (w), al = sn / 2.0 * std::sqrt (2.0); // shelf slope 1
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
        k.b0 = b0 / a0; k.b1 = b1 / a0; k.b2 = b2 / a0; k.a1 = a1 / a0; k.a2 = a2 / a0;
    }

    double sr = 44100.0;
    float tilt = 0.0f;
    bool ready = false;
    Coef lo, hi;
    State st[2][2];
};
