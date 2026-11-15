#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Bitcrusher: word-length reduction (quantiser) and sample-rate reduction (sample and hold), with optional TPDF dither.
// Plain textbook signal processing, no particular hardware is modelled.
class Bitcrusher
{
public:
    void prepare (double) { reset(); }
    void reset() { for (auto& h : held) h = 0.0f; phase = 1.0; }

    // bits: 3..16 (fractional allowed), factor: 1..32 = keep one sample out of 'factor', dither 0..1, mix 0..1
    void process (juce::AudioBuffer<float>& b, int n, float bits, float factor, float dither, float mix)
    {
        if (mix <= 0.0005f)
        {
            reset();
            return;
        }
        const float step = std::pow (2.0f, 1.0f - bits); // full scale -1..1 is 2.0 wide
        const double inc = 1.0 / juce::jmax (1.0, (double) factor);
        const int chs = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            const bool take = phase >= 1.0;
            if (take) phase -= 1.0;
            const float d = dither > 0.0f ? dither * step * (rng.nextFloat() - rng.nextFloat()) : 0.0f; // TPDF, +-1 step
            for (int ch = 0; ch < chs; ++ch)
            {
                float* x = b.getWritePointer (ch);
                if (take)
                    held[ch] = step * std::floor ((x[i] + d) / step + 0.5f);
                x[i] += mix * (held[ch] - x[i]);
            }
        }
    }

private:
    float held[2] { 0.0f, 0.0f };
    double phase = 1.0;
    juce::Random rng { 1234 };
};
