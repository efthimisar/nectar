#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// The two tone knobs of the main screen, on the whole sound:
//   Weight (thin - heavy): low shelf at 160 Hz, -10 .. +10 dB
//   Light  (dull - bright): high shelf at 3.5 kHz, -10 .. +10 dB; on the dull half also a gentle low-pass (20 kHz down to 1.5 kHz)
// The loudness is held: the level after the knobs is matched to the level before them (power over about 0.4 s, at most
// +-12 dB), so the knobs change the colour and not simply the volume.
// Shelves after the Audio EQ Cookbook (R. Bristow-Johnson), slope S = 0.7. The frequencies and ranges are our own choices.
class MacroTone
{
public:
    void prepare (double sampleRate) { sr = sampleRate; for (auto& c : ch) c = Ch(); w = 0.5f; l = 0.5f; eIn = eOut = 0.0; gain = 1.0f; }

    void process (juce::AudioBuffer<float>& b, float weight, float light)
    {
        const int n = b.getNumSamples(), chs = juce::jmin (2, b.getNumChannels());
        const float w0 = w, l0 = l; // (80 ms glide: a quick flick from dull to bright still glides)
        const float k = 1.0f - std::exp (-(float) n / (float) sr / 0.08f);
        const float wE = w + (juce::jlimit (0.0f, 1.0f, weight) - w) * k, lE = l + (juce::jlimit (0.0f, 1.0f, light) - l) * k;
        const float dbW = (wE - 0.5f) * 20.0f, dbL = (lE - 0.5f) * 20.0f;
        // resting in the middle (and settled there): nothing to do. Never switched off while the filters still hold sound:
        // stopping them then is a step in the waveform
        if (std::abs (dbW) < 0.05f && std::abs (dbL) < 0.05f && std::abs (gain - 1.0f) < 1.0e-3f && settledBlocks > (int) (0.2 * sr / juce::jmax (1, n)))
        { for (auto& c : ch) c = Ch(); eIn = eOut = 0.0; gain = 1.0f; w = wE; l = lE; return; }
        settledBlocks = (std::abs (dbW) < 0.05f && std::abs (dbL) < 0.05f) ? settledBlocks + 1 : 0;
        double inE = 0.0;
        for (int c = 0; c < chs; ++c) { auto* x = b.getReadPointer (c); for (int i = 0; i < n; ++i) inE += (double) x[i] * x[i]; }
        // the knobs glide inside the block too, in steps of 32 samples (a big jump of the filters once per block, with
        // the sound already in them, is a step in the waveform: heard as a click when Light is flicked quickly)
        const float wT = juce::jlimit (0.0f, 1.0f, weight), lT = juce::jlimit (0.0f, 1.0f, light);
        w = w0; l = l0;
        for (int st = 0; st < n; st += 32)
        {
            const int m = juce::jmin (32, n - st);
            const float km = 1.0f - std::exp (-(float) m / (float) sr / 0.08f);
            w += (wT - w) * km; l += (lT - l) * km;
            const Bq lo = shelf (160.0f, (w - 0.5f) * 20.0f, false), hi = shelf (3500.0f, (l - 0.5f) * 20.0f, true);
            const float dull = juce::jlimit (0.0f, 1.0f, (0.5f - l) * 2.0f);
            const float fc = juce::jmin (20000.0f * std::pow (1500.0f / 20000.0f, dull), 0.45f * (float) sr);
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc / (float) sr);
            const float dullMix = juce::jlimit (0.0f, 1.0f, dull * 10.0f); // the low-pass fades in over the first part of the dull half
            for (int c = 0; c < chs; ++c)
            {
                auto* x = b.getWritePointer (c, st);
                auto& s = ch[(size_t) c];
                for (int i = 0; i < m; ++i)
                {
                    float y = lo.b0 * x[i] + s.z1; s.z1 = lo.b1 * x[i] - lo.a1 * y + s.z2; s.z2 = lo.b2 * x[i] - lo.a2 * y;
                    float v = hi.b0 * y + s.z3;    s.z3 = hi.b1 * y - hi.a1 * v + s.z4;    s.z4 = hi.b2 * y - hi.a2 * v;
                    s.lp += a * (v - s.lp);
                    x[i] = v + (s.lp - v) * dullMix;
                }
            }
        }
        double outE = 0.0;
        for (int c = 0; c < chs; ++c) { auto* x = b.getReadPointer (c); for (int i = 0; i < n; ++i) outE += (double) x[i] * x[i]; }
        const double ka = 1.0 - std::exp (-(double) n / sr / 0.4);
        eIn += (inE - eIn) * ka; eOut += (outE - eOut) * ka;
        const float target = eOut > 1.0e-12 && eIn > 1.0e-12 ? juce::jlimit (0.25f, 4.0f, (float) std::sqrt (eIn / eOut)) : gain;
        const float g0 = gain;
        gain += (target - gain) * (1.0f - std::exp (-(float) n / (float) sr / 0.1f));
        for (int c = 0; c < chs; ++c) b.applyGainRamp (c, 0, n, g0, gain);
    }

private:
    struct Bq { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Ch { float z1 = 0, z2 = 0, z3 = 0, z4 = 0, lp = 0; };
    Bq shelf (float hz, float db, bool high) const
    {
        const float A = std::pow (10.0f, db / 40.0f), w0 = juce::MathConstants<float>::twoPi * hz / (float) sr;
        const float cs = std::cos (w0), sn = std::sin (w0), S = 0.7f;
        const float al = sn * 0.5f * std::sqrt ((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f), q = 2.0f * std::sqrt (A) * al;
        const float sg = high ? -1.0f : 1.0f; // sign flips between the low and the high shelf
        const float a0 = (A + 1) + sg * (A - 1) * cs + q;
        Bq r;
        r.b0 = A * ((A + 1) - sg * (A - 1) * cs + q) / a0;
        r.b1 = sg * 2 * A * ((A - 1) - sg * (A + 1) * cs) / a0;
        r.b2 = A * ((A + 1) - sg * (A - 1) * cs - q) / a0;
        r.a1 = -sg * 2 * ((A - 1) + sg * (A + 1) * cs) / a0;
        r.a2 = ((A + 1) + sg * (A - 1) * cs - q) / a0;
        return r;
    }
    double sr = 48000.0;
    std::array<Ch, 2> ch;
    float w = 0.5f, l = 0.5f, gain = 1.0f;
    int settledBlocks = 1000;
    double eIn = 0.0, eOut = 0.0;
};
