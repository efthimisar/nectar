#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// Granular voice for one key (v2). Short pieces ("grains") of a chosen part of the sound are played
// one over the other, each with a smooth fade in and out (Hann window; Gabor 1947, Roads 2001).
//   melodic:  a steady stream of grains at the pitch of the key
//   rhythmic: one grain per step of a 16-step pattern, in time with the tempo; each step reads its own place in the part
class GrainVoice
{
public:
    struct Params
    {
        float start = 0.0f, end = 1.0f; // chosen part of the sound (0..1 of its length)
        float position = 0.0f;          // where in the part the grains are taken from (0..1)
        float scan = 0.0f;              // -1..1: how fast the position moves by itself (1 = normal speed of the sound)
        float spray = 0.1f;             // 0..1 random spread of the position around it
        float sizeMs = 120.0f;          // grain length
        float density = 4.0f;           // how many grains sound at the same time (melodic)
        float jitter = 0.0f;            // 0..1 random pitch (up to +-1 semitone) and timing
        float reverse = 0.0f;           // 0..1 chance that a grain plays backwards
        float spread = 0.5f;            // 0..1 stereo spread of the grains
        bool rhythmic = false;
        double bpm = 120.0;
        juce::uint32 pattern = 0xffffu; // 16 steps, bit 0 = first step
        float gate = 0.8f;              // rhythmic: grain length as part of one step
        float pitchSemis = 0.0f;
        float releaseSec = 0.25f;
        float gain = 1.0f;
    };

    void prepare (double sr) { rate = sr; reset(); }
    void reset() { for (auto& g : grains) g.on = false; untilNext = 0.0; scanPos = 0.0; step = 0; }
    void noteOn (int midiNote, float vel) { note = midiNote; velocity = vel; held = true; env = juce::jmax (env, 0.0f); reset(); }
    void noteOff() { held = false; }
    int getNote() const { return note; }
    bool isHeld() const { return held; }
    bool active() const { if (held || env > 1.0e-4f) return true; for (auto& g : grains) if (g.on) return true; return false; }

    // adds to out
    void process (juce::AudioBuffer<float>& out, int startSample, int num, const juce::AudioBuffer<float>& src, const Params& p)
    {
        const int n = src.getNumSamples();
        if (n < 64) return;
        const int chs = src.getNumChannels();
        const float a = juce::jlimit (0.0f, 0.98f, juce::jmin (p.start, p.end)), b = juce::jlimit (a + 0.01f, 1.0f, juce::jmax (p.start, p.end));
        const double regA = a * (double) n, regLen = (b - a) * (double) n;
        const double ratio = std::pow (2.0, ((double) (note - 60) + p.pitchSemis) / 12.0);
        const double stepLen = rate * 60.0 / juce::jmax (20.0, p.bpm) / 4.0; // one sixteenth
        const double sizeS = p.rhythmic ? stepLen * juce::jlimit (0.05f, 4.0f, p.gate)
                                        : rate * 0.001 * juce::jlimit (5.0f, 2000.0f, p.sizeMs);
        const double interval = p.rhythmic ? stepLen : sizeS / juce::jlimit (0.5f, 32.0f, p.density);
        const float norm = p.rhythmic ? 1.0f : 1.0f / std::sqrt (juce::jmax (1.0f, p.density * 0.5f));
        auto* L = out.getWritePointer (0);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : L;

        const float relC = 1.0f - std::exp (-1.0f / (juce::jmax (0.01f, p.releaseSec) * (float) rate));
        for (int i = 0; i < num; ++i)
        {
            env += ((held ? 1.0f : 0.0f) - env) * (held ? 0.002f : relC);
            if (held && untilNext <= 0.0)
            {
                const bool fire = ! p.rhythmic || ((p.pattern >> (step & 15)) & 1u) != 0;
                if (fire)
                    for (auto& g : grains)
                        if (! g.on)
                        {
                            double pos = p.rhythmic ? (double) (step & 15) / 16.0 + p.position : p.position + scanPos;
                            pos += (rnd.nextDouble() - 0.5) * p.spray;
                            pos -= std::floor (pos);
                            g.rev = rnd.nextFloat() < p.reverse;
                            g.inc = ratio * std::pow (2.0, (rnd.nextDouble() - 0.5) * 2.0 * p.jitter / 12.0) * (g.rev ? -1.0 : 1.0);
                            g.len = sizeS;
                            g.t = 0.0;
                            g.pos = regA + pos * regLen + (g.rev ? sizeS * ratio : 0.0);
                            const float pan = 0.5f + (rnd.nextFloat() - 0.5f) * p.spread;
                            g.gl = std::cos (pan * juce::MathConstants<float>::halfPi) * 1.4142f;
                            g.gr = std::sin (pan * juce::MathConstants<float>::halfPi) * 1.4142f;
                            g.on = true;
                            break;
                        }
                untilNext += interval * (p.rhythmic ? 1.0 : 1.0 + (rnd.nextDouble() - 0.5) * p.jitter);
                ++step;
            }
            untilNext -= 1.0;
            scanPos += p.scan / juce::jmax (1.0, regLen);
            scanPos -= std::floor (scanPos);

            float l = 0.0f, r = 0.0f;
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const double ph = g.t / g.len;
                if (ph >= 1.0) { g.on = false; continue; }
                // Hann window; rhythmic grains use short edges so the hit stays sharp
                float w;
                if (p.rhythmic) { const double e = 0.08; w = (float) (ph < e ? ph / e : (ph > 1.0 - 3 * e ? (1.0 - ph) / (3 * e) : 1.0)); w = w * w * (3.0f - 2.0f * w); }
                else w = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * ph);
                double q = g.pos;
                if (q < regA || q >= regA + regLen) { q = regA + std::fmod (q - regA, regLen); if (q < regA) q += regLen; }
                const int i0 = juce::jlimit (0, n - 2, (int) q);
                const float fr = (float) (q - (double) i0);
                const float sl = src.getSample (0, i0) * (1.0f - fr) + src.getSample (0, i0 + 1) * fr;
                const float sr2 = chs > 1 ? src.getSample (1, i0) * (1.0f - fr) + src.getSample (1, i0 + 1) * fr : sl;
                l += sl * w * g.gl;
                r += sr2 * w * g.gr;
                g.pos += g.inc;
                g.t += 1.0;
            }
            const float gn = env * velocity * norm * p.gain;
            L[startSample + i] += l * gn;
            if (R != L) R[startSample + i] += r * gn;
        }
    }

private:
    struct Grain { bool on = false, rev = false; double pos = 0, inc = 1, len = 1, t = 0; float gl = 1, gr = 1; };
    std::array<Grain, 48> grains;
    juce::Random rnd { 1234 };
    double rate = 48000.0, untilNext = 0.0, scanPos = 0.0;
    int note = 60, step = 0;
    float velocity = 1.0f, env = 0.0f;
    bool held = false;
};

// Polyphonic granular player for one slot.
class GrainPlayer
{
public:
    void prepare (double sr) { for (auto& v : voices) v.prepare (sr); }
    bool active() const { for (auto& v : voices) if (v.active()) return true; return false; }
    void allOff() { for (auto& v : voices) v.noteOff(); }

    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const juce::AudioBuffer<float>& src, const GrainVoice::Params& p)
    {
        const int n = out.getNumSamples();
        int pos = 0;
        auto render = [&] (int upTo)
        {
            if (upTo > pos)
                for (auto& v : voices)
                    if (v.active()) v.process (out, pos, upTo - pos, src, p);
            pos = juce::jmax (pos, upTo);
        };
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            render (juce::jlimit (0, n, meta.samplePosition));
            if (m.isNoteOn())
            {
                GrainVoice* use = nullptr;
                for (auto& v : voices) if (! v.active()) { use = &v; break; }
                if (use == nullptr) for (auto& v : voices) if (! v.isHeld()) { use = &v; break; }
                if (use == nullptr) { use = &voices[(size_t) next]; next = (next + 1) % (int) voices.size(); }
                use->noteOn (m.getNoteNumber(), m.getFloatVelocity());
            }
            else if (m.isNoteOff())
            {
                for (auto& v : voices) if (v.isHeld() && v.getNote() == m.getNoteNumber()) v.noteOff();
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                allOff();
        }
        render (n);
    }

private:
    std::array<GrainVoice, 10> voices;
    int next = 0;
};
