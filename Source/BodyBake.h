#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "SpectralStretch.h"
#include "AdditiveVoice.h"
#include "MorphVoice.h"

// "Bakes" the colour (Tone / Focus / Size) into a copy of a sound, so that the grains can be cut from the coloured copy.
// Same envelope maths as SpectralStretch::applyBody, but here the phases of the sound are kept (short-time Fourier
// transform with overlap-add), so with the knobs in the middle the copy equals the sound.
inline juce::AudioBuffer<float> bakeBody (const juce::AudioBuffer<float>& in, double rate, const SpectralBody& body, float blur = 0.0f)
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
    juce::Random rng (4321);
    const bool shape = body.shapeActive();
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
            if (shape && maxE > 1.0e-12f)
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
            if (body.rough > 0.01f)
            {   // Grit: a random level change per ~60 Hz patch, new for every frame; the loudness is kept
                const float amt = body.rough * 1.2f;
                const int spacing = juce::jmax (2, (int) std::lround (60.0 / binHz));
                double b0 = 0.0, a0 = 0.0;
                float g0 = std::exp (amt * (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 1.5f) * 2.0f), g1 = g0;
                for (int k = 0; k <= bins; ++k)
                {
                    if (k % spacing == 0) { g0 = g1; g1 = std::exp (amt * (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 1.5f) * 2.0f); }
                    const float g = g0 + (g1 - g0) * (float) (k % spacing) / (float) spacing;
                    const double pw = (double) buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + (double) buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)];
                    b0 += pw; a0 += pw * g * g;
                    buf[(size_t) (2 * k)] *= g; buf[(size_t) (2 * k + 1)] *= g;
                }
                const float fx = (float) std::sqrt (b0 / juce::jmax (1.0e-20, a0));
                for (int k = 0; k <= 2 * bins + 1; ++k) buf[(size_t) k] *= fx;
            }
            if (blur > 0.01f)
            {   // Blur: the levels of the spectrum are smoothed (triangle), the loudness is kept
                const int hw = juce::jmax (1, (int) std::lround ((1.0 + 79.0 * (double) blur * blur) * (double) N / 16384.0));
                double b0 = 0.0, a0 = 0.0; float mx = 0.0f;
                for (int k = 0; k <= bins; ++k) { mag[(size_t) k] = std::hypot (buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)]); b0 += (double) mag[(size_t) k] * mag[(size_t) k]; mx = juce::jmax (mx, mag[(size_t) k]); }
                std::copy (mag.begin(), mag.end(), tmp.begin());
                for (int pass = 0; pass < 2; ++pass)
                {
                    pre[0] = 0.0f;
                    for (int k = 0; k <= bins; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + tmp[(size_t) k];
                    for (int k = 0; k <= bins; ++k)
                    {
                        const int a = juce::jmax (0, k - hw), b = juce::jmin (bins, k + hw);
                        env[(size_t) k] = (pre[(size_t) b + 1] - pre[(size_t) a]) / (float) (b - a + 1);
                    }
                    std::copy (env.begin(), env.end(), tmp.begin());
                }
                for (int k = 0; k <= bins; ++k) a0 += (double) env[(size_t) k] * env[(size_t) k];
                const float fx = (float) std::sqrt (b0 / juce::jmax (1.0e-20, a0));
                for (int k = 0; k <= bins; ++k)
                {
                    const float m0 = mag[(size_t) k];
                    if (m0 > 1.0e-7f * mx) { const float g = env[(size_t) k] * fx / m0; buf[(size_t) (2 * k)] *= g; buf[(size_t) (2 * k + 1)] *= g; }
                    else { const float ph = rng.nextFloat() * juce::MathConstants<float>::twoPi; buf[(size_t) (2 * k)] = env[(size_t) k] * fx * std::cos (ph); buf[(size_t) (2 * k + 1)] = env[(size_t) k] * fx * std::sin (ph); }
                }
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

// The sound played by its sine-wave description (Model), mixed with the sound itself like the live Model knob does.
inline juce::AudioBuffer<float> bakeModel (const juce::AudioBuffer<float>& play, const SineModel& model, float mix, float noise)
{
    const int n = play.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    out.clear();
    AdditiveVoice v;
    v.prepare();
    v.start (99);
    for (int done = 0; done < n; done += 256)
        v.process (model, 1.0, 1.0, out.getWritePointer (0) + done, out.getWritePointer (1) + done, juce::jmin (256, n - done), noise);
    out.applyGain (mix);
    for (int c = 0; c < 2; ++c)
        out.addFrom (c, 0, play, juce::jmin (c, play.getNumChannels() - 1), 0, n, 1.0f - mix);
    return out;
}

// Air: the part of a sound that is not notes (what the sine-wave description leaves over: breath, hiss, texture).
// air 0 = without it (only the notes), 0.5 = the sound as it is, towards 1 the air gets louder until it is as loud
// as the sound (0.75), and at 1 only the air is left, at the loudness of the sound.
inline void applyAir (juce::AudioBuffer<float>& play, const SineModel& model, float air)
{
    const int n = play.getNumSamples();
    if (n < 64 || std::abs (air - 0.5f) < 0.01f) return;
    const bool less = air < 0.5f;
    juce::AudioBuffer<float> r (2, n);
    r.clear();
    AdditiveVoice v;
    v.prepare (less);
    v.start (7);
    for (int done = 0; done < n; done += 256)
        v.process (model, 1.0, 1.0, r.getWritePointer (0) + done, r.getWritePointer (1) + done, juce::jmin (256, n - done), less ? 0.0f : 1.0f);
    float gSound, gOther;
    if (less) { const float t = (0.5f - air) * 2.0f; gSound = 1.0f - t; gOther = t; }
    else
    {
        const float t = (air - 0.5f) * 2.0f;
        double a = 0.0, b = 0.0;
        for (int c = 0; c < play.getNumChannels(); ++c) { const float x = play.getRMSLevel (c, 0, n); a += (double) x * x; }
        for (int c = 0; c < 2; ++c) { const float x = r.getRMSLevel (c, 0, n); b += (double) x * x; }
        a /= (double) play.getNumChannels(); b /= 2.0;
        const float eq = b > 1.0e-20 ? (float) juce::jmin (63.0, std::sqrt (a / b)) : 0.0f; // air as loud as the sound (at most +36 dB)
        gSound = t < 0.5f ? 1.0f : 2.0f - 2.0f * t;
        gOther = eq * juce::jmin (1.0f, 2.0f * t);
    }
    for (int c = 0; c < play.getNumChannels(); ++c)
    {
        play.applyGain (c, 0, n, gSound);
        play.addFrom (c, 0, r, juce::jmin (c, 1), 0, n, gOther);
    }
}

// The morph of a pair rendered into a copy of the sound (for the grains): sound A turning into B by the amount `morph`,
// with the hits of both sounds laid back over it and the level moved towards B's auto level, like the live voice does.
inline juce::AudioBuffer<float> bakeMorph (const juce::AudioBuffer<float>& play, const SineModel& A, const SineModel& B, float morph,
                                           const juce::AudioBuffer<float>& hitsA, const std::vector<float>& gateA,
                                           const juce::AudioBuffer<float>& hitsB, const std::vector<float>& gateB, float levelRatio)
{
    const int n = play.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    out.clear();
    MorphVoice v;
    v.prepare();
    v.start (4242);
    for (int done = 0; done < n; done += 256)
        v.process (A, B, 1.0, 1.0, morph, 1.0f, out.getWritePointer (0) + done, out.getWritePointer (1) + done, juce::jmin (256, n - done));
    const float wa = 1.0f - morph, wb = morph, g = std::pow (juce::jlimit (0.06f, 16.0f, levelRatio), morph);
    const int na = juce::jmin (hitsA.getNumSamples(), (int) gateA.size()), nb = juce::jmin (hitsB.getNumSamples(), (int) gateB.size());
    for (int c = 0; c < 2; ++c)
    {
        float* d = out.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float gm = wa * (i < na ? gateA[(size_t) i] : 0.0f) + wb * (i < nb ? gateB[(size_t) i] : 0.0f);
            float x = d[i] * (1.0f - gm);
            if (i < na) x += wa * hitsA.getSample (juce::jmin (c, hitsA.getNumChannels() - 1), i);
            if (i < nb) x += wb * hitsB.getSample (juce::jmin (c, hitsB.getNumChannels() - 1), i);
            d[i] = x * g;
        }
    }
    return out;
}
