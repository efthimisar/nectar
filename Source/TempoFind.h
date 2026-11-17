#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

// The tempo of a sound, for locking loops to the host tempo.
// 1. From the file name: a number between 60 and 200 standing on its own ("Pad - Percussion - 124", "drums_95bpm").
// 2. Otherwise from the sound: an onset strength signal (half-wave rectified log spectral flux) whose autocorrelation is
//    combined with how well regularly spaced pulses line up with it - the two ideas of the streamlined tempo estimation
//    of Percival & Tzanetakis (2014, IEEE/ACM TASLP); our version is simplified and its weights are our own.
// 3. A loop is usually a whole number of bars (1, 2, 4, 8, 16 bars of 4 beats): if the length of the sound gives a tempo
//    within 4 % of the estimate (or its half / double), that exact tempo is taken. Our own rule.
// A sound counts as having a tempo only if its pulse is clear enough and it lasts at least four beats (our own limits).
struct TempoFind
{
    float bpm = 0.0f;      // 0 = no tempo
    bool fromName = false;
    float clarity = 0.0f;

    static float fromFileName (const juce::String& name)
    {
        const auto s = name.toLowerCase();
        float best = 0.0f;
        int i = 0;
        const int n = s.length();
        while (i < n)
        {
            if (! juce::CharacterFunctions::isDigit (s[i])) { ++i; continue; }
            int j = i;
            while (j < n && (juce::CharacterFunctions::isDigit (s[j]) || (s[j] == '.' && j + 1 < n && juce::CharacterFunctions::isDigit (s[j + 1])))) ++j;
            const bool alone = (i == 0 || ! juce::CharacterFunctions::isLetterOrDigit (s[i - 1])) && (j >= n || ! juce::CharacterFunctions::isDigit (s[j]));
            const float v = s.substring (i, j).getFloatValue();
            const bool saysBpm = s.substring (j, j + 4).trimStart().startsWith ("bpm");
            if (v >= 60.0f && v <= 200.0f && (alone || saysBpm))
            {
                if (saysBpm) return v;
                best = v; // the last such number wins (names often end with the tempo)
            }
            i = j;
        }
        return best;
    }

    static TempoFind of (const juce::AudioBuffer<float>& x, double rate, const juce::String& name)
    {
        TempoFind t;
        if (const float v = fromFileName (name); v > 0.0f) { t.bpm = v; t.fromName = true; t.clarity = 1.0f; return t; }
        const int len = x.getNumSamples();
        const double secs = (double) len / rate;
        if (secs < 1.2) return t;

        // onset strength: frames of 1024 at a hop of about 5.8 ms (independent of the rate)
        const int hop = juce::jmax (64, (int) std::lround (rate * 0.0058)), N = 1024;
        const int frames = (len - N) / hop;
        if (frames < 64) return t;
        juce::dsp::FFT fft (10);
        std::vector<float> buf (2 * N), prev (N / 2 + 1, 0.0f), win (N), oss ((size_t) frames, 0.0f);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        for (int f = 0; f < frames; ++f)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
            {
                float m = 0.0f;
                for (int c = 0; c < x.getNumChannels(); ++c) m += x.getSample (c, f * hop + i);
                buf[(size_t) i] = m * win[(size_t) i];
            }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            float flux = 0.0f;
            for (int k = 1; k <= N / 2; ++k)
            {
                const float m = std::log1p (1000.0f * buf[(size_t) k]);
                flux += juce::jmax (0.0f, m - prev[(size_t) k]);
                prev[(size_t) k] = m;
            }
            oss[(size_t) f] = f == 0 ? 0.0f : flux;
        }
        {   // remove the slow trend (local mean over about 0.25 s)
            const int w = juce::jmax (4, (int) (0.25 / (hop / rate)));
            std::vector<float> o2 (oss.size());
            double acc = 0.0; int cnt = 0;
            for (int i = 0; i < (int) oss.size() + w; ++i)
            {
                if (i < (int) oss.size()) { acc += oss[(size_t) i]; ++cnt; }
                if (i - 2 * w >= 0) { acc -= oss[(size_t) (i - 2 * w)]; --cnt; }
                const int c = i - w;
                if (c >= 0 && c < (int) oss.size()) o2[(size_t) c] = juce::jmax (0.0f, oss[(size_t) c] - (float) (acc / juce::jmax (1, cnt)));
            }
            oss.swap (o2);
        }
        const double fps = rate / hop;
        auto lagOf = [&] (double b) { return 60.0 * fps / b; };
        auto ac = [&] (double lag)
        {   // autocorrelation at a fractional lag (linear interpolation), normalised by the energy
            double s = 0.0, e = 0.0;
            const int L = (int) lag; const double fr = lag - L;
            for (int i = 0; i + L + 1 < (int) oss.size(); ++i)
            {
                const double y = oss[(size_t) (i + L)] * (1.0 - fr) + oss[(size_t) (i + L + 1)] * fr;
                s += oss[(size_t) i] * y;
            }
            for (float v : oss) e += (double) v * v;
            return e > 0.0 ? s / e : 0.0;
        };
        auto pulse = [&] (double b)
        {   // best line-up of a regular pulse train with the onsets, over phases
            const double lag = lagOf (b);
            double best = 0.0, tot = 0.0;
            for (float v : oss) tot += v;
            if (tot <= 0.0) return 0.0;
            for (int ph = 0; ph < (int) lag; ++ph)
            {
                double s = 0.0;
                for (double p = ph; p < (double) oss.size(); p += lag) s += oss[(size_t) p];
                best = juce::jmax (best, s);
            }
            return best * lag / tot; // onset energy on the best pulse train, relative to a pulse train at a random phase (1 = chance)
        };
        double bestB = 0.0, bestS = -1.0, bestAc = 0.0;
        for (double b = 60.0; b <= 200.0; b += 0.5)
        {
            const double lag = lagOf (b);
            if (lag * 4.0 > (double) oss.size()) continue;
            const double a = ac (lag) + 0.5 * ac (2.0 * lag) + 0.25 * ac (lag * 0.5);
            const double s = a * (0.5 + 0.25 * juce::jlimit (0.0, 4.0, pulse (b) - 1.0));
            const double pref = std::exp (-0.5 * std::pow (std::log2 (b / 115.0) / 0.9, 2.0)); // a mild leaning to moderate tempi
            if (s * pref > bestS) { bestS = s * pref; bestB = b; bestAc = ac (lag); }
        }
        if (bestB <= 0.0 || bestAc < 0.35 || secs * bestB / 60.0 < 4.0) return t;
        t.clarity = (float) bestAc;
        t.bpm = (float) bestB;
        // a loop of whole bars: take the exact tempo its length gives, if it is close
        for (double bars : { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 })
        {
            const double exact = 240.0 * bars / secs;
            for (double m : { 1.0, 0.5, 2.0 })
                if (std::abs (exact - bestB * m) / exact < 0.04 && exact >= 60.0 && exact <= 200.0) { t.bpm = (float) exact; return t; }
        }
        return t;
    }
};
