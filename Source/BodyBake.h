#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "SpectralStretch.h"

// "Bakes" the colour (Tone / Focus / Size) into a copy of a sound, so that the grains can be cut from the coloured copy.
// Same envelope maths as SpectralStretch::applyBody, but here the phases of the sound are kept (short-time Fourier
// transform with overlap-add), so with the knobs in the middle the copy equals the sound.
inline juce::AudioBuffer<float> bakeBody (const juce::AudioBuffer<float>& in, double rate, const SpectralBody& body)
{
    const int order = 12, N = 1 << order, hop = N / 4, bins = N / 2;
    const int n = in.getNumSamples(), chs = in.getNumChannels();
    juce::AudioBuffer<float> out (chs, n);
    out.clear();
    if (n < N) { out = in; return out; }
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N), buf ((size_t) (2 * N)), mag ((size_t) bins + 1), env ((size_t) bins + 1), tmp ((size_t) bins + 1), pre ((size_t) bins + 2);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    const float norm = 1.0f / 1.5f; // Hann squared at 75 % overlap sums to 1.5
    const int hwMin = juce::jmax (2, (int) std::lround (150.0 * (double) N / rate));
    const double sh = std::pow (2.0, (double) (body.size - 0.5f) * 1.5);
    const float expo = std::pow (2.0f, (body.focus - 0.5f) * 2.0f);
    const float tilt = (body.tone - 0.5f) * 8.0f;
    const double binHz = rate / (double) N;
    std::vector<float> tiltGain ((size_t) bins + 1);
    for (int k = 0; k <= bins; ++k)
        tiltGain[(size_t) k] = std::pow (10.0f, tilt * (float) std::log2 (juce::jlimit (150.0, 8000.0, (double) k * binHz) / 1000.0) / 20.0f);

    for (int c = 0; c < chs; ++c)
    {
        const float* src = in.getReadPointer (c);
        float* dst = out.getWritePointer (c);
        for (int start = -N + hop; start < n; start += hop)
        {
            for (int i = 0; i < N; ++i)
            {
                const int idx = start + i;
                buf[(size_t) i] = (idx >= 0 && idx < n) ? src[idx] * win[(size_t) i] : 0.0f;
            }
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (buf.data(), true);
            float maxE = 0.0f; double before = 0.0;
            for (int k = 0; k <= bins; ++k) { mag[(size_t) k] = std::hypot (buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)]); before += (double) mag[(size_t) k] * mag[(size_t) k]; }
            std::copy (mag.begin(), mag.end(), tmp.begin());
            for (int pass = 0; pass < 2; ++pass)
            {
                pre[0] = 0.0f;
                for (int k = 0; k <= bins; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + tmp[(size_t) k];
                for (int k = 0; k <= bins; ++k)
                {
                    const int hw = juce::jmax (hwMin, (int) (0.15 * (double) k));
                    const int a = juce::jmax (0, k - hw), b = juce::jmin (bins, k + hw);
                    env[(size_t) k] = (pre[(size_t) b + 1] - pre[(size_t) a]) / (float) (b - a + 1);
                }
                std::copy (env.begin(), env.end(), tmp.begin());
            }
            for (int k = 0; k <= bins; ++k) maxE = juce::jmax (maxE, env[(size_t) k]);
            if (maxE > 1.0e-12f)
            {
                const float fl = 1.0e-3f * maxE;
                double after = 0.0;
                for (int k = 0; k <= bins; ++k)
                {
                    const double q = (double) k * sh;
                    float e;
                    if (q >= (double) bins) e = env[(size_t) bins];
                    else { const int i0 = (int) q; const float f = (float) (q - (double) i0); e = env[(size_t) i0] * (1.0f - f) + env[(size_t) juce::jmin (bins, i0 + 1)] * f; }
                    e = maxE * std::pow ((e + fl) / (maxE + fl), expo) * tiltGain[(size_t) k];
                    const float g = e / (env[(size_t) k] + fl);
                    tmp[(size_t) k] = g;
                    after += (double) mag[(size_t) k] * g * mag[(size_t) k] * g;
                }
                const float fix = (float) std::sqrt (before / juce::jmax (1.0e-20, after));
                for (int k = 0; k <= bins; ++k) { buf[(size_t) (2 * k)] *= tmp[(size_t) k] * fix; buf[(size_t) (2 * k + 1)] *= tmp[(size_t) k] * fix; }
            }
            fft.performRealOnlyInverseTransform (buf.data());
            for (int i = 0; i < N; ++i)
            {
                const int idx = start + i;
                if (idx >= 0 && idx < n) dst[idx] += buf[(size_t) i] * win[(size_t) i] * norm;
            }
        }
    }
    return out;
}
