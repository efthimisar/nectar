#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// Filter with four characters (the Type knob is split in four zones):
//   0 Ladder  - juce::dsp::LadderFilter, 24 dB/oct low-pass, Moog-style (JUCE's implementation); the position inside the zone sets the drive
//   1 SVF     - juce::dsp::StateVariableTPTFilter; the position inside the zone morphs low-pass -> band-pass -> high-pass
//   2 Comb    - feedback comb; Cutoff = pitch of the comb (30 Hz .. 4 kHz), Reso = feedback
//   3 Formant - three parallel band-pass filters on vowel formants; Cutoff sweeps the vowels i - e - a - o - u, Reso = sharpness
// Formant values: average F1/F2/F3 of adult male American English vowels, Peterson & Barney (1952), J. Acoust. Soc. Am. 24, 175-184
// (i 270/2290/3010, e 530/1840/2480, a 730/1090/2440, o 570/840/2410, u 300/870/2240 Hz).
// The relative formant levels (1 / 0.5 / 0.25) are our own choice, not from the paper.
class CharacterFilter
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 1 };
        for (int c = 0; c < 2; ++c)
        {
            ladder[c].prepare (spec);
            ladder[c].setMode (juce::dsp::LadderFilterMode::LPF24);
            for (auto& f : fm[c]) { f.prepare (spec); f.setType (juce::dsp::StateVariableTPTFilterType::bandpass); }
            comb[c].assign ((size_t) (sampleRate / 25.0) + 4, 0.0f);
            combPos[c] = 0;
        }
        wasActive = false;
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            ladder[c].reset(); svf2[c] = Svf2 { sr, 0.0f, 0.0f };
            for (auto& f : fm[c]) f.reset();
            lastT[c] = -1.0f;
            std::fill (comb[c].begin(), comb[c].end(), 0.0f);
        }
    }

    // type, cutoff, reso, mix: all 0..1
    void process (juce::AudioBuffer<float>& b, int n, float type, float cutoff, float reso, float mix)
    {
        if (mix <= 0.0005f)
        {
            if (wasActive) { reset(); wasActive = false; }
            return;
        }
        wasActive = true;
        const int zone = juce::jlimit (0, 3, (int) (type * 3.999f));
        const float frac = juce::jlimit (0.0f, 1.0f, type * 4.0f - (float) zone);
        const float hz = 30.0f * std::pow (18000.0f / 30.0f, cutoff);
        const float fcl = juce::jmin (hz, 0.45f * (float) sr);
        const int chans = juce::jmin (2, b.getNumChannels());

        for (int c = 0; c < chans; ++c)
        {
            float* d = b.getWritePointer (c);
            scratch.resize ((size_t) n);
            std::copy (d, d + n, scratch.begin());
            float* w = scratch.data();
            if (zone == 0)
            {
                ladder[c].setCutoffFrequencyHz (fcl);
                ladder[c].setResonance (reso * 0.9f);
                ladder[c].setDrive (1.0f + frac * 3.0f);
                float* ptr[1] = { w };
                juce::dsp::AudioBlock<float> blk (ptr, 1, (size_t) n);
                juce::dsp::ProcessContextReplacing<float> ctx (blk);
                ladder[c].process (ctx);
            }
            else if (zone == 1)
            {
                // low-pass -> band-pass -> high-pass morph (TPT state-variable filter, Zavalishin)
                const float q = 0.707f + reso * 11.0f;
                const float t1 = frac * 2.0f; // 0..2
                const float t0 = lastT[c] < 0.0f ? t1 : lastT[c];
                lastT[c] = t1;
                for (int i = 0; i < n; ++i)
                {   // (the morph glides through the block: a jump of it once per block is a step in the waveform)
                    const float t = t0 + (t1 - t0) * (float) (i + 1) / (float) n;
                    float l, bq, h;
                    svf2[c].tick (d[i], fcl, q, l, bq, h);
                    w[i] = t < 1.0f ? l * (1.0f - t) + bq * t : bq * (2.0f - t) + h * (t - 1.0f);
                }
            }
            else if (zone == 2)
            {
                auto& buf = comb[c];
                const int size = (int) buf.size();
                const float delay = juce::jlimit (2.0f, (float) size - 3.0f, (float) sr / juce::jlimit (30.0f, 4000.0f, hz));
                const float fb = reso * 0.95f;
                int pos = combPos[c];
                for (int i = 0; i < n; ++i)
                {
                    float rp = (float) pos - delay;
                    if (rp < 0.0f) rp += (float) size;
                    const int i0 = (int) rp; const int i1 = (i0 + 1) % size; const float fr = rp - (float) i0;
                    const float y = buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
                    const float out = d[i] + fb * y;
                    buf[(size_t) pos] = out;
                    w[i] = out * (1.0f - 0.5f * fb);
                    pos = (pos + 1) % size;
                }
                combPos[c] = pos;
            }
            else
            {
                static const float F[5][3] = { { 270, 2290, 3010 }, { 530, 1840, 2480 }, { 730, 1090, 2440 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
                const float pos = cutoff * 4.0f;
                const int v = juce::jmin (3, (int) pos);
                const float t = pos - (float) v;
                static const float lvl[3] = { 1.0f, 0.5f, 0.25f };
                for (int i = 0; i < n; ++i) w[i] = 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    const float f = F[v][k] * std::pow (F[v + 1][k] / F[v][k], t);
                    fm[c][(size_t) k].setCutoffFrequency (juce::jmin (f, 0.45f * (float) sr));
                    fm[c][(size_t) k].setResonance (2.0f + reso * 18.0f);
                    for (int i = 0; i < n; ++i)
                        w[i] += lvl[k] * fm[c][(size_t) k].processSample (0, d[i]);
                }
                const float g = 1.0f + 0.5f * reso * 3.0f;
                for (int i = 0; i < n; ++i) w[i] *= g;
            }
            const float m = mix;
            for (int i = 0; i < n; ++i)
                d[i] = d[i] * (1.0f - m) + w[i] * m;
        }
    }

private:
    // simple state-variable filter (Chamberlin-free TPT form, Zavalishin) giving all three outputs at once
    struct Svf2
    {
        double sr = 44100.0; float s1 = 0, s2 = 0;
        void tick (float x, float fc, float q, float& lp, float& bp, float& hp)
        {
            const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
            const float k = 1.0f / q;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            const float v3 = x - s2;
            const float v1 = a1 * s1 + a2 * v3;
            const float v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
    };

    double sr = 44100.0;
    bool wasActive = false;
    juce::dsp::LadderFilter<float> ladder[2];
    Svf2 svf2[2];
    float lastT[2] = { -1.0f, -1.0f };
    std::array<juce::dsp::StateVariableTPTFilter<float>, 3> fm[2];
    std::vector<float> comb[2];
    int combPos[2] { 0, 0 };
    std::vector<float> scratch;
};
