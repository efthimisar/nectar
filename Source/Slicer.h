#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Rhythmic gate: a 16-step pattern, tempo-synced, with smoothed edges. The patterns are our own design
// (no published method). The gate value (0..1) is also offered as a modulation source.
class Slicer
{
public:
    static constexpr int kPatterns = 8;
    static constexpr int kSteps = 16;

    void prepare (double sampleRate) { sr = sampleRate; gain = 1.0f; counter = 0.0; }

    // rate 0..1 -> 1, 2, 4, 8 steps per beat; pattern 0..1 -> one of 8; smooth 0..1 -> 0.3..25 ms; depth 0..1
    // ppq < 0 = host gives no position: free running at bpm.
    // Returns the gate value at the start of the block (0..1).
    float process (juce::AudioBuffer<float>& b, int n, float rate, float pattern, float smooth, float depth, double bpm, double ppq)
    {
        const int spb = 1 << juce::jlimit (0, 3, (int) (rate * 3.999f));
        const int pat = juce::jlimit (0, kPatterns - 1, (int) (pattern * 7.999f));
        const double stepsPerSample = (bpm / 60.0 / sr) * spb;
        const double ms = 0.3 + 24.7 * (double) smooth * smooth;
        const float a = (float) (1.0 - std::exp (-1.0 / (0.001 * ms * sr)));
        double pos = ppq >= 0.0 ? ppq * spb : counter;
        const float first = gateAt (pat, pos);
        { const int st = (int) std::floor (std::fmod (pos, (double) kSteps)); lastStep = st < 0 ? st + kSteps : st; }
        if (depth <= 0.0005f && gain > 0.9999f)
        {
            if (ppq < 0.0) counter += stepsPerSample * n;
            return first;
        }
        for (int i = 0; i < n; ++i)
        {
            const float target = 1.0f - depth * (1.0f - gateAt (pat, pos));
            gain += a * (target - gain);
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.getWritePointer (ch)[i] *= gain;
            pos += stepsPerSample;
        }
        if (ppq < 0.0) counter = std::fmod (pos, 1024.0);
        return first;
    }

    int currentStep() const { return lastStep; } // 0..15, at the start of the last block

private:
    static float gateAt (int pat, double pos)
    {
        static const unsigned short masks[kPatterns] = {
            0xFFFF, // all on
            0x5555, // x.x.x.x.
            0x9249, // every third step
            0xB6DB, // xx.xx.xx
            0x5A5A, // x.xx.x.x
            0x7777, // xxx.xxx.
            0x1111, // x...x...
            0x2D33  // syncopated
        };
        const int step = (int) std::floor (std::fmod (pos, (double) kSteps));
        const int s = step < 0 ? step + kSteps : step;
        return (masks[pat] >> s) & 1 ? 1.0f : 0.0f;
    }

    double sr = 44100.0, counter = 0.0;
    int lastStep = 0;
    float gain = 1.0f;
};
