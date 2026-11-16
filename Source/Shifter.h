#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// Frequency shifter and ring modulator for one stereo sound (v2).
// Frequency shifter: every frequency moves by the same number of Hz (not a pitch change: the partials stop being
// multiples of each other, so the sound turns bell-like or metallic). Done with a 90-degree phase splitter
// (two chains of all-pass filters; coefficients from O. Niemitalo's published half-band design) and single-sideband mixing.
// Ring modulator: the sound is multiplied by a sine wave, which gives the sum and the difference of the frequencies.
class Shifter
{
public:
    void prepare (double sr) { rate = sr; for (auto& c : ch) c = {}; phS = phR = 0.0; }

    // shiftHz: 0 = off (negative = down). ringHz: 0 = off. ringMix 0..1.
    void process (juce::AudioBuffer<float>& b, int n, float shiftHz, float ringHz, float ringMix)
    {
        const bool sOn = std::abs (shiftHz) > 0.05f, rOn = ringHz > 0.5f && ringMix > 0.001f;
        if (! sOn && ! rOn) return;
        const double dS = juce::MathConstants<double>::twoPi * (double) shiftHz / rate;
        const double dR = juce::MathConstants<double>::twoPi * (double) ringHz / rate;
        const int chs = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const float cs = (float) std::cos (phS), sn = (float) std::sin (phS), rg = (float) std::sin (phR);
            for (int c = 0; c < chs; ++c)
            {
                float x = b.getSample (c, i);
                if (sOn)
                {
                    auto& st = ch[(size_t) c];
                    float I = st.d1, q = x;           // I path: chain A, then one sample of delay
                    float a = x;
                    for (int k = 0; k < 4; ++k) a = st.a[(size_t) k].tick (a, kA[k] * kA[k]);
                    st.d1 = a;
                    for (int k = 0; k < 4; ++k) q = st.b[(size_t) k].tick (q, kB[k] * kB[k]);
                    x = I * cs + q * sn;
                }
                if (rOn) x = x * (1.0f - ringMix) + x * rg * ringMix * 1.4142f;
                b.setSample (c, i, x);
            }
            phS += dS; if (phS > juce::MathConstants<double>::twoPi) phS -= juce::MathConstants<double>::twoPi; if (phS < 0.0) phS += juce::MathConstants<double>::twoPi;
            phR += dR; if (phR > juce::MathConstants<double>::twoPi) phR -= juce::MathConstants<double>::twoPi;
        }
    }

private:
    struct AP { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; float tick (float x, float a2) { const float y = a2 * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
    struct Ch { std::array<AP, 4> a, b; float d1 = 0; };
    static constexpr float kA[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    static constexpr float kB[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    std::array<Ch, 2> ch;
    double rate = 48000.0, phS = 0.0, phR = 0.0;
};
