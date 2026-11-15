#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

// Key-tuned resonator ("sympathetic strings"): one feedback comb filter ("string") for every key that is held.
// Each string is tuned to the pitch of its key (plus Tune), and is fed by the sound of the slots.
// Only the frequencies at the string's pitch and its harmonics ring on, so the sound takes on the pitch of the key.
// Written for Nektar from the classic idea of a comb filter with a damped feedback loop (Karplus & Strong 1983).
class Resonator
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& c : strings)
        {
            for (auto& b : c.buf) b.assign (kLen, 0.0f);
            c.reset();
        }
    }

    // tuneSemis: shift of the strings from the key; decaySec: time the ring takes to fall by 60 dB;
    // material 0 = soft / dull (the highs die quickly) ... 1 = hard / bright (metal); mix 0..1 (0 = off).
    void process (juce::AudioBuffer<float>& audio, const juce::MidiBuffer& midi, float tuneSemis, float decaySec, float material, float mix)
    {
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn()) noteOn (m.getNoteNumber());
            else if (m.isNoteOff()) noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& c : strings) c.held = false;
        }
        const int n = audio.getNumSamples();
        const int chs = juce::jmin (2, audio.getNumChannels());
        if (mix < 0.005f)
        {
            // off: the strings are let go (nothing rings on when the knob is turned down)
            for (auto& c : strings) c.active = false;
            return;
        }

        const float a = 0.75f * std::pow (1.0f - juce::jlimit (0.0f, 1.0f, material), 1.5f); // loop low-pass: damping
        const float rt = juce::jmax (0.05f, decaySec);
        for (auto& c : strings)
        {
            if (! c.active) continue;
            const double f = juce::jlimit (25.0, 4000.0, 440.0 * std::pow (2.0, ((double) c.note + (double) tuneSemis - 69.0) / 12.0));
            const double loopDelayOfFilter = (double) a / (1.0 - (double) a); // delay of the one-pole low-pass at low frequencies
            c.D = (float) juce::jmax (4.0, sr / f - loopDelayOfFilter);
            c.g = std::pow (10.0f, -3.0f * (float) (sr / f) / ((float) sr * rt)); // 60 dB down after rt seconds
            c.damp = a;
        }

        for (int i = 0; i < n; ++i)
        {
            float wet[2] = { 0.0f, 0.0f };
            for (auto& c : strings)
            {
                if (! c.active) continue;
                c.gate += ((c.held ? 1.0f : 0.0f) - c.gate) * 0.01f; // the string stops being fed softly after the key is let go
                const int r0 = (c.w - (int) c.D) & kMask;
                const int r1 = (r0 - 1) & kMask;
                const float fr = c.D - std::floor (c.D);
                for (int ch = 0; ch < chs; ++ch)
                {
                    const float in = audio.getReadPointer (ch)[i];
                    const float d = c.buf[ch][(size_t) r0] * (1.0f - fr) + c.buf[ch][(size_t) r1] * fr;
                    c.lp[ch] = (1.0f - c.damp) * d + c.damp * c.lp[ch];
                    const float y = in * c.gate + c.g * c.lp[ch];
                    c.buf[ch][(size_t) c.w] = y;
                    wet[ch] += y * (1.0f - c.g); // peak gain 1 at the string's pitch
                }
                c.w = (c.w + 1) & kMask;
                if (! c.held && ++c.releasedFor > (juce::int64) (sr * (double) rt * 1.5)) // rang long enough: free the string
                    c.active = false;
            }
            for (int ch = 0; ch < chs; ++ch)
                audio.getWritePointer (ch)[i] += wet[ch] * mix * 2.0f; // x2: a string only passes narrow lines of the sound, so it needs a little make-up gain
        }
    }

private:
    static constexpr int kLen = 8192, kMask = kLen - 1, kStrings = 16;
    struct String
    {
        bool active = false, held = false;
        int note = 0, w = 0;
        float D = 100.0f, g = 0.9f, damp = 0.0f, gate = 0.0f;
        float lp[2] = { 0.0f, 0.0f };
        juce::int64 releasedFor = 0, startedAt = 0;
        std::vector<float> buf[2];
        void reset()
        {
            active = held = false; w = 0; gate = 0.0f; releasedFor = 0;
            lp[0] = lp[1] = 0.0f;
            for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        }
    };

    void noteOn (int note)
    {
        String* pick = nullptr;
        for (auto& c : strings) if (c.active && c.note == note) { pick = &c; break; }
        if (pick == nullptr) for (auto& c : strings) if (! c.active) { pick = &c; break; }
        if (pick == nullptr)
        {
            pick = &strings[0];
            for (auto& c : strings) if (c.startedAt < pick->startedAt) pick = &c;
        }
        if (! (pick->active && pick->note == note))
            pick->reset();
        pick->active = true; pick->held = true; pick->gate = 1.0f; pick->note = note; pick->releasedFor = 0; pick->startedAt = ++counter;
    }
    void noteOff (int note) { for (auto& c : strings) if (c.active && c.note == note) { c.held = false; c.releasedFor = 0; } }

    std::array<String, kStrings> strings;
    double sr = 44100.0;
    juce::int64 counter = 0;
};
