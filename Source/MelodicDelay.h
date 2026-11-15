#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// Ping-pong delay whose repeats change pitch: every pass through the loop is shifted by the same number of semitones
// (the Pitch knob, -12 .. +12, 0 = unshifted), so a repeat rises or falls like a melody. Each pass is also darker (low-pass in the loop).
// Time is tempo-synced (1/16 .. 1 bar in 8 steps). The pitch shifter (two crossfaded read taps) is our own simple implementation.
class MelodicDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            line[c].assign ((size_t) (sr * 4.2) + 8, 0.0f); pos[c] = 0;
            sh[c].assign ((size_t) kWin * 2, 0.0f); shPos[c] = 0; shPhase[c] = 0.0f;
            lp[c] = 0.0f;
        }
        len = -1.0f;
        lastMix = 0.0f;
    }

    // time/feedback/pitch/mix: 0..1
    void process (juce::AudioBuffer<float>& b, int n, float time, float feedback, float pitch, float mix, double bpm)
    {
        if (mix <= 0.0005f && lastMix <= 0.0005f) { lastMix = mix; return; }
        lastMix = mix;
        static const float beats[8] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
        const float target = juce::jlimit (16.0f, (float) line[0].size() - 8.0f, (float) (beats[juce::jlimit (0, 7, (int) (time * 7.999f))] * 60.0 / bpm * sr));
        if (len < 0.0f) len = target;
        const int semis = juce::jlimit (-12, 12, (int) std::lround ((pitch - 0.5f) * 24.0f));
        const float ratio = std::pow (2.0f, (float) semis / 12.0f);
        const float fb = feedback * 0.92f;
        const float slew = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
        const int chans = juce::jmin (2, b.getNumChannels());
        float* ch[2] = { b.getWritePointer (0), chans > 1 ? b.getWritePointer (1) : b.getWritePointer (0) };

        for (int i = 0; i < n; ++i)
        {
            len += slew * (target - len);
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                const int sz = (int) line[c].size();
                float rp = (float) pos[c] - len;
                if (rp < 0.0f) rp += (float) sz;
                const int i0 = (int) rp, i1 = (i0 + 1) % sz;
                const float fr = rp - (float) i0;
                out[c] = line[c][(size_t) i0] * (1.0f - fr) + line[c][(size_t) i1] * fr;
            }
            const float mono = 0.5f * (ch[0][i] + ch[1][i]);
            // ping-pong: the repeat of one side feeds the other side (shifted, darker, softly limited)
            float fed[2];
            for (int c = 0; c < 2; ++c)
            {
                float x = out[1 - c];
                if (semis != 0) x = shifter (1 - c, x, ratio);
                lp[c] += 0.55f * (x - lp[c]);
                fed[c] = std::tanh (lp[c] * fb);
            }
            line[0][(size_t) pos[0]] = mono + fed[0];
            line[1][(size_t) pos[1]] = fed[1];
            pos[0] = (pos[0] + 1) % (int) line[0].size();
            pos[1] = (pos[1] + 1) % (int) line[1].size();
            ch[0][i] += out[0] * mix;
            if (chans > 1) ch[1][i] += out[1] * mix;
        }
    }

private:
    static constexpr int kWin = 2048;

    float shifter (int c, float x, float ratio)
    {
        auto& buf = sh[c];
        const int sz = (int) buf.size();
        buf[(size_t) shPos[c]] = x;
        float y = 0.0f;
        for (int t = 0; t < 2; ++t)
        {
            float ph = shPhase[c] + 0.5f * (float) t;
            ph -= std::floor (ph);
            float rp = (float) shPos[c] - ph * (float) kWin;
            while (rp < 0.0f) rp += (float) sz;
            const int i0 = (int) rp, i1 = (i0 + 1) % sz;
            const float fr = rp - (float) i0;
            const float v = buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
            const float w = std::sin (juce::MathConstants<float>::pi * ph);
            y += v * w * w;
        }
        shPhase[c] -= (ratio - 1.0f) / (float) kWin;
        shPhase[c] -= std::floor (shPhase[c]);
        shPos[c] = (shPos[c] + 1) % sz;
        return y;
    }

    double sr = 44100.0;
    std::vector<float> line[2], sh[2];
    int pos[2] {}, shPos[2] {};
    float shPhase[2] {}, lp[2] {}, len = -1.0f, lastMix = 0.0f;
};
