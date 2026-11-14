#include "SampleCleaner.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace SampleCleaner
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> monoMix (const juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples(), ch = b.getNumChannels();
    std::vector<float> m ((size_t) n, 0.0f);
    for (int c = 0; c < ch; ++c)
    {
        const float* s = b.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            m[(size_t) i] += s[i] / (float) ch;
    }
    return m;
}

// window shared by the profile estimate and the noise reduction, so that both are in the same units
float analysisWindow (int i, int n)
{
    const double hann = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
    return (float) std::sqrt (hann);
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void setNotch (double f0, double q, double fs)
    {
        const double w = 2.0 * kPi * f0 / fs, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = 1.0 / a0; b1 = -2.0 * std::cos (w) / a0; b2 = 1.0 / a0;
        a1 = -2.0 * std::cos (w) / a0; a2 = (1.0 - al) / a0;
    }
    void setHighPass (double f0, double q, double fs)
    {
        const double w = 2.0 * kPi * f0 / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 + c) / 2.0 / a0; b1 = -(1.0 + c) / a0; b2 = (1.0 + c) / 2.0 / a0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void run (float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            x[i] = (float) out;
        }
    }
};

// Average power spectrum of the quietest frames of a long FFT (used to find mains hum).
bool humSpectrum (const std::vector<float>& m, int fftSize, std::vector<double>& avg)
{
    const int n = (int) m.size();
    if (n < fftSize)
        return false;
    juce::dsp::FFT fft ((int) std::lround (std::log2 ((double) fftSize)));
    const int hop = fftSize / 2;
    const int frames = (n - fftSize) / hop + 1;
    std::vector<float> win ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) fftSize));

    struct F { double e; int start; };
    std::vector<F> fr;
    for (int k = 0; k < frames; ++k)
    {
        double e = 0.0;
        for (int i = 0; i < fftSize; ++i)
            e += (double) m[(size_t) (k * hop + i)] * m[(size_t) (k * hop + i)];
        fr.push_back ({ e, k * hop });
    }
    std::sort (fr.begin(), fr.end(), [] (const F& a, const F& b) { return a.e < b.e; });
    const int use = juce::jmax (3, (int) (fr.size() * 15 / 100));
    const int take = juce::jmin ((int) fr.size(), use);

    avg.assign ((size_t) fftSize / 2 + 1, 0.0);
    std::vector<float> work ((size_t) fftSize * 2);
    for (int f = 0; f < take; ++f)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            work[(size_t) i] = m[(size_t) (fr[(size_t) f].start + i)] * win[(size_t) i];
        fft.performRealOnlyForwardTransform (work.data(), true);
        for (int k = 0; k <= fftSize / 2; ++k)
            avg[(size_t) k] += (double) work[(size_t) (2 * k)] * work[(size_t) (2 * k)] + (double) work[(size_t) (2 * k + 1)] * work[(size_t) (2 * k + 1)];
    }
    for (auto& v : avg) v /= (double) take;
    return true;
}

// how far (in dB) a narrow peak stands out from the spectrum around it
double prominenceDb (const std::vector<double>& p, int centre)
{
    const int last = (int) p.size() - 1;
    if (centre - 8 < 0 || centre + 8 > last)
        return 0.0;
    double peak = 0.0;
    for (int k = centre - 1; k <= centre + 1; ++k)
        peak = juce::jmax (peak, p[(size_t) k]);
    std::vector<double> ref;
    for (int k = centre - 8; k <= centre + 8; ++k)
        if (std::abs (k - centre) > 2)
            ref.push_back (p[(size_t) k]);
    std::nth_element (ref.begin(), ref.begin() + (long) ref.size() / 2, ref.end());
    const double median = ref[ref.size() / 2];
    return 10.0 * std::log10 ((peak + 1.0e-20) / (median + 1.0e-20));
}
} // namespace

float CleanParams::lowCutHz (float knob) noexcept
{
    return 20.0f * std::pow (20.0f, juce::jlimit (0.0f, 1.0f, knob)); // 20 Hz .. 400 Hz
}

Analysis analyse (const juce::AudioBuffer<float>& data, double rate)
{
    Analysis a;
    const int n = data.getNumSamples();
    if (n < 64 || rate <= 0.0)
        return a;
    a.valid = true;

    const auto m = monoMix (data);

    // noise floor: quietest 10 % of 50 ms blocks
    const int blk = juce::jmax (64, (int) (rate * 0.05));
    std::vector<float> levels;
    for (int s = 0; s + blk <= n; s += blk)
    {
        double e = 0.0;
        for (int i = 0; i < blk; ++i)
            e += (double) m[(size_t) (s + i)] * m[(size_t) (s + i)];
        levels.push_back ((float) (10.0 * std::log10 (e / blk + 1.0e-10)));
    }
    if (levels.empty())
    {
        double e = 0.0;
        for (float v : m) e += (double) v * v;
        levels.push_back ((float) (10.0 * std::log10 (e / n + 1.0e-10)));
    }
    std::sort (levels.begin(), levels.end());
    a.noiseFloorDb = juce::jmax (-100.0f, levels[(size_t) ((levels.size() - 1) / 10)]);

    // clipping: 3 or more samples in a row at the maximum
    for (int c = 0; c < data.getNumChannels(); ++c)
    {
        const float* s = data.getReadPointer (c);
        int run = 0;
        for (int i = 0; i < n; ++i)
        {
            if (std::abs (s[i]) >= 0.999f) ++run;
            else { if (run >= 3) ++a.clippedSpots; run = 0; }
        }
        if (run >= 3) ++a.clippedSpots;
    }

    // mains hum (50 or 60 Hz and harmonics) in the quiet parts
    int fftSize = 16384;
    while (fftSize > n) fftSize /= 2;
    if (fftSize >= 8192)
    {
        std::vector<double> avg;
        if (humSpectrum (m, fftSize, avg))
        {
            const double binHz = rate / (double) fftSize;
            int bestHz = 0, bestCount = 0;
            double bestScore = 0.0;
            for (int f0 : { 50, 60 })
            {
                int count = 0, odd = 0;
                double score = 0.0;
                for (int h = 1; h <= 6; ++h)
                {
                    const double f = (double) f0 * h;
                    if (f > 0.4 * rate) break;
                    const double pr = prominenceDb (avg, (int) std::lround (f / binHz));
                    if (pr >= 10.0)
                    {
                        ++count;
                        score += pr;
                        if (h % 2 == 1) ++odd;
                    }
                }
                // a real hum has at least two harmonics, and at least one odd one (a plain 100 Hz tone has none)
                if (count >= 2 && odd >= 1 && (count > bestCount || (count == bestCount && score > bestScore)))
                {
                    bestHz = f0; bestCount = count; bestScore = score;
                }
            }
            a.humHz = bestHz;
            a.humHarmonics = bestCount;
        }
    }
    return a;
}

NoiseProfile estimateProfile (const juce::AudioBuffer<float>& data, double rate, bool quietestOnly)
{
    NoiseProfile pr;
    const int n = data.getNumSamples();
    if (n < 256 || rate <= 0.0)
        return pr;
    const int N = NoiseProfile::kFft, hop = N / 2;
    const auto m = monoMix (data);
    juce::dsp::FFT fft (11);

    const int frames = juce::jmax (1, (n - N) / hop + 1);
    struct F { double e; int start; };
    std::vector<F> fr;
    for (int k = 0; k < frames; ++k)
    {
        double e = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const int idx = k * hop + i;
            const float v = idx < n ? m[(size_t) idx] : 0.0f;
            e += (double) v * v;
        }
        fr.push_back ({ e, k * hop });
    }
    int take = (int) fr.size();
    if (quietestOnly)
    {
        std::sort (fr.begin(), fr.end(), [] (const F& a, const F& b) { return a.e < b.e; });
        take = juce::jmin ((int) fr.size(), juce::jmax (4, (int) (fr.size() * 15 / 100)));
    }

    pr.power.assign ((size_t) N / 2 + 1, 0.0f);
    std::vector<float> work ((size_t) N * 2);
    for (int f = 0; f < take; ++f)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int idx = fr[(size_t) f].start + i;
            work[(size_t) i] = (idx < n ? m[(size_t) idx] : 0.0f) * analysisWindow (i, N);
        }
        fft.performRealOnlyForwardTransform (work.data(), true);
        for (int k = 0; k <= N / 2; ++k)
            pr.power[(size_t) k] += (work[(size_t) (2 * k)] * work[(size_t) (2 * k)] + work[(size_t) (2 * k + 1)] * work[(size_t) (2 * k + 1)]) / (float) take;
    }
    pr.rate = rate;
    pr.valid = true;
    return pr;
}

juce::AudioBuffer<float> clean (const juce::AudioBuffer<float>& in, double rate, const CleanParams& p,
                                const NoiseProfile& profile, int humHz, int humHarmonics)
{
    juce::AudioBuffer<float> out (in);
    const int n = out.getNumSamples(), chs = out.getNumChannels();
    if (n < 2 || ! p.any())
        return out;

    // 1. low cut: 4th order Butterworth high-pass
    if (p.lowCut > 0.01f)
    {
        const double fc = juce::jmin (CleanParams::lowCutHz (p.lowCut), 0.45f * (float) rate);
        for (int c = 0; c < chs; ++c)
        {
            Biquad b1, b2;
            b1.setHighPass (fc, 0.5412, rate);
            b2.setHighPass (fc, 1.3066, rate);
            b1.run (out.getWritePointer (c), n);
            b2.run (out.getWritePointer (c), n);
        }
    }

    // 2. hum: narrow notches at 50/60 Hz and the first harmonics, blended in by the Hum amount
    if (p.hum > 0.01f && humHz > 0)
    {
        const int harmonics = juce::jlimit (3, 8, humHarmonics + 2);
        for (int c = 0; c < chs; ++c)
        {
            std::vector<float> wet (out.getReadPointer (c), out.getReadPointer (c) + n);
            for (int h = 1; h <= harmonics; ++h)
            {
                const double f = (double) humHz * h;
                if (f > 0.45 * rate) break;
                Biquad nb;
                nb.setNotch (f, 30.0, rate);
                nb.run (wet.data(), n);
            }
            float* d = out.getWritePointer (c);
            for (int i = 0; i < n; ++i)
                d[i] = d[i] * (1.0f - p.hum) + wet[(size_t) i] * p.hum;
        }
    }

    // 3. noise: spectral subtraction with over-subtraction and a spectral floor (Boll 1979, Berouti et al. 1979)
    if (p.noise > 0.01f && profile.valid)
    {
        const int N = NoiseProfile::kFft, hop = N / 4, half = N / 2;
        // the noise profile in the bins of this sound (the rates may differ)
        std::vector<float> noiseP ((size_t) half + 1);
        for (int k = 0; k <= half; ++k)
        {
            const double q = (double) k * rate / profile.rate;
            const int i0 = juce::jmin ((int) q, half);
            const int i1 = juce::jmin (i0 + 1, half);
            const float f = (float) juce::jlimit (0.0, 1.0, q - (double) i0);
            noiseP[(size_t) k] = profile.power[(size_t) i0] * (1.0f - f) + profile.power[(size_t) i1] * f;
        }
        const float alpha = 0.5f + 3.5f * p.noise; // how much more than the average noise is taken away
        const float floorRel = 0.02f;              // what stays of the noise power, so that no holes ("musical noise") appear

        juce::dsp::FFT fft (11);
        std::vector<float> win ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = analysisWindow (i, N);
        std::vector<float> work ((size_t) N * 2), gain ((size_t) half + 1), gs ((size_t) half + 1);
        const int padded = n + 2 * N;
        for (int c = 0; c < chs; ++c)
        {
            std::vector<float> x ((size_t) padded, 0.0f), y ((size_t) padded, 0.0f);
            std::copy (out.getReadPointer (c), out.getReadPointer (c) + n, x.begin() + N);
            for (int s = 0; s + N <= padded; s += hop)
            {
                std::fill (work.begin(), work.end(), 0.0f);
                for (int i = 0; i < N; ++i)
                    work[(size_t) i] = x[(size_t) (s + i)] * win[(size_t) i];
                fft.performRealOnlyForwardTransform (work.data(), true);
                for (int k = 0; k <= half; ++k)
                {
                    const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
                    const float pw = re * re + im * im + 1.0e-20f;
                    const float nz = noiseP[(size_t) k];
                    const float clean2 = juce::jmax (pw - alpha * nz, floorRel * nz);
                    gain[(size_t) k] = juce::jmin (1.0f, std::sqrt (clean2 / pw));
                }
                for (int k = 0; k <= half; ++k) // smooth over 3 bins: isolated random dips do not become tones
                {
                    const float a = gain[(size_t) juce::jmax (0, k - 1)], b = gain[(size_t) k], d = gain[(size_t) juce::jmin (half, k + 1)];
                    gs[(size_t) k] = 0.25f * a + 0.5f * b + 0.25f * d;
                }
                for (int k = 0; k <= half; ++k)
                {
                    work[(size_t) (2 * k)] *= gs[(size_t) k];
                    work[(size_t) (2 * k + 1)] *= gs[(size_t) k];
                }
                fft.performRealOnlyInverseTransform (work.data());
                for (int i = 0; i < N; ++i)
                    y[(size_t) (s + i)] += work[(size_t) i] * win[(size_t) i] * 0.5f; // sqrt-Hann twice at 75 % overlap sums to 2
            }
            std::copy (y.begin() + N, y.begin() + N + n, out.getWritePointer (c));
        }
    }
    return out;
}
} // namespace SampleCleaner
