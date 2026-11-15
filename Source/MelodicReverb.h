#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

// Creative, pitched reverb: an 8-line feedback delay network (FDN) with a Hadamard feedback matrix.
// Structure: after the FDN idea of Jot & Chaigne (1991) / Stautner & Puckette (1982) -- from general knowledge, the papers were not checked.
// What is our own design:
//   * "Tuned": line k has a loop frequency root * ratio[k] (ratios 1, 3/2, 2, 3, 4, 5, 6, 8), so the tail rings as a chord.
//     The Size knob picks the root note (A1 .. A4, one semitone steps, knob up = higher note).
//   * "Shimmer": the tail is shifted an octave up (two crossfaded read taps of a delay line) and fed back into the network.
// Decay: the time for -60 dB (T60) of every line is the same, 0.3 .. 20 s; a low-pass in each line makes high frequencies decay faster.
class MelodicReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& l : line) { l.buf.assign ((size_t) (sr / 30.0) + 8, 0.0f); l.pos = 0; l.lp = 0.0f; l.len = 100.0f; }
        for (int c = 0; c < 2; ++c)
            for (int a = 0; a < 2; ++a) { ap[c][a].assign ((size_t) (sr * 0.01) + 8, 0.0f); apPos[c][a] = 0; }
        shift.assign (kShiftLen * 2, 0.0f);
        shPos = 0; shPhase = 0.0f;
        smoothLen = -1.0f;
    }

    void process (juce::AudioBuffer<float>& b, int n, float size, float decay, float shimmer, float mix)
    {
        if (mix <= 0.0005f && lastMix <= 0.0005f)
            { lastMix = mix; return; }
        lastMix = mix;
        static const float ratio[kLines] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
        const int midi = 33 + juce::jlimit (0, 36, (int) std::floor (size * 36.999f));
        const float root = 440.0f * std::pow (2.0f, ((float) midi - 69.0f) / 12.0f);
        // with shimmer the tail is capped: the octave path feeds back and a nearly lossless network would grow
        const float t60 = juce::jmin (0.3f * std::pow (20.0f / 0.3f, decay), 20.0f - 14.0f * shimmer);
        const float shim = shimmer * 0.5f;
        const int chans = juce::jmin (2, b.getNumChannels());
        float* L = b.getWritePointer (0);
        float* R = chans > 1 ? b.getWritePointer (1) : L;
        const float slew = 1.0f - std::exp (-1.0f / (0.05f * (float) sr)); // glide when the root changes (50 ms)
        const float h = 0.35355339f; // 1/sqrt(8)

        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i], dryR = R[i];
            float in = 0.5f * (dryL + dryR);
            for (int a = 0; a < 2; ++a) // input diffusion: two short allpasses
                in = allpass (0, a, in, a == 0 ? 0.0031f : 0.0047f);

            float out[kLines];
            float tot = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                auto& l = line[(size_t) k];
                const float target = juce::jlimit (4.0f, (float) l.buf.size() - 4.0f, (float) sr / (root * ratio[k]));
                l.len += slew * (target - l.len);
                float rp = (float) l.pos - l.len;
                const int sz = (int) l.buf.size();
                if (rp < 0.0f) rp += (float) sz;
                const int i0 = (int) rp, i1 = (i0 + 1) % sz;
                const float fr = rp - (float) i0;
                out[k] = l.buf[(size_t) i0] * (1.0f - fr) + l.buf[(size_t) i1] * fr;
                tot += out[k];
            }
            // shimmer: octave up from the mean of the lines
            const float sh = shifter (tot * 0.125f);
            // self-limiting: the more energy the network holds, the less the octave path feeds in (it can not run away)
            const float m = tot * 0.125f;
            env += 0.002f * (m * m - env);
            const float inj = std::tanh (sh) * shim / (1.0f + env / 0.004f);

            // feedback: per-line gain for the chosen T60, damping, Hadamard mixing
            float fb[kLines];
            for (int k = 0; k < kLines; ++k)
            {
                auto& l = line[(size_t) k];
                const float g = std::pow (10.0f, -3.0f * l.len / ((float) sr * t60));
                l.lp += 0.65f * (out[k] - l.lp); // damping
                fb[k] = g * l.lp;
            }
            hadamard (fb);
            for (int k = 0; k < kLines; ++k)
            {
                auto& l = line[(size_t) k];
                l.buf[(size_t) l.pos] = fb[k] * h + in * 0.35f + inj * 0.01f;
                l.pos = (l.pos + 1) % (int) l.buf.size();
            }
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < kLines; ++k) { if (k & 1) wr += out[k]; else wl += out[k]; }
            wl *= 0.5f; wr *= 0.5f;
            L[i] = dryL * (1.0f - 0.5f * mix) + wl * mix * 2.5f;
            if (chans > 1) R[i] = dryR * (1.0f - 0.5f * mix) + wr * mix * 2.5f;
        }
    }

private:
    static constexpr int kLines = 8;
    static constexpr int kShiftLen = 4096; // window of the pitch shifter (samples)
    struct Line { std::vector<float> buf; int pos = 0; float lp = 0.0f; float len = 100.0f; };

    static void hadamard (float* x)
    {
        for (int len = 1; len < kLines; len <<= 1)
            for (int i = 0; i < kLines; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const float a = x[j], b = x[j + len];
                    x[j] = a + b; x[j + len] = a - b;
                }
    }

    float allpass (int c, int a, float x, float seconds)
    {
        auto& buf = ap[c][a];
        const int d = juce::jmin ((int) buf.size() - 1, (int) (seconds * (float) sr));
        const int sz = (int) buf.size();
        int& p = apPos[c][a];
        const float z = buf[(size_t) ((p - d + sz) % sz)];
        const float v = x - 0.6f * z;
        buf[(size_t) p] = v;
        p = (p + 1) % sz;
        return z + 0.6f * v;
    }

    // octave up: two read taps, delay shrinking by one sample per sample, Hann-type crossfade
    float shifter (float x)
    {
        shift[(size_t) shPos] = x;
        const float W = (float) kShiftLen;
        float y = 0.0f;
        for (int t = 0; t < 2; ++t)
        {
            float ph = shPhase + 0.5f * (float) t;
            ph -= std::floor (ph);
            float rp = (float) shPos - ph * W;
            const int sz = (int) shift.size();
            while (rp < 0.0f) rp += (float) sz;
            const int i0 = (int) rp, i1 = (i0 + 1) % sz;
            const float fr = rp - (float) i0;
            const float v = shift[(size_t) i0] * (1.0f - fr) + shift[(size_t) i1] * fr;
            const float w = std::sin (juce::MathConstants<float>::pi * ph);
            y += v * w * w;
        }
        shPhase -= 1.0f / W;
        shPhase -= std::floor (shPhase);
        shPos = (shPos + 1) % (int) shift.size();
        return y;
    }

    double sr = 44100.0;
    std::array<Line, kLines> line;
    std::vector<float> ap[2][2];
    int apPos[2][2] {};
    std::vector<float> shift;
    int shPos = 0;
    float shPhase = 0.0f, smoothLen = -1.0f, lastMix = 0.0f, env = 0.0f;
};
