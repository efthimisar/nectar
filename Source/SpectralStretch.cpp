#include "SpectralStretch.h"
#include <cmath>

void SpectralStretch::prepare (double sampleRate)
{
    order = sampleRate > 60000.0 ? 15 : 14; // about 0.37 s of sound per frame
    N = 1 << order;
    hop = N / 2;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i) // sine window: two frames shifted by half a frame add up to a constant
        window[(size_t) i] = std::sin (juce::MathConstants<float>::pi * ((float) i + 0.5f) / (float) N);
    work.assign ((size_t) N * 2, 0.0f);
    mag.assign ((size_t) N / 2 + 1, 0.0f);
    shifted.assign ((size_t) N / 2 + 1, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        tail[c].assign ((size_t) hop, 0.0f);
        hopOut[c].assign ((size_t) hop, 0.0f);
    }
    readIdx = hop;
}

void SpectralStretch::start (const juce::AudioBuffer<float>& src, double startPos, double ratio)
{
    const int n = src.getNumSamples();
    pos = n > 0 ? std::fmod (juce::jmax (0.0, startPos), (double) n) : 0.0;
    for (int c = 0; c < 2; ++c)
    {
        std::fill (tail[c].begin(), tail[c].end(), 0.0f);
        std::fill (hopOut[c].begin(), hopOut[c].end(), 0.0f);
    }
    // One frame in advance, so that the very first output is already at full level (no slow fade-in).
    makeFrame (src, 1.0, ratio, false);
    readIdx = hop; // the next call to process() makes the second frame
}

void SpectralStretch::makeFrame (const juce::AudioBuffer<float>& src, double stretch, double ratio, bool advance)
{
    const int n = src.getNumSamples();
    const int chs = src.getNumChannels();
    if (n < 2)
        return;

    for (int c = 0; c < 2; ++c)
    {
        const float* s = src.getReadPointer (juce::jmin (c, chs - 1));
        const int base = (int) pos;
        for (int i = 0; i < N; ++i)
        {
            int idx = base + i;
            if (idx >= n) idx %= n;
            work[(size_t) i] = s[idx] * window[(size_t) i];
        }
        std::fill (work.begin() + N, work.end(), 0.0f);
        fft->performRealOnlyForwardTransform (work.data(), true);

        const int bins = N / 2;
        for (int k = 0; k <= bins; ++k)
            mag[(size_t) k] = std::hypot (work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)]);

        // pitch: bin k takes the magnitude found at k / ratio (frequencies above the top are dropped)
        const double inv = 1.0 / juce::jmax (0.01, ratio);
        for (int k = 0; k <= bins; ++k)
        {
            const double q = (double) k * inv;
            const int i0 = (int) q;
            if (i0 >= bins) { shifted[(size_t) k] = 0.0f; continue; }
            const float f = (float) (q - (double) i0);
            shifted[(size_t) k] = mag[(size_t) i0] * (1.0f - f) + mag[(size_t) (i0 + 1)] * f;
        }

        for (int k = 0; k <= bins; ++k)
        {
            const float ph = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const bool edge = (k == 0 || k == bins); // DC and the top bin must stay real
            work[(size_t) (2 * k)] = shifted[(size_t) k] * (edge ? 1.0f : std::cos (ph));
            work[(size_t) (2 * k + 1)] = edge ? 0.0f : shifted[(size_t) k] * std::sin (ph);
        }
        fft->performRealOnlyInverseTransform (work.data());

        // overlap-add: first half joins the stored tail, second half is kept for the next frame
        const float gain = kGain / (float) std::sqrt (juce::jmax (0.05, ratio)); // moving bins up/down changes the energy by about the ratio
        for (int i = 0; i < hop; ++i)
            hopOut[c][(size_t) i] = tail[c][(size_t) i] + work[(size_t) i] * window[(size_t) i] * gain;
        for (int i = 0; i < hop; ++i)
            tail[c][(size_t) i] = work[(size_t) (hop + i)] * window[(size_t) (hop + i)] * gain;
    }

    if (advance)
    {
        pos += (double) hop / juce::jmax (1.0, stretch);
        pos = std::fmod (pos, (double) n);
    }
}

void SpectralStretch::process (const juce::AudioBuffer<float>& src, double stretch, double ratio, float* outL, float* outR, int num)
{
    for (int i = 0; i < num; ++i)
    {
        if (readIdx >= hop)
        {
            makeFrame (src, stretch, ratio, true);
            readIdx = 0;
        }
        outL[i] = hopOut[0][(size_t) readIdx];
        outR[i] = hopOut[1][(size_t) readIdx];
        ++readIdx;
    }
}
