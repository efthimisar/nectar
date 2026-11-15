#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include "Spatializer.h"

// Mixes the four slots: pan (balance) per slot and "Intermorph" per pair of slots.
// Intermorph: the low band of the first slot of a pair (1 or 3) plus the high band of the second (2 or 4), split by a
// Linkwitz-Riley crossover (4th order, LP + HP sum is flat in level). Amount 0 = off; above 0 it is the crossover frequency.
class SlotMixer
{
public:
    static constexpr int kSlots = 4;

    void prepare (double sampleRate, int maxBlock)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, maxBlock), 2 };
        for (auto& pr : pairs)
        {
            pr.lp.prepare (spec);
            pr.hp.prepare (spec);
            pr.lp.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            pr.hp.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
            pr.active = false;
        }
        for (auto& b : scratch)
            b.setSize (2, juce::jmax (1, maxBlock));
        for (auto& sp : spat) sp.prepare (sampleRate);
        spatActive = {};
        mono.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
        binL = mono; binR = mono;
        lastGain = {};
        for (auto& g : lastGain) g = { 1.0f, 1.0f };
    }

    // crossover frequency for the Intermorph knob (0..1, 0 = off)
    static float crossoverHz (float amount) { return 100.0f * std::pow (80.0f, amount); }

    // gains of a pan position -1 (left) .. 1 (right): equal power, 1.0 on both sides in the middle
    static std::array<float, 2> panGains (float pan)
    {
        const float th = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        return { std::sqrt (2.0f) * std::cos (th), std::sqrt (2.0f) * std::sin (th) };
    }

    // The scratch buffer a slot should be rendered into (cleared here).
    // Returns a view of exactly numSamples samples (the buffer itself may be longer).
    juce::AudioBuffer<float> scratchFor (int slot, int numSamples)
    {
        auto& b = scratch[(size_t) slot];
        if (b.getNumSamples() < numSamples)
            b.setSize (2, numSamples, false, false, true); // only if the host block is larger than announced
        b.clear();
        return juce::AudioBuffer<float> (b.getArrayOfWritePointers(), 2, numSamples);
    }

    // Placement of the four slots around the listener (see Spatializer). mix 0 = plain pan, 1 = fully placed.
    struct Space
    {
        float mix = 0.0f, far = 0.5f, back = 0.7f;
        bool speakers = false;
        std::array<float, kSlots> azDeg { -60.0f, -20.0f, 20.0f, 60.0f };
        std::array<float, kSlots> dist { 0.3f, 0.3f, 0.3f, 0.3f };
    };

    // without placement (plain pan only)
    void mix (juce::AudioBuffer<float>& out, int numSamples, const std::array<bool, kSlots>& loaded,
              const std::array<float, kSlots>& pan, const std::array<float, 2>& imorph)
    {
        mix (out, numSamples, loaded, pan, imorph, Space());
    }

    // Adds the slots to `out`. loaded[s]: the slot was rendered into its scratch buffer. imorph[pair]: 0 = off.
    void mix (juce::AudioBuffer<float>& out, int numSamples, const std::array<bool, kSlots>& loaded,
              const std::array<float, kSlots>& pan, const std::array<float, 2>& imorph,
              const Space& space)
    {
        for (int s = 0; s < kSlots; ++s)
        {
            if (! loaded[(size_t) s]) continue;
            auto& b = scratch[(size_t) s];
            const bool placed = space.mix > 0.001f;
            if (placed)
            {
                if (mono.size() < (size_t) numSamples)
                {
                    mono.resize ((size_t) numSamples);
                    binL.resize ((size_t) numSamples);
                    binR.resize ((size_t) numSamples);
                }
                const float* l = b.getReadPointer (0);
                const float* r = b.getReadPointer (1);
                for (int i = 0; i < numSamples; ++i) mono[(size_t) i] = 0.5f * (l[i] + r[i]);
                Spatializer::Params sp;
                sp.azDeg = space.azDeg[(size_t) s];
                sp.dist = space.dist[(size_t) s];
                sp.far = space.far;
                sp.back = space.back;
                sp.speakers = space.speakers;
                spat[(size_t) s].process (mono.data(), binL.data(), binR.data(), numSamples, sp);
                spatActive[(size_t) s] = true;
            }
            else if (spatActive[(size_t) s])
            {
                spat[(size_t) s].reset();
                spatActive[(size_t) s] = false;
            }
            const auto g = panGains (pan[(size_t) s]);
            for (int c = 0; c < 2; ++c)
            {
                b.applyGainRamp (c, 0, numSamples, lastGain[(size_t) s][(size_t) c], g[(size_t) c]);
                lastGain[(size_t) s][(size_t) c] = g[(size_t) c];
            }
            if (placed)
            {
                float* l = b.getWritePointer (0);
                float* r = b.getWritePointer (1);
                for (int i = 0; i < numSamples; ++i)
                {
                    l[i] += space.mix * (binL[(size_t) i] - l[i]);
                    r[i] += space.mix * (binR[(size_t) i] - r[i]);
                }
            }
        }
        for (int pr = 0; pr < 2; ++pr)
        {
            const int a = 2 * pr, b = a + 1;
            const bool both = loaded[(size_t) a] && loaded[(size_t) b] && imorph[(size_t) pr] > 0.001f;
            if (both)
            {
                auto& P = pairs[(size_t) pr];
                if (! P.active) { P.lp.reset(); P.hp.reset(); P.active = true; }
                const float hz = crossoverHz (imorph[(size_t) pr]);
                P.lp.setCutoffFrequency (hz);
                P.hp.setCutoffFrequency (hz);
                juce::dsp::AudioBlock<float> ba (scratch[(size_t) a]), bb (scratch[(size_t) b]);
                auto sa = ba.getSubBlock (0, (size_t) numSamples);
                auto sb = bb.getSubBlock (0, (size_t) numSamples);
                juce::dsp::ProcessContextReplacing<float> ca (sa), cb (sb);
                P.lp.process (ca);
                P.hp.process (cb);
            }
            else
                pairs[(size_t) pr].active = false;
            for (int s : { a, b })
                if (loaded[(size_t) s])
                    for (int c = 0; c < juce::jmin (2, out.getNumChannels()); ++c)
                        out.addFrom (c, 0, scratch[(size_t) s], c, 0, numSamples);
        }
    }

private:
    struct Pair
    {
        juce::dsp::LinkwitzRileyFilter<float> lp, hp;
        bool active = false;
    };
    std::array<Pair, 2> pairs;
    std::array<juce::AudioBuffer<float>, kSlots> scratch;
    std::array<std::array<float, 2>, kSlots> lastGain {};
    std::array<Spatializer, kSlots> spat;
    std::array<bool, kSlots> spatActive {};
    std::vector<float> mono, binL, binR;
};
