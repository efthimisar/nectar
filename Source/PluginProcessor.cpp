#include "PluginProcessor.h"
#include <thread>
#include "PluginEditor.h"
#include "BodyBake.h"

// Colour settings for the grains at the ends of what LFOs / envelopes do to the colour knobs: 1 setting (nothing moves),
// 2 (one source moves knobs) or 4 (two different sources: every combination of their two ends).
struct ColourCorner { SpectralBody body; float blur = 0.0f, air = 0.5f; };
struct ColourMove { ColourCorner c[4]; int n = 1; int param[2] = { -1, -1 }; float lo[2] = { 0.0f, 0.0f }, hi[2] = { 1.0f, 1.0f }; };

// Cleans one slot in the background: analysis once per sound, then low cut, hum and noise reduction.
class CleanJob : public juce::ThreadPoolJob
{
public:
    CleanJob (NektarProcessor& o, int slotIn, int gen, std::shared_ptr<const Sample> b, SampleCleaner::CleanParams p,
              SampleCleaner::NoiseProfile room, double rate, float pa, float pb, bool gr, SpectralBody bd, float bl, float mm, float mn, float ai, bool rv, std::shared_ptr<const Sample> pt, float pm, bool al, ColourMove cm)
        : juce::ThreadPoolJob ("clean"), owner (o), slot (slotIn), generation (gen), base (std::move (b)), params (p),
          roomCopy (std::move (room)), hostRate (rate), partA (pa), partB (pb), grains (gr), body (bd), blur (bl), modelMix (mm), modelNoise (mn), air (ai), reverse (rv), partner (std::move (pt)), pairMorph (pm), autoLevel (al), mv (cm) {}

    JobStatus runJob() override
    {
        if (generation != owner.latestJob[(size_t) slot].load() || shouldExit())
            return jobHasFinished;
        auto s = std::make_shared<Sample> (*base);
        if (! s->analysis.valid)
        {
            s->analysis = SampleCleaner::analyse (s->data, s->rate);
            s->autoProfile = SampleCleaner::estimateProfile (s->data, s->rate, true);
        }
        const bool sameClean = s->cleaned.getNumSamples() > 0 && s->cleanN == params.noise && s->cleanH == params.hum && s->cleanL == params.lowCut;
        if (params.any() && ! sameClean)
        {
            const auto& prof = roomCopy.valid ? roomCopy : s->autoProfile;
            s->cleaned = SampleCleaner::clean (s->data, s->rate, params, prof, s->analysis.humHz, s->analysis.humHarmonics);
            s->cleanN = params.noise; s->cleanH = params.hum; s->cleanL = params.lowCut;
        }
        else if (params.any()) {}
        else
            s->cleaned.setSize (0, 0);
        if (shouldExit() || generation != owner.latestJob[(size_t) slot].load())
            return jobHasFinished;
        // the sine-wave description of what is played (kept when the sound itself did not change)
        const bool soundSame = (! params.any() ? base->cleaned.getNumSamples() == 0 : sameClean)
                               && std::abs (base->partA - partA) < 1.0e-5f && std::abs (base->partB - partB) < 1.0e-5f && base->reverse == reverse;
        s->partA = partA; s->partB = partB; s->reverse = reverse;
        if (soundSame && base->plainPlay.getNumSamples() > 0 && std::abs (base->plainRate - hostRate) < 0.5)
        {   // only the colour changed: the sound is already converted (and its hits found); start again from the plain copy
            s->play = base->plainPlay;
            s->playRate = hostRate;
        }
        else
        {
            SamplePlayer::makePlayable (*s, hostRate);
            s->plainPlay = s->play;
            s->plainRate = hostRate;
        }
        // the colour baked in for the grains
        auto bakeOne = [this] (Sample& smp, const SpectralBody& body, float blur)
        {
            const bool useMorph = pairMorph > 0.01f && smp.model != nullptr && partner != nullptr && partner->model != nullptr
                                  && std::abs (partner->modelRate - hostRate) < 0.5;
            const bool useModel = ! useMorph && modelMix > 0.01f && smp.model != nullptr;
            if (! grains || ! (body.active() || blur > 0.01f || useModel || useMorph)) { smp.grainSrc.setSize (0, 0); return; }
            if (useMorph)
                smp.grainSrc = bakeMorph (smp.play, *smp.model, *partner->model, pairMorph, smp.transient, smp.hitGate,
                                          partner->transient, partner->hitGate, autoLevel && smp.autoGain > 1.0e-6f ? partner->autoGain / smp.autoGain : 1.0f,
                                          smp.modelGain, partner->modelGain);
            if (useModel) smp.grainSrc = bakeModel (smp.play, *smp.model, modelMix, modelNoise);
            if (body.active() || blur > 0.01f) smp.grainSrc = bakeBody (useModel || useMorph ? smp.grainSrc : smp.play, hostRate, body, blur);
            if (useMorph && ! (body.active() || blur > 0.01f)) return; // the morph already carries its own level
            // same overall loudness as the sound itself, so the colour knobs do not act as volume knobs
            double a = 0.0, b = 0.0;
            for (int c = 0; c < smp.play.getNumChannels(); ++c)
            {
                const float ra = smp.play.getRMSLevel (c, 0, smp.play.getNumSamples());
                const float rb = smp.grainSrc.getRMSLevel (juce::jmin (c, smp.grainSrc.getNumChannels() - 1), 0, smp.grainSrc.getNumSamples());
                a += (double) ra * ra; b += (double) rb * rb;
            }
            if (b > 1.0e-20) smp.grainSrc.applyGain ((float) juce::jlimit (0.25, 4.0, std::sqrt (a / b)));
        };
        auto clearMove = [] (Sample& smp)
        {
            smp.moveParam = smp.moveParam2 = -1;
            smp.grainSrcHi.setSize (0, 0); smp.grainSrcY.setSize (0, 0); smp.grainSrcXY.setSize (0, 0);
        };
        clearMove (*s);
        if (! s->tempoKnown)
        {   // the tempo of the whole file (name first, then the sound), once per sound
            s->tempo = TempoFind::of (s->data, s->rate, s->name);
            s->tempoKnown = true;
        }
        if (! (soundSame && base->model != nullptr && std::abs (base->modelRate - hostRate) < 0.5))
        {
            {   // the sound itself is ready: hand it over now, the sine-wave description follows when it is done
                bakeOne (*s, body, blur);
                auto quick = std::make_shared<Sample> (*s);
                quick->model = nullptr;
                const juce::ScopedLock sl (owner.resultLock);
                owner.results[(size_t) slot] = quick;
            }
            auto model = SineModel::analyse (s->play, hostRate);
            if (model == nullptr || shouldExit() || generation != owner.latestJob[(size_t) slot].load())
                return jobHasFinished;
            s->model = std::move (model);
            s->modelRate = hostRate;
            {   // measured once per sound: how loud the description plays compared with the sound
                const auto r = bakeModel (s->play, *s->model, 1.0f, 1.0f);
                double a = 0.0, b = 0.0;
                for (int c = 0; c < s->play.getNumChannels(); ++c) { const float x = s->play.getRMSLevel (c, 0, s->play.getNumSamples()); a += (double) x * x; }
                for (int c = 0; c < 2; ++c) { const float x = r.getRMSLevel (c, 0, r.getNumSamples()); b += (double) x * x; }
                a /= (double) juce::jmax (1, s->play.getNumChannels()); b /= 2.0;
                s->modelGain = b > 1.0e-20 ? (float) juce::jlimit (0.25, 4.0, std::sqrt (a / b)) : 1.0f;
            }
            s->tonality = Tonality::of (*s->model);
            s->keyKnown = true;
        }
        clearMove (*s);
        if (grains && mv.n > 1)
        {   // something moves colour knobs: the grains get a coloured copy for every end of the movement and blend between them
            const juce::AudioBuffer<float> plain (s->play);
            juce::AudioBuffer<float> copies[4], firstPlay;
            auto one = [&] (int i)
            {   // each corner on its own (they are baked at the same time, one core each)
                Sample t;
                t.play = plain; t.model = s->model; t.modelRate = s->modelRate; t.modelGain = s->modelGain; t.autoGain = s->autoGain;
                t.hitGate = s->hitGate; t.transient = s->transient;
                if (std::abs (mv.c[i].air - 0.5f) >= 0.01f && t.model != nullptr) applyAir (t.play, *t.model, mv.c[i].air);
                bakeOne (t, mv.c[i].body, mv.c[i].blur);
                copies[i] = t.grainSrc.getNumSamples() > 0 ? std::move (t.grainSrc) : t.play;
                if (i == 0) firstPlay = std::move (t.play);
            };
            {
                std::vector<std::thread> others;
                for (int i = 1; i < mv.n; ++i) others.emplace_back (one, i);
                one (0);
                for (auto& th : others) th.join();
            }
            if (shouldExit() || generation != owner.latestJob[(size_t) slot].load())
                return jobHasFinished;
            s->play = std::move (firstPlay);
            s->grainSrc = std::move (copies[0]); // s->play is now the sound with the first setting
            s->grainSrcHi = std::move (copies[1]);
            s->moveParam = mv.param[0]; s->moveLo = mv.lo[0]; s->moveHi = mv.hi[0];
            if (mv.n == 4)
            {
                s->grainSrcY = std::move (copies[2]);
                s->grainSrcXY = std::move (copies[3]);
                s->moveParam2 = mv.param[1]; s->moveLo2 = mv.lo[1]; s->moveHi2 = mv.hi[1];
            }
        }
        else
        {
            if (std::abs (air - 0.5f) >= 0.01f && s->model != nullptr) applyAir (s->play, *s->model, air);
            bakeOne (*s, body, blur);
        }
        const juce::ScopedLock sl (owner.resultLock);
        owner.results[(size_t) slot] = s;
        return jobHasFinished;
    }

private:
    NektarProcessor& owner;
    int slot, generation;
    std::shared_ptr<const Sample> base;
    SampleCleaner::CleanParams params;
    SampleCleaner::NoiseProfile roomCopy;
    double hostRate;
    float partA, partB;
    bool grains;
    SpectralBody body;
    float blur, modelMix, modelNoise, air;
    bool reverse;
    std::shared_ptr<const Sample> partner;
    float pairMorph;
    bool autoLevel;
    ColourMove mv;
};

juce::AudioProcessorValueTreeState::ParameterLayout NektarProcessor::createLayout()
{
    using juce::NormalisableRange;
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // per slot: Level, Pitch, Tail (slot 1 keeps the original ids, so older saved states still work)
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        const juce::String nm = " " + juce::String (slot + 1);
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "level" + sfx, 1 }, "Level" + nm,
                                                             NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "pitch" + sfx, 1 }, "Pitch" + nm,
                                                             NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("st")));
        // grains an octave up (Shimmer), and a seed for the notes of the 16 steps (Arp; 0 = off)
        l.add (std::make_unique<juce::AudioParameterInt> (ParameterID { "groct" + sfx, 1 }, "Grain octave" + nm, -1, 1, 0));
        l.add (std::make_unique<juce::AudioParameterInt> (ParameterID { "grarp" + sfx, 1 }, "Grain notes" + nm, 0, 9999, 0));
        l.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "stempo" + sfx, 1 }, "Tempo of sound" + nm,
                                                              juce::StringArray { "Auto", "No tempo", "Half", "Double" }, 0));
        {   // the key of the sound: 0 = as found by the analysis, 1 = no key (left as it is), 2.. = C major, C minor, C# major ...
            juce::StringArray keys { "Auto", "No key" };
            for (int k = 0; k < 12; ++k) { keys.add (juce::String (Tonality::noteName (k)) + " major"); keys.add (juce::String (Tonality::noteName (k)) + " minor"); }
            l.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "skey" + sfx, 1 }, "Key of sound" + nm, keys, 0));
        }
        {
            static const float defAz[4] = { -60.0f, -20.0f, 20.0f, 60.0f };
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spaz" + sfx, 1 }, "Space direction" + nm,
                                                                 NormalisableRange<float> (-180.0f, 180.0f, 0.1f), defAz[slot],
                                                                 juce::AudioParameterFloatAttributes().withLabel ("deg")));
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spdist" + sfx, 1 }, "Space distance" + nm,
                                                                 NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));
            // movement of the sound around the listener: 0 still, 1 round and round, 2 left - right, 3 front - back
            // 0 still, 1 round the listener, 2 left - right, 3 front - back, 4.. a shape (MotionShapes.h) around the dot's place
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spmove" + sfx, 1 }, "Space movement" + nm,
                                                                 NormalisableRange<float> (0.0f, 5.0f, 1.0f), 0.0f));
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spspeed" + sfx, 1 }, "Space movement speed" + nm,
                                                                 NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
        }
        {   // v2: every sample has its own time / colour controls (sample 1 keeps the old ids) and its own filter
            static const char* const ids[kCol] = { "blur", "bodytone", "bodyfocus", "bodysize", "srcrough" };
            static const char* const names[kCol] = { "Blur", "Body tone", "Body focus", "Body size", "Grit" };
            static const float defs[kCol] = { 0.0f, 0.5f, 0.5f, 0.5f, 0.0f };
            if (slot > 0)
                for (int i = 0; i < kCol; ++i)
                    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { ids[i] + sfx, 1 }, names[i] + nm,
                                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), defs[i]));
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "air" + sfx, 1 }, "Air" + nm,
                                                                 NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "fshift" + sfx, 1 }, "Frequency shift" + nm,
                                                                 NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "ring" + sfx, 1 }, "Ring" + nm,
                                                                 NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
            static const char* const fids[4] = { "sfiltype", "sfilcut", "sfilres", "sfilmix" };
            static const char* const fnames[4] = { "Sample filter type", "Sample filter cutoff", "Sample filter reso", "Sample filter mix" };
            static const float fdefs[4] = { 0.0f, 1.0f, 0.3f, 1.0f };
            for (int i = 0; i < 4; ++i)
                l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { fids[i] + sfx, 1 }, fnames[i] + nm,
                                                                     NormalisableRange<float> (0.0f, 1.0f, 0.001f), fdefs[i]));
        }
        {   // v2 granular (per sample)
            auto f = [&] (const char* id, const char* name, float lo, float hi, float def, float skew = 1.0f)
            {
                l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { id + sfx, 1 }, name + nm,
                                                                     NormalisableRange<float> (lo, hi, 0.0001f, skew), def));
            };
            l.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "grmode" + sfx, 1 }, "Granular" + nm,
                                                                  juce::StringArray { "Off", "Melodic", "Rhythmic" }, 0));
            f ("grstart", "Start", 0.0f, 1.0f, 0.0f);
            f ("grend", "End", 0.0f, 1.0f, 1.0f);
            f ("grpos", "Position", 0.0f, 1.0f, 0.0f);
            f ("grscan", "Scan", -1.0f, 1.0f, 0.15f);
            f ("grspray", "Spray", 0.0f, 1.0f, 0.15f);
            f ("grsize", "Grain size", 10.0f, 1000.0f, 140.0f, 0.4f);
            f ("grdens", "Density", 1.0f, 16.0f, 6.0f, 0.6f);
            f ("grjit", "Jitter", 0.0f, 1.0f, 0.0f);
            f ("grrev", "Reverse", 0.0f, 1.0f, 0.0f);
            f ("grspread", "Grain spread", 0.0f, 1.0f, 0.8f);
            f ("grgate", "Gate", 0.1f, 2.0f, 0.7f);
            l.add (std::make_unique<juce::AudioParameterInt> (ParameterID { "grpat" + sfx, 1 }, "Pattern" + nm, 0, 65535, 0xb6b5));
        }
        // how much Make changes this sound: its rhythm, its colour, its texture (0 = untouched, 1 = drawn anew)
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "mkrhy" + sfx, 1 }, "Make rhythm" + nm, 0.0f, 1.0f, 0.5f));
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "mkcol" + sfx, 1 }, "Make colour" + nm, 0.0f, 1.0f, 0.5f));
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "mktex" + sfx, 1 }, "Make texture" + nm, 0.0f, 1.0f, 0.5f));
    }
    l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "autolevel", 1 }, "Auto level", true));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bright", 1 }, "Bright",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "blur", 1 }, "Blur",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bodytone", 1 }, "Body tone",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bodyfocus", 1 }, "Body focus",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bodysize", 1 }, "Body size",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "spspk", 1 }, "Space: speaker check mode", false));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "imorph12", 1 }, "Intermorph 1+2 (0 = off, then crossover)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "imorph34", 1 }, "Intermorph 3+4 (0 = off, then crossover)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "xmorph12", 1 }, "Crossmorph 1<>2 (centre = both; route Velocity or Envelope here)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "xmorph34", 1 }, "Crossmorph 3<>4 (centre = both; route Velocity or Envelope here)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "srcrough", 1 }, "Source rough",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "restune", 1 }, "Resonator tune",
                                                         NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                         juce::AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "resdecay", 1 }, "Resonator decay",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "resmat", 1 }, "Resonator material",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "resmix", 1 }, "Resonator mix",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "morph12", 1 }, "Morph 1 > 2 (0 = sound 1, 1 = sound 2 at the pitch of 1)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "morph34", 1 }, "Morph 3 > 4 (0 = sound 3, 1 = sound 4 at the pitch of 3)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    // the three big knobs of the main screen (0.5 = as made)
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "mspeed", 1 }, "Motion: slow (0) to fast (1)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "mweight", 1 }, "Weight: thin (0) to heavy (1)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "mlight", 1 }, "Light: dull (0) to bright (1)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    // the joysticks can move by themselves: a shape (0 = none, 1.. = shape) around the place where the stick was left, at one of
    // five speeds (16, 8, 4, 2, 1 s per round, as the Orb of Flow)
    for (const char* id : { "vec1shape", "vec2shape" })
        l.add (std::make_unique<juce::AudioParameterInt> (ParameterID { id, 1 }, juce::String ("Joystick movement ") + (id[3] == '1' ? "1" : "2"), 0, 4, 0));
    for (const char* id : { "vec1speed", "vec2speed" })
        l.add (std::make_unique<juce::AudioParameterInt> (ParameterID { id, 1 }, juce::String ("Joystick movement speed ") + (id[3] == '1' ? "1" : "2"), 0, motionshapes::speedSteps - 1, 1));
    for (const char* id : { "vec1x", "vec1y", "vec2x", "vec2y" })
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { id, 1 }, juce::String ("Pair joystick ") + id,
                                                             NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterInt> (ParameterID { "key", 1 }, "Scale key (0 = C)", 0, 11, 0));
    // Each joystick has its own character (what Make builds) and its own Gentle - Wild
    {
        const juce::StringArray chars { "Cloud", "Rhythm", "Metal", "Air", "Reverse", "Drone", "Swell", "Glitch", "Dust", "Bells", "Shimmer", "Melt", "Arp" };
        for (int pr = 1; pr <= 2; ++pr)
        {
            l.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "character" + juce::String (pr), 1 }, "Character " + juce::String (pr), chars, pr == 1 ? 0 : 1));
            l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "swell" + juce::String (pr), 1 }, "Swell " + juce::String (pr), false));
        }
        // how the two pairs behave towards each other
        l.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "relation", 1 }, "Relation of the pairs",
                                                              juce::StringArray { "None", "Contrast", "Shadow", "Call and response", "Depth" }, 0));
    }
    // Tempo lock: sounds with a tempo are read faster or slower (pitch kept) to follow the host tempo
    l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "tempolock", 1 }, "Lock to tempo", true));
    // Harmony: the four sounds as recorded, all moved into one key, or their keynotes on the notes of one chord
    l.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "harmony", 1 }, "Harmony",
                                                          juce::StringArray { "As is", "One key", "Chord" }, 1));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "master", 1 }, "Master out (dB)",
                                                         NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f));
    return l;
}

juce::AudioProcessor::BusesProperties NektarProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    // Only the standalone app gets a microphone input (plugin hosts often give instruments no audio input).
    if (juce::JUCEApplicationBase::isStandaloneApp())
        b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true);
    return b;
}

NektarProcessor::NektarProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "STATE", createLayout())
{
    fxRack.onChange = [this] { setLatencySamples (fxRack.latency()); };
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        levelP[(size_t) slot] = apvts.getRawParameterValue ("level" + sfx);
        pitchP[(size_t) slot] = apvts.getRawParameterValue ("pitch" + sfx);
        {
            static const char* const ids[kGr] = { "grmode", "grstart", "grend", "grpos", "grscan", "grspray", "grsize", "grdens", "grjit", "grrev", "grspread", "grgate", "grpat" };
            for (int i = 0; i < kGr; ++i)
                grP[(size_t) slot][(size_t) i] = apvts.getRawParameterValue (ids[i] + sfx);
            static const char* const cids[kCol] = { "blur", "bodytone", "bodyfocus", "bodysize", "srcrough" };
            for (int i = 0; i < kCol; ++i)
                colP[(size_t) slot][(size_t) i] = apvts.getRawParameterValue (cids[i] + sfx);
            static const char* const fids[4] = { "sfiltype", "sfilcut", "sfilres", "sfilmix" };
            for (int i = 0; i < 4; ++i)
                sfilP[(size_t) slot][(size_t) i] = apvts.getRawParameterValue (fids[i] + sfx);
            airP[(size_t) slot] = apvts.getRawParameterValue ("air" + sfx);
            fshiftP[(size_t) slot] = apvts.getRawParameterValue ("fshift" + sfx);
            ringP[(size_t) slot] = apvts.getRawParameterValue ("ring" + sfx);
            apvts.addParameterListener ("air" + sfx, this);
        }
        spAzP[(size_t) slot] = apvts.getRawParameterValue ("spaz" + sfx);
        spDistP[(size_t) slot] = apvts.getRawParameterValue ("spdist" + sfx);
        spMoveP[(size_t) slot] = apvts.getRawParameterValue ("spmove" + sfx);
        spSpeedP[(size_t) slot] = apvts.getRawParameterValue ("spspeed" + sfx);
        apvts.addParameterListener ("noise" + sfx, this);
        apvts.addParameterListener ("hum" + sfx, this);
        apvts.addParameterListener ("lowcut" + sfx, this);
        apvts.addParameterListener ("grstart" + sfx, this);
        apvts.addParameterListener ("grend" + sfx, this);
        for (auto* extra : { "grmode", "bodytone", "bodyfocus", "bodysize", "blur", "srcrough", "model", "modelnoise" })
            apvts.addParameterListener (extra + sfx, this);
        cleanDirty[(size_t) slot] = false;
        latestJob[(size_t) slot] = 0;
    }
    startTimer (100);
    brightP = apvts.getRawParameterValue ("bright");
    imorphP[0] = apvts.getRawParameterValue ("imorph12");
    imorphP[1] = apvts.getRawParameterValue ("imorph34");
    xmorphP[0] = apvts.getRawParameterValue ("xmorph12");
    xmorphP[1] = apvts.getRawParameterValue ("xmorph34");
    buildModTable();
    resTuneP = apvts.getRawParameterValue ("restune");
    resDecayP = apvts.getRawParameterValue ("resdecay");
    resMatP = apvts.getRawParameterValue ("resmat");
    resMixP = apvts.getRawParameterValue ("resmix");
    masterP = apvts.getRawParameterValue ("master");
    pairMorphP[0] = apvts.getRawParameterValue ("morph12");
    pairMorphP[1] = apvts.getRawParameterValue ("morph34");
    apvts.addParameterListener ("morph12", this);
    apvts.addParameterListener ("morph34", this);
    for (int sl = 0; sl < kSlots; ++sl)
    {
        const juce::String sx = sl == 0 ? juce::String() : juce::String (sl + 1);
        grOctP[(size_t) sl] = apvts.getRawParameterValue ("groct" + sx);
        grArpP[(size_t) sl] = apvts.getRawParameterValue ("grarp" + sx);
        tapStep[(size_t) sl].store (-1);
    }
    vecShapeP[0] = apvts.getRawParameterValue ("vec1shape"); vecShapeP[1] = apvts.getRawParameterValue ("vec2shape");
    vecSpeedP[0] = apvts.getRawParameterValue ("vec1speed"); vecSpeedP[1] = apvts.getRawParameterValue ("vec2speed");
    for (auto& a : vecPh) for (auto& v : a) v = vecMoveRnd.nextFloat() * juce::MathConstants<float>::twoPi;
    for (auto& a : spPh) for (auto& v : a) v = vecMoveRnd.nextFloat() * juce::MathConstants<float>::twoPi;
    vecXP[0] = apvts.getRawParameterValue ("vec1x"); vecYP[0] = apvts.getRawParameterValue ("vec1y");
    vecXP[1] = apvts.getRawParameterValue ("vec2x"); vecYP[1] = apvts.getRawParameterValue ("vec2y");
    keyP = apvts.getRawParameterValue ("key");
    spSpkP = apvts.getRawParameterValue ("spspk");
    mSpeedP = apvts.getRawParameterValue ("mspeed");
    mWeightP = apvts.getRawParameterValue ("mweight");
    mLightP = apvts.getRawParameterValue ("mlight");
    recordingAllowed = juce::JUCEApplicationBase::isStandaloneApp();
    formats.registerBasicFormats();
}

NektarProcessor::~NektarProcessor()
{
    stopTimer();
    if (judgeThread != nullptr) judgeThread->stopThread (8000);
    judgeThread = nullptr;
    judge = nullptr;
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        apvts.removeParameterListener ("air" + sfx, this);
    }
    cleanPool.removeAllJobs (true, 20000);
}

void NektarProcessor::parameterChanged (const juce::String& id, float)
{
    const juce::juce_wchar last = id.getLastCharacter();
    const bool pairId = id.startsWith ("morph");
    const int slot = pairId ? (id.endsWith ("34") ? 2 : 0) : ((last >= '2' && last <= '4') ? (int) (last - '1') : 0);
    if (pairId && grP[(size_t) slot][0]->load() < 0.5f)
        return; // without grains the morph is made live
    const bool colour = id.startsWith ("body") || id.startsWith ("blur") || id.startsWith ("srcrough");
    if (colour && grP[(size_t) slot][0]->load() < 0.5f)
        return; // without grains the colour is made live
    // choosing a part of the sound is quick, so it only waits a moment; cleaning waits until the knob stops
    cleanChangedAt[(size_t) slot] = juce::Time::getMillisecondCounter() - (id.startsWith ("gr") || id.startsWith ("air") || pairId || colour ? 190u : 0u);
    cleanDirty[(size_t) slot] = true;
}

void NektarProcessor::timerCallback()
{
    if (judgeDone.exchange (false)) finishMake();
    applyHarmony();
    updateArpSteps();
    const auto now = juce::Time::getMillisecondCounter();
    for (int slot = 0; slot < kSlots; ++slot)
        if (cleanDirty[(size_t) slot].load() && now - cleanChangedAt[(size_t) slot].load() >= 250) // wait until the knob stops moving
        {
            cleanDirty[(size_t) slot] = false;
            scheduleClean (slot);
        }

    for (int slot = 0; slot < kSlots; ++slot)
    {
        std::shared_ptr<Sample> r;
        {
            const juce::ScopedLock sl (resultLock);
            r = std::move (results[(size_t) slot]);
            results[(size_t) slot] = nullptr;
        }
        if (r != nullptr)
            installResult (slot, r);
    }
}

void NektarProcessor::scheduleClean (int slot)
{
    auto base = getSample (slot);
    if (base == nullptr)
        return;
    SampleCleaner::CleanParams cp;      // analysis only: noise, hum and low cut stay off (they had no place on the main screen)
    SampleCleaner::NoiseProfile room;
    SpectralBody bd;
    bd.tone = colP[(size_t) slot][kTone]->load(); bd.focus = colP[(size_t) slot][kFocus]->load(); bd.size = colP[(size_t) slot][kSize]->load(); bd.rough = colP[(size_t) slot][kGrit]->load();
    // the colour is baked as it is, or once per corner of the pair joystick
    const float blurBase = colP[(size_t) slot][kBlur]->load(), airBase = airP[(size_t) slot]->load();
    ColourMove mv;
    for (auto& c : mv.c) { c.body = bd; c.blur = blurBase; c.air = airBase; }
    if (grP[(size_t) slot][0]->load() > 0.5f && vecOn[(size_t) (slot / 2)].load())
    {   // the pair joystick: one coloured copy per corner, blended by the stick
        const juce::SpinLock::ScopedLockType lk (vecLock);
        const std::pair<int, const std::atomic<float>*> ks[6] = { { kTone, colP[(size_t) slot][kTone] }, { kFocus, colP[(size_t) slot][kFocus] }, { kSize, colP[(size_t) slot][kSize] },
                                                                 { kBlur, colP[(size_t) slot][kBlur] }, { kGrit, colP[(size_t) slot][kGrit] }, { 100, airP[(size_t) slot] } };
        for (const auto& k : ks)
        {
            const auto it = modIdxByRaw.find (k.second);
            if (it == modIdxByRaw.end()) continue;
            for (const auto& e : vecEntries[(size_t) (slot / 2)])
                if (e.idx == it->second)
                    for (int i = 0; i < 4; ++i)
                        switch (k.first)
                        {
                            case kTone: mv.c[i].body.tone = e.c[i]; break;
                            case kFocus: mv.c[i].body.focus = e.c[i]; break;
                            case kSize: mv.c[i].body.size = e.c[i]; break;
                            case kBlur: mv.c[i].blur = e.c[i]; break;
                            case kGrit: mv.c[i].body.rough = e.c[i]; break;
                            default: mv.c[i].air = e.c[i]; break;
                        }
        }
        mv.n = 4; mv.param[0] = 200; mv.param[1] = 201; mv.lo[0] = mv.lo[1] = 0.0f; mv.hi[0] = mv.hi[1] = 1.0f;
    }
    const int gen = ++latestJob[(size_t) slot];
    cleanPool.addJob (new CleanJob (*this, slot, gen, base, cp, std::move (room), hostRate.load(),
                                    grP[(size_t) slot][1]->load(), grP[(size_t) slot][2]->load(),
                                    grP[(size_t) slot][0]->load() > 0.5f, bd,
                                    blurBase, 0.0f, 0.0f,
                                    airP[(size_t) slot]->load(), false,
                                    slot % 2 == 0 ? getSample (slot + 1) : nullptr, slot % 2 == 0 ? pairMorphP[(size_t) (slot / 2)]->load() : 0.0f,
                                    apvts.getRawParameterValue ("autolevel")->load() > 0.5f, mv), true);
}

void NektarProcessor::adoptSample (int slot, std::shared_ptr<Sample> s)
{
    ++latestJob[(size_t) slot]; // anything still being baked here for this slot is out of date
    serials[(size_t) slot] = s->serial;
    cleanDirty[(size_t) slot] = false;
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    currentSamples[(size_t) slot] = s;
    pendingSamples[(size_t) slot] = s;
    samplePending[(size_t) slot] = true;
}

void NektarProcessor::installResult (int slot, std::shared_ptr<Sample> s)
{
    if (slot % 2 == 1 && pairMorphP[(size_t) (slot / 2)]->load() > 0.01f && grP[(size_t) (slot - 1)][0]->load() > 0.5f && s->model != nullptr
        && (getSample (slot) == nullptr || getSample (slot)->model != s->model))
    {
        cleanChangedAt[(size_t) (slot - 1)] = juce::Time::getMillisecondCounter();
        cleanDirty[(size_t) (slot - 1)] = true; // the partner changed: the baked morph of the first sound is made again
    }
    if (s->serial != serials[(size_t) slot])
        return; // another sound was loaded in the meantime
    if (std::abs (s->playRate - hostRate.load()) > 0.5)
    {
        s->serial = ++serials[(size_t) slot];
        SamplePlayer::makePlayable (*s, hostRate.load());
    }
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        currentSamples[(size_t) slot] = s;
        pendingSamples[(size_t) slot] = s;
        samplePending[(size_t) slot] = true;
    }
    sendChangeMessage();
}

void NektarProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fxRack.prepare (sampleRate, samplesPerBlock);
    mixer.prepare (sampleRate, samplesPerBlock);
    for (auto& pl : players)
        pl.prepare (sampleRate);
    for (auto& gpl : grainPlayers)
        gpl.prepare (sampleRate);
    for (auto& sf : slotFilter)
        sf.prepare (sampleRate, samplesPerBlock);
    for (auto& sh : shifters)
        sh.prepare (sampleRate);
    tilt.prepare (sampleRate);
    macroTone.prepare (sampleRate);
    resonator.prepare (sampleRate);
    keyboardState.reset();
    if (recState.load() == 0) // the buffer also serves Resample, which works in the plugin too
        recBuffer.setSize (2, (int) (sampleRate * kMaxRecordSeconds));

    // the sound must be converted to the new host rate
    hostRate = sampleRate;
    for (int slot = 0; slot < kSlots; ++slot)
        if (auto s = getSample (slot))
        {
            if (std::abs (s->playRate - sampleRate) > 0.5)
            {
                auto copy = std::make_shared<Sample> (*s);
                SamplePlayer::makePlayable (*copy, sampleRate);
                const juce::SpinLock::ScopedLockType lock (sampleLock);
                currentSamples[(size_t) slot] = copy;
                pendingSamples[(size_t) slot] = copy;
                samplePending[(size_t) slot] = true;
            }
            else
            {
                // a new player instance needs the sound again after prepare()
                const juce::SpinLock::ScopedLockType lock (sampleLock);
                pendingSamples[(size_t) slot] = s;
                samplePending[(size_t) slot] = true;
            }
        }
}

bool NektarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void NektarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked())
            for (size_t i = 0; i < (size_t) kSlots; ++i)
                if (samplePending[i])
                {
                    players[i].setSample (pendingSamples[i]);
                    samplePending[i] = false;
                }
    }

    // microphone: copy the input before the output is written (the same buffer carries both)
    {
        const int inCh = juce::jmin (2, getTotalNumInputChannels());
        const int n = buffer.getNumSamples();
        if (inCh > 0)
        {
            float pk = 0.0f;
            for (int ch = 0; ch < inCh; ++ch)
                pk = juce::jmax (pk, buffer.getMagnitude (ch, 0, n));
            inputLevel.store (juce::jmax (pk, inputLevel.load() * 0.9f));

            if (recState.load() == 1 && recSource.load() == 0)
            {
                const int pos = recPos.load();
                const int room = recBuffer.getNumSamples() - pos;
                const int m = juce::jmin (n, room);
                for (int ch = 0; ch < recChannels.load(); ++ch)
                    recBuffer.copyFrom (ch, pos, buffer, juce::jmin (ch, inCh - 1), 0, m);
                recPos.store (pos + m);
                if (m < n)
                    recState.store (2); // full
            }
        }
    }

    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    {   // the host's tempo and position, for the tempo lock and the movements that follow the beat
        hostNow = {};
        if (auto* phd = getPlayHead())
            if (auto pos = phd->getPosition())
            {
                if (auto b = pos->getBpm()) { hostNow.bpm = *b; hostNow.has = *b > 1.0; }
                if (auto q = pos->getPpqPosition()) { hostNow.ppq = *q; hostNow.hasPpq = pos->getIsPlaying(); }
            }
    }
    hostBpmAtomic.store (hostNow.has ? hostNow.bpm : 0.0);
    updateMods (buffer.getNumSamples());
    SamplePlayer::Params p;
    // Morph: slot 1 takes its colour from slot 2, slot 3 from slot 4 (slots 2 and 4 keep playing as they are)
    players[0].setPartner (players[1].getSample());
    players[2].setPartner (players[3].getSample());

    // the voices are shared between the slots that have a sound (a fixed budget keeps the CPU load in check)
    int loaded = 0;
    for (auto& pl : players)
        if (pl.hasSample()) ++loaded;
    const int limit = juce::jmax (8, 32 / juce::jmax (1, loaded));

    buffer.clear();
    double hostBpm = hostNow.has ? hostNow.bpm : 120.0;
    {   // Swell (a pair's level breathes) and Call and response (the pairs take turns), our own shapes:
        // with a host tempo both follow the bar (8 beats a round, the second pair half a round later); without one, 4 s
        const int rel = juce::roundToInt (apvts.getRawParameterValue ("relation")->load());
        relation.store (rel);
        const double beats = hostNow.has ? (hostNow.hasPpq ? hostNow.ppq : freeBeats) : freeBeats;
        freeBeats += (double) buffer.getNumSamples() / (hostRate.load() > 0.0 ? hostRate.load() : 44100.0) * (hostNow.has ? hostNow.bpm : 120.0) / 60.0;
        beatNow.store ((float) (beats - std::floor (beats)));
        beatOn.store (hostNow.has);
        const double ph = beats / 8.0 - std::floor (beats / 8.0);
        for (int pr = 0; pr < 2; ++pr)
        {
            float gsum = 1.0f;
            if (apvts.getRawParameterValue (pr == 0 ? "swell1" : "swell2")->load() > 0.5f)
                gsum *= 0.2f + 0.8f * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (ph + 0.5 * pr)));
            if (rel == 3)
            {
                const float th = juce::MathConstants<float>::halfPi * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * ph));
                gsum *= pr == 0 ? std::cos (th) : std::sin (th);
            }
            {   // the pad's level correction where the stick is now (bilinear between the 3x3 points)
                const float x = juce::jlimit (0.0f, 1.0f, vecXShown[(size_t) pr].load()) * 2.0f, y = juce::jlimit (0.0f, 1.0f, vecYShown[(size_t) pr].load()) * 2.0f;
                const int ix = juce::jmin (1, (int) x), iy = juce::jmin (1, (int) y);
                const float fx = x - (float) ix, fy = y - (float) iy;
                auto at = [this, pr] (int gx, int gy) { return padDbNow[(size_t) pr][(size_t) (gy * 3 + gx)].load(); };
                const float db = (1 - fy) * ((1 - fx) * at (ix, iy) + fx * at (ix + 1, iy)) + fy * ((1 - fx) * at (ix, iy + 1) + fx * at (ix + 1, iy + 1));
                if (vecOn[(size_t) pr].load() && std::abs (db) > 0.01f) gsum *= juce::Decibels::decibelsToGain (db);
            }
            if (onlyPair.load() >= 0 && onlyPair.load() != pr) gsum = 0.0f;
            pairGainNow[(size_t) pr] = gsum;
        }
    }
    const bool hostHasBpm = hostNow.has;
    // Motion knob: 0.5 = as made; down to four times slower, up to four times faster
    const float motionKnob = juce::jlimit (0.0f, 1.0f, modded (mSpeedP));
    const float motion = std::pow (2.0f, 4.0f * (motionKnob - 0.5f));
    const double motionSteps = std::pow (2.0, std::round (4.0 * ((double) motionKnob - 0.5)));
    const juce::MidiBuffer emptyMidi;
    std::array<bool, kSlots> loadedNow {};
    std::array<float, kSlots> pans {};
    for (int slot = 0; slot < kSlots; ++slot)
    {
        auto& pl = players[(size_t) slot];
        if (! pl.hasSample())
            continue;
        p.autoGain = apvts.getRawParameterValue ("autolevel")->load() > 0.5f ? pl.getSample()->autoGain : 1.0f;
        {
            const auto& c = colP[(size_t) slot];
            p.blur = modded (c[kBlur]);
            p.body.tone = modded (c[kTone]); p.body.focus = modded (c[kFocus]); p.body.size = modded (c[kSize]); p.body.rough = modded (c[kGrit]);
            p.model = 0.0f; p.modelNoise = 0.0f;
        }
        {   // Morph of the pair: the first sound of the pair turns into the second (partials paired and moved, MorphVoice);
            // the second sound itself steps back, because it is now heard inside the morph
            const float m = modded (pairMorphP[(size_t) (slot / 2)]);
            const float in = juce::jmin (1.0f, m * 50.0f);
            if (slot % 2 == 0)
            {
                p.morph = m;
                if (m > 0.01f) { p.model = juce::jmax (p.model, in); p.modelNoise = juce::jmax (p.modelNoise, in); }
            }
            else
                p.morph = 0.0f;
        }
        p.level = modded (levelP[(size_t) slot]);
        if (slot % 2 == 1)
            p.level *= std::sqrt (1.0f - juce::jmin (1.0f, modded (pairMorphP[(size_t) (slot / 2)]) * 50.0f)); // heard inside the morph of its pair
        p.pitchSemis = modded (pitchP[(size_t) slot]);
        p.tempoRatio = tempoRatioFor (slot, pl.getSample().get(), hostBpm, hostHasBpm);
        p.releaseSec = kTail;
        pl.setVoiceLimit (limit);
        auto view = mixer.scratchFor (slot, buffer.getNumSamples());
        const auto& gp = grP[(size_t) slot];
        const int grMode = juce::roundToInt (gp[0]->load());
        auto& grPl = grainPlayers[(size_t) slot];
        if (grMode > 0 || grPl.active())
        {
            if (grMode == 0) grPl.allOff(); // switched off while sounding: let it fade
            GrainVoice::Params g;
            g.rhythmic = grMode == 2;
            g.start = 0.0f; g.end = 1.0f; // the sound that is played is already the chosen part
             g.position = modded (gp[3]); g.scan = modded (gp[4]) * motion * (float) tempoRatioFor (slot, getSample (slot).get(), hostBpm, hostHasBpm);
            g.spray = modded (gp[5]); g.sizeMs = modded (gp[6]); g.density = modded (gp[7]); g.jitter = modded (gp[8]);
            g.reverse = modded (gp[9]); g.spread = modded (gp[10]); g.gate = modded (gp[11]);
            g.sizeMs /= std::sqrt (motion); // slower motion = longer grains, so they follow each other more slowly
            g.pattern = (juce::uint32) juce::roundToInt (gp[12]->load());
            g.bpm = hostBpm * motionSteps; // the rhythm moves in halves and doubles, so it stays in time
            g.pitchSemis = p.pitchSemis;
            g.octaveSemis = 12.0f * (float) juce::roundToInt (grOctP[(size_t) slot]->load());
            g.stepSemis = juce::roundToInt (grArpP[(size_t) slot]->load()) > 0 ? arpSteps[(size_t) slot].data() : nullptr;
            g.tapPos = tapPos[(size_t) slot].data(); g.tapCount = &tapCount[(size_t) slot]; g.tapStep = &tapStep[(size_t) slot];
            g.releaseSec = p.releaseSec;
            g.gain = p.level * p.level * p.autoGain;
            const auto smpNow = pl.getSample();
            const bool baked = smpNow->grainSrc.getNumSamples() == smpNow->play.getNumSamples();
            const int nPlay = smpNow->play.getNumSamples();
            auto knobNow = [&] (int id) { return id == 200 ? vecXs[(size_t) (slot / 2)] : id == 201 ? vecYs[(size_t) (slot / 2)] : id == 100 ? modded (airP[(size_t) slot]) : modded (colP[(size_t) slot][(size_t) id]); }; // (ids: the colour indices, 100 air, 200 / 201 the stick)
            if (baked && smpNow->moveParam >= 0 && smpNow->grainSrcHi.getNumSamples() == nPlay)
            {
                g.src2 = &smpNow->grainSrcHi;
                g.mix2 = (knobNow (smpNow->moveParam) - smpNow->moveLo) / juce::jmax (0.001f, smpNow->moveHi - smpNow->moveLo);
                if (smpNow->moveParam2 >= 0 && smpNow->grainSrcY.getNumSamples() == nPlay && smpNow->grainSrcXY.getNumSamples() == nPlay)
                {
                    g.src3 = &smpNow->grainSrcY;
                    g.src4 = &smpNow->grainSrcXY;
                    g.mix3 = (knobNow (smpNow->moveParam2) - smpNow->moveLo2) / juce::jmax (0.001f, smpNow->moveHi2 - smpNow->moveLo2);
                }
            }
            grPl.process (view, grMode > 0 ? midi : emptyMidi, baked ? smpNow->grainSrc : smpNow->play, g);
        }
        if (grMode == 0)
            pl.process (view, midi, p, false);
        {
            const auto& f = sfilP[(size_t) slot];
            slotFilter[(size_t) slot].process (view, view.getNumSamples(), modded (f[0]), modded (f[1]), modded (f[2]), modded (f[3]));
            const float fs = modded (fshiftP[(size_t) slot]), rg = modded (ringP[(size_t) slot]);
            shifters[(size_t) slot].process (view, view.getNumSamples(), (fs < 0.0f ? -1.0f : 1.0f) * 1000.0f * std::abs (fs * fs * fs),
                                             rg > 0.005f ? 20.0f * std::pow (100.0f, rg) : 0.0f, juce::jmin (1.0f, rg * 8.0f));
        }
        {
            const float pg = pairGainNow[(size_t) (slot / 2)], pg0 = pairGainWas[(size_t) slot];
            if (std::abs (pg - pg0) > 1.0e-4f || std::abs (pg - 1.0f) > 1.0e-4f)
                for (int c = 0; c < view.getNumChannels(); ++c) view.applyGainRamp (c, 0, view.getNumSamples(), pg0, pg);
            pairGainWas[(size_t) slot] = pg;
            float m = 0.0f;
            for (int c = 0; c < view.getNumChannels(); ++c) m = juce::jmax (m, view.getRMSLevel (c, 0, view.getNumSamples()));
            slotLevel[(size_t) slot].store (m);
        }
        loadedNow[(size_t) slot] = true;
        pans[(size_t) slot] = 0.0f; // the space places the sounds
    }
    SlotMixer::Space space;
    space.mix = 1.0f; // the sounds are always placed (the old Amount knob is gone: what the pad shows is what is heard)
    space.speakers = spSpkP->load() > 0.5f;
    space.far = 0.5f;  // how much distance is heard (level, air, dryness): the fixed middle
    space.back = 0.7f; // how much darker the sounds behind are
    for (int slot = 0; slot < kSlots; ++slot)
    {
        // the place on the pad, and its movement around it (our own design, no source)
        float az = modded (spAzP[(size_t) slot]), di = juce::jlimit (0.0f, 1.0f, modded (spDistP[(size_t) slot]));
        if (relation.load() == 4)
        {   // Depth: sounds 1 and 2 near and in front, 3 and 4 far and behind (our own rule)
            if (slot < 2) { di *= 0.35f; az = juce::jlimit (-60.0f, 60.0f, az); }
            else { di = 0.55f + 0.45f * di; if (std::abs (az) < 100.0f) az = (az < 0.0f ? -1.0f : 1.0f) * (180.0f - std::abs (az) * 0.6f); }
        }
        const int move = juce::jlimit (0, 5, juce::roundToInt (spMoveP[(size_t) slot]->load()));
        if (move > 0)
        {   // 1 left - right, 2 front - back, 3 an arc in front, 4 an arc behind, 5 a circle round the listener (our own designs)
            const double ph = spaceAdvance (slot, buffer.getNumSamples(), motion);
            const float a0 = juce::degreesToRadians (az), rr = 0.12f + 0.88f * di;
            const float x0 = rr * std::sin (a0), y0 = rr * std::cos (a0), tw = juce::MathConstants<float>::twoPi * (float) ph;
            if (move == 5) az += 360.0f * (float) ph;
            else if (move == 3) az = 80.0f * std::sin (tw);
            else if (move == 4) az = 180.0f + 80.0f * std::sin (tw);
            else
            {   // a straight line through the place; it never passes exactly through the head, where left and right would swap at once
                float x = x0, y = y0;
                if (move == 1) { x = juce::jmax (std::abs (x0), 0.4f) * std::sin (tw); y = (y0 < 0.0f ? -1.0f : 1.0f) * juce::jmax (std::abs (y0), 0.12f); }
                else           { y = juce::jmax (std::abs (y0), 0.4f) * std::cos (tw); x = (x0 < 0.0f ? -1.0f : 1.0f) * juce::jmax (std::abs (x0), 0.12f); }
                az = juce::radiansToDegrees (std::atan2 (x, y));
                di = juce::jlimit (0.0f, 1.0f, (juce::jmin (1.0f, std::sqrt (x * x + y * y)) - 0.12f) / 0.88f);
            }
            while (az > 180.0f) az -= 360.0f;
            while (az < -180.0f) az += 360.0f;
        }
        spAzShown[(size_t) slot].store (az); spDistShown[(size_t) slot].store (di);
        space.azDeg[(size_t) slot] = az;
        space.dist[(size_t) slot] = di;
    }
    mixer.setSolo (soloSlot.load());
    mixer.setCross ({ modded (xmorphP[0]), modded (xmorphP[1]) });
    mixer.mix (buffer, buffer.getNumSamples(), loadedNow, pans, { modded (imorphP[0]), modded (imorphP[1]) }, space);
    {
        float m = 0.0f;
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c) m = juce::jmax (m, buffer.getRMSLevel (c, 0, buffer.getNumSamples()));
        mixLevel.store (m);
    }

    // Resonator: strings tuned to the held keys, fed by the sound of the slots; decay 0..1 -> 0.1 .. 8 s (opened by Harmony -> Chord)
    {
        const float decaySec = 0.1f * std::pow (80.0f, modded (resDecayP)), mat = modded (resMatP);
        resonator.process (buffer, midi, modded (resTuneP), decaySec, mat, resMixNow());
    }

    // Bright: tilt of the whole sound (0.5 = off)
    tilt.setTilt ((modded (brightP) - 0.5f) * 2.0f * 9.0f);
    tilt.process (buffer);
    macroTone.process (buffer, modded (mWeightP), modded (mLightP));


    // Master out: the last gain, before the safety knee (smoothed over the block)
    fxRack.process (buffer, midi); // the user's own effects (VST3), on the whole sound

    {
        const float g = std::pow (10.0f, masterP->load() / 20.0f);
        if (std::abs (g - 1.0f) > 1.0e-4f || std::abs (lastMaster - 1.0f) > 1.0e-4f)
        {
            buffer.applyGainRamp (0, buffer.getNumSamples(), lastMaster, g);
            lastMaster = g;
        }
    }

    {   // the output level before the safety knee (shown at the river's edge; red when the knee is working)
        float pk = 0.0f;
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch) pk = juce::jmax (pk, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
        if (pk > outPeak.load()) outPeak.store (pk);
    }
    // Safety: a soft knee above -0.9 dBFS-ish (0.9 linear) keeps peaks below 1.0; identical below 0.9
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float a = std::abs (d[i]);
            if (a > 0.9f)
                d[i] = std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), d[i]);
        }
    }

    // Resample: copy what is about to be heard
    if (recState.load() == 1 && recSource.load() == 1)
    {
        const int n = buffer.getNumSamples();
        const int pos = recPos.load();
        const int m = juce::jmin (n, recBuffer.getNumSamples() - pos);
        for (int ch = 0; ch < 2; ++ch)
            if (m > 0)
                recBuffer.copyFrom (ch, pos, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, m);
        recPos.store (pos + juce::jmax (0, m));
        if (m < n)
            recState.store (2); // full

        // auto-stop: once the sound has been heard and 2.5 s of silence follow, the take ends (0.4 s of the silence is kept as the tail)
        const float pk = juce::jmax (buffer.getMagnitude (0, 0, n), buffer.getNumChannels() > 1 ? buffer.getMagnitude (1, 0, n) : 0.0f);
        int sil = rsSilent.load();
        if (pk > 0.0005f)
            sil = 0;
        else if (sil >= 0)
            sil += n;
        rsSilent.store (sil);
        const int sr = (int) hostRate.load();
        if (sil > (int) (2.5 * sr) && recState.load() == 1)
        {
            recPos.store (juce::jmax (1, recPos.load() - (sil - (int) (0.4 * sr))));
            recState.store (2);
        }
    }
    midi.clear();
}

juce::String NektarProcessor::loadSample (const juce::File& file, int slot)
{
    if (slot < 0 || slot >= kSlots)
        return "No such slot.";
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Could not read this file.";
    if (reader->lengthInSamples < 2)
        return "The file is empty.";
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 600.0))
        return "The file is longer than 10 minutes.";

    auto s = std::make_shared<Sample>();
    s->rate = reader->sampleRate;
    s->name = file.getFileNameWithoutExtension();
    s->path = file.getFullPathName();
    s->data.setSize ((int) juce::jmin (2u, reader->numChannels), (int) reader->lengthInSamples);
    reader->read (&s->data, 0, (int) reader->lengthInSamples, 0, true, s->data.getNumChannels() > 1);

    publishSample (s, slot);
    return {};
}

void NektarProcessor::publishSample (std::shared_ptr<Sample> s, int slot)
{
    s->partA = grP[(size_t) slot][1]->load(); s->partB = grP[(size_t) slot][2]->load();
    SamplePlayer::makePlayable (*s, hostRate.load());
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        currentSamples[(size_t) slot] = s;
        pendingSamples[(size_t) slot] = s;
        samplePending[(size_t) slot] = true;
    }
    sendChangeMessage();
    scheduleClean (slot); // analysis (and cleaning, if the knobs are up)
}

// ---- microphone recording
void NektarProcessor::startRecording (int slot)
{
    if (! recordingAllowed || recState.load() == 1)
        return;
    recSource.store (0);
    recSlot.store (slot == kToLibrary ? kToLibrary : juce::jlimit (0, kSlots - 1, slot));
    recChannels.store (juce::jlimit (1, 2, getTotalNumInputChannels()));
    recBuffer.clear();
    recPos.store (0);
    recState.store (1);
}

void NektarProcessor::startResample (int slot)
{
    if (recState.load() == 1 || recBuffer.getNumSamples() < 2)
        return;
    recSource.store (1);
    rsSilent.store (-1);
    recSlot.store (juce::jlimit (kToLibrary, kSlots - 1, slot));
    if (slot >= 0)
    {   // one sound: only it is heard (and recorded) until the take ends
        soloBeforeResample = soloSlot.load();
        soloSlot.store (slot);
    }
    recChannels.store (2);
    recBuffer.clear();
    recPos.store (0);
    recState.store (1);
}

double NektarProcessor::getRecordedSeconds() const
{
    const double sr = hostRate.load();
    return sr > 0.0 ? (double) recPos.load() / sr : 0.0;
}

juce::String NektarProcessor::stopRecording()
{
    if (recState.load() == 0)
        return {};
    recState.store (0);
    const int n = recPos.load();
    const int ch = recChannels.load();
    const bool wasResample = recSource.load() == 1;
    const int target = recSlot.load();
    if (soloBeforeResample != -2) { soloSlot.store (soloBeforeResample); soloBeforeResample = -2; }
    if (target == kToLibrary)
    {   // a new sound in the library: Recordings (microphone) or Resampled (the whole Nectar)
        if (n < (int) (hostRate.load() * 0.1))
            return {};
        juce::AudioBuffer<float> take (ch, n);
        for (int c = 0; c < ch; ++c)
            take.copyFrom (c, 0, recBuffer, c, 0, n);
        const auto dir = getLibraryFolder().getChildFile (wasResample ? "Resampled" : "Recordings");
        if (! dir.createDirectory())
            return "Could not create the library folder.";
        const auto file = dir.getChildFile (juce::String (wasResample ? "Resample " : "Rec ") + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav").getNonexistentSibling();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return "Could not write the file.";
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), hostRate.load(), (unsigned int) ch, 24, {}, 0));
        if (writer == nullptr)
            return "Could not write the file.";
        stream.release();
        writer->writeFromAudioSampleBuffer (take, 0, n);
        writer.reset();
        rescanLibrary();
        return "New sound in the library: " + dir.getFileName() + " / " + file.getFileNameWithoutExtension();
    }
    if (target < 0)
        return {};
    if (n < (int) (hostRate.load() * 0.1)) // shorter than 0.1 s: ignore
        return {};

    auto s = std::make_shared<Sample>();
    s->rate = hostRate.load();
    s->name = juce::String (recSource.load() == 1 ? "Resample " : "Rec ") + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    s->path = {};
    s->data.setSize (ch, n);
    for (int c = 0; c < ch; ++c)
        s->data.copyFrom (c, 0, recBuffer, c, 0, n);
    publishSample (s, target);
    if (! wasResample)
        return {};
    {   // the changes are now inside the sound: its own processing goes back to neutral, so nothing is heard twice
        const juce::String sfx = target == 0 ? juce::String() : juce::String (target + 1);
        for (auto* id : { "pitch", "grmode", "grstart", "grend", "grpos", "grscan", "grspray", "grsize", "grdens", "grjit", "grrev",
                          "grspread", "grgate", "grpat", "groct", "blur", "bodytone", "bodyfocus", "bodysize",
                          "srcrough", "sfiltype", "sfilcut", "sfilres", "sfilmix", "air", "fshift", "ring",
                           })
            if (auto* p = apvts.getParameter (juce::String (id) + sfx))
                p->setValueNotifyingHost (p->getDefaultValue());
    }
    return "Sound " + juce::String (target + 1) + " now holds its changes. Press Make for new corners.";
}

juce::File NektarProcessor::getLibraryFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Nektar Library");
}

juce::String NektarProcessor::saveToLibrary (int slot)
{
    auto s = getSample (slot);
    if (s == nullptr)
        return "Nothing to save yet.";

    const auto dir = getLibraryFolder();
    if (! dir.createDirectory())
        return "Could not create the library folder.";

    auto file = dir.getChildFile (juce::File::createLegalFileName (s->name) + ".wav").getNonexistentSibling();
    if (juce::File (s->path).isAChildOf (dir))
        return "Already in the library.";

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return "Could not write the file.";
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), s->rate, (unsigned int) s->data.getNumChannels(), 24, {}, 0));
    if (writer == nullptr)
        return "Could not write the file.";
    stream.release(); // the writer owns it now
    writer->writeFromAudioSampleBuffer (s->data, 0, s->data.getNumSamples());
    writer.reset();
    return "Saved: " + file.getFileName();
}

// ---- presets
juce::Array<juce::File> NektarProcessor::listPresets()
{
    juce::Array<juce::File> files = getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.nektarpreset");
    files.sort();
    return files;
}

juce::String NektarProcessor::renamePreset (const juce::File& file, const juce::String& newName)
{
    const auto clean = juce::File::createLegalFileName (newName.trim());
    if (clean.isEmpty())
        return "Give the preset a name.";
    const auto target = file.getParentDirectory().getChildFile (clean + ".nektarpreset");
    if (target == file)
        return {};
    if (target.exists())
        return "A preset with that name already exists.";
    return file.moveFileTo (target) ? "Renamed to: " + clean : "Could not rename the preset.";
}

juce::String NektarProcessor::deletePreset (const juce::File& file)
{
    const auto name = file.getFileNameWithoutExtension();
    return file.moveToTrash() ? "Moved to the Trash: " + name : "Could not delete the preset.";
}

juce::StringArray NektarProcessor::factoryNames()
{
    return { "Init (all knobs to default)" };
}

juce::String NektarProcessor::loadFactory (int index)
{
    for (auto* prm : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            r->setValueNotifyingHost (r->getDefaultValue());
    readModFromState (juce::ValueTree ("EMPTY")); // no joystick corners
    const auto names = factoryNames();
    return "Factory: " + names[juce::jlimit (0, names.size() - 1, index)];
}

void NektarProcessor::storeSnapshot (int which)
{
    auto state = apvts.copyState();
    addModToState (state);
    snapshots[(size_t) juce::jlimit (0, 1, which)] = state;
}

bool NektarProcessor::recallSnapshot (int which)
{
    const auto& snap = snapshots[(size_t) juce::jlimit (0, 1, which)];
    if (! snap.isValid())
        return false;
    auto state = snap.createCopy();
    apvts.replaceState (state);
    readModFromState (state);
    return true;
}

juce::String NektarProcessor::savePreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return "Give the preset a name.";
    auto state = apvts.copyState();
    for (int slot = 0; slot < kSlots; ++slot)
    {
        auto s = getSample (slot);
        const juce::String key = "samplePath" + juce::String (slot + 1);
        state.removeProperty (key, nullptr);
        if (s == nullptr)
            continue;
        juce::String path = s->path;
        if (path.isEmpty() || ! juce::File (path).existsAsFile())
        {
            const auto msg = saveToLibrary (slot);
            if (! msg.startsWith ("Saved: "))
                return "Could not keep the sound of slot " + juce::String (slot + 1) + ": " + msg;
            path = getLibraryFolder().getChildFile (msg.fromFirstOccurrenceOf ("Saved: ", false, false)).getFullPathName();
        }
        state.setProperty (key, path, nullptr);
    }
    addModToState (state);
    if (! getPresetFolder().createDirectory())
        return "Could not create the presets folder.";
    const auto file = getPresetFolder().getChildFile (clean + ".nektarpreset");
    if (auto xml = state.createXml())
        if (xml->writeTo (file))
            return "Saved preset: " + clean;
    return "Could not write the preset.";
}

// A preset remembers where each sound file was. When the file is not there (a preset made on another computer),
// the sound is looked up by its name in the Nektar Library: letters and digits only, so "nord_ pad 1.wav",
// "nord__pad_1.wav" and "031d8657-nord__pad_1.wav" (an upload prefix) all mean the same file.
static juce::File findSampleFile (const juce::String& path, const juce::File& library)
{
    const juce::File direct (path);
    if (path.isEmpty() || direct.existsAsFile()) return direct;
    auto norm = [] (juce::String name)
    {
        name = name.upToLastOccurrenceOf (".", false, false).toLowerCase();
        if (name.length() > 9 && name[8] == '-' && name.substring (0, 8).containsOnly ("0123456789abcdef")) name = name.substring (9);
        return name.retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
    };
    const auto want = norm (path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false));
    if (want.isEmpty() || ! library.isDirectory()) return {};
    for (const auto& e : juce::RangedDirectoryIterator (library, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3", juce::File::findFiles))
        if (norm (e.getFile().getFileName()) == want) return e.getFile();
    return {};
}

juce::String NektarProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return "Could not read the preset.";
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return "Could not read the preset.";
    juce::String missing;
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const auto path = state.getProperty ("samplePath" + juce::String (slot + 1)).toString();
        if (path.isNotEmpty() && ! findSampleFile (path, getLibraryFolder()).existsAsFile())
            missing += (missing.isEmpty() ? "" : ", ") + juce::String (slot + 1) + " (" + juce::File (path).getFileName() + ")";
    }
    apvts.replaceState (state);
    readModFromState (state);
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const auto path = state.getProperty ("samplePath" + juce::String (slot + 1)).toString();
        const auto found = findSampleFile (path, getLibraryFolder());
        if (path.isNotEmpty() && found.existsAsFile())
            loadSample (found, slot);
    }
    return missing.isEmpty() ? juce::String() : "Sound file not found in the Nektar Library for slot " + missing + ".";
}

std::shared_ptr<const Sample> NektarProcessor::getSample (int slot) const
{
    if (slot < 0 || slot >= kSlots)
        return nullptr;
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return currentSamples[(size_t) slot];
}

void NektarProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    for (int slot = 0; slot < kSlots; ++slot)
        if (auto s = getSample (slot))
            state.setProperty ("samplePath" + juce::String (slot + 1), s->path, nullptr);
    addModToState (state);
    if (auto xml = state.createXml())
    {
        xml->addChildElement (fxRack.toXml().release());
        copyXmlToBinary (*xml, dest);
    }
}

void NektarProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        {   // the effects open on the message thread
            std::shared_ptr<juce::XmlElement> fx;
            if (auto* f = xml->getChildByName ("FX")) fx = std::make_shared<juce::XmlElement> (*f);
            if (juce::MessageManager::existsAndIsCurrentThread())
                fxRack.fromXml (fx.get());
            else
            {
                auto* self = this;
                auto alive = std::weak_ptr<int> (aliveToken);
                juce::MessageManager::callAsync ([self, fx, alive] { if (! alive.expired()) self->fxRack.fromXml (fx.get()); });
            }
            xml->deleteAllChildElementsWithTagName ("FX");
        }
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.isValid())
        {
            apvts.replaceState (state);
            readModFromState (state);
            for (int slot = 0; slot < kSlots; ++slot)
            {
                auto path = state.getProperty ("samplePath" + juce::String (slot + 1)).toString();
                if (path.isEmpty() && slot == 0)
                    path = state.getProperty ("samplePath").toString(); // saved by an older version
                if (path.isNotEmpty() && juce::File (path).existsAsFile())
                    loadSample (juce::File (path), slot);
            }
        }
    }
}

juce::AudioProcessorEditor* NektarProcessor::createEditor()
{
    return new NektarEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NektarProcessor();
}

// ---- modulation
void NektarProcessor::buildModTable()
{
    modIds.clear();
    modRanges.clear();
    for (auto* par : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (par))
            if (auto* raw = apvts.getRawParameterValue (rp->paramID))
            {
                modIdxByRaw[raw] = (int) modIds.size();
                modIdxById[rp->paramID.toStdString()] = (int) modIds.size();
                modIds.push_back (rp->paramID);
                modRanges.push_back (rp->getNormalisableRange());
            }
    vecVal = std::vector<std::atomic<float>> (modIds.size());
    vecAct = std::vector<std::atomic<bool>> (modIds.size());
    for (auto& a : vecAct) a.store (false);
}

void NektarProcessor::setVectorOn (int pair, bool on)
{
    vecOn[(size_t) pair].store (on);
    if (! on)
    {
        const juce::SpinLock::ScopedLockType lk (vecLock);
        for (const auto& e : vecEntries[(size_t) pair]) vecAct[(size_t) e.idx].store (false);
    }
    for (int slot = pair * 2; slot < pair * 2 + 2; ++slot)
    {   // the baked copies for the grains follow the corners
        cleanChangedAt[(size_t) slot] = juce::Time::getMillisecondCounter();
        cleanDirty[(size_t) slot] = true;
    }
}

NektarProcessor::VecSnapshot NektarProcessor::vectorNow (int pair)
{
    VecSnapshot snap;
    {
        const juce::SpinLock::ScopedLockType lk (vecLock);
        snap.entries = vecEntries[(size_t) pair];
    }
    for (int k = 0; k < 2; ++k)
    {
        snap.grMode[k] = juce::roundToInt (grP[(size_t) (pair * 2 + k)][0]->load());
        snap.grPat[k] = juce::roundToInt (grP[(size_t) (pair * 2 + k)][12]->load());
        snap.grOct[k] = juce::roundToInt (grOctP[(size_t) (pair * 2 + k)]->load());
        snap.grArp[k] = juce::roundToInt (grArpP[(size_t) (pair * 2 + k)]->load());
    }
    snap.swell = apvts.getRawParameterValue (pair == 0 ? "swell1" : "swell2")->load() > 0.5f;
    for (int i = 0; i < 9; ++i) snap.padDb[(size_t) i] = padDbNow[(size_t) pair][(size_t) i].load();
    return snap;
}

void NektarProcessor::applyVector (int pair, const VecSnapshot& snap)
{
    for (int k = 0; k < 2; ++k)
    {
        const int slot = pair * 2 + k;
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        if (auto* pa = apvts.getParameter ("grmode" + sfx)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) snap.grMode[k]));
        if (auto* pa = apvts.getParameter ("grpat" + sfx)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) snap.grPat[k]));
        if (auto* pa = apvts.getParameter ("groct" + sfx)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) snap.grOct[k]));
        if (auto* pa = apvts.getParameter ("grarp" + sfx)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) snap.grArp[k]));
    }
    if (auto* pa = apvts.getParameter (pair == 0 ? "swell1" : "swell2")) pa->setValueNotifyingHost (snap.swell ? 1.0f : 0.0f);
    for (int i = 0; i < 9; ++i) padDbNow[(size_t) pair][(size_t) i].store (snap.padDb[(size_t) i]);
    {
        const juce::SpinLock::ScopedLockType lk (vecLock);
        for (const auto& e : vecEntries[(size_t) pair]) vecAct[(size_t) e.idx].store (false);
        vecEntries[(size_t) pair] = snap.entries;
    }
    setVectorOn (pair, ! snap.entries.empty());
}

bool NektarProcessor::backVector (int pair)
{
    auto& h = vecHistory[(size_t) pair];
    if (h.empty()) return false;
    const auto snap = h.back();
    h.pop_back();
    applyVector (pair, snap);
    return true;
}

void NektarProcessor::pinVector (const juce::String& paramId, float value01)
{
    const auto it = modIdxById.find (paramId.toStdString());
    if (it == modIdxById.end()) return;
    for (int pair = 0; pair < 2; ++pair)
    {
        bool hit = false;
        {
            const juce::SpinLock::ScopedLockType lk (vecLock);
            for (auto& e : vecEntries[(size_t) pair])
                if (e.idx == it->second) { for (auto& c : e.c) c = value01; hit = true; }
        }
        if (hit && vecOn[(size_t) pair].load())
            for (int slot = pair * 2; slot < pair * 2 + 2; ++slot)
            {
                cleanChangedAt[(size_t) slot] = juce::Time::getMillisecondCounter();
                cleanDirty[(size_t) slot] = true;
            }
    }
}

// "New": four corner settings for one pair, drawn at random inside the limits of the chosen Character.
// Pitch and the chosen part of each sound are left alone, so the result stays playable in tune; in every corner the
// sound that Cross favours is clearly heard, and the grains are taken from places where the sound is present.
// The limits are our own choices, tuned by ear with the user; they are not taken from any source.
void NektarProcessor::newVector (int pair, int seed, int parts, bool history)
{
    if (history && vecOn[(size_t) pair].load())
    {
        auto& h = vecHistory[(size_t) pair];
        h.push_back (vectorNow (pair));
        if (h.size() > 24) h.erase (h.begin());
    }
    newVectorCore (pair, seed, parts, nullptr, 8);
}

void NektarProcessor::newVectorCore (int pair, int seed, int parts, std::vector<VecSnapshot>* draftsOut, int draftCount)
{
    juce::Random master (seed != 0 ? seed : vecRnd.nextInt());
    const juce::String chId = pair == 0 ? "character1" : "character2";
    if (pair == 1 && juce::roundToInt (apvts.getRawParameterValue ("relation")->load()) == 1)
    {   // Contrast: joystick 2 takes the character opposite to joystick 1's (our own pairs)
        static const int opposite[13] = { 7, 5, 3, 2, 10, 1, 8, 0, 6, 11, 5, 9, 6 };
        const int c1 = juce::jlimit (0, 12, juce::roundToInt (apvts.getRawParameterValue ("character1")->load()));
        if (auto* pa = apvts.getParameter (chId)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) opposite[c1]));
    }
    const int character = juce::jlimit (0, 12, juce::roundToInt (apvts.getRawParameterValue (chId)->load()));
    // what Make may change, per sound: rhythm (0), colour (1), texture (2), each by its own amount
    auto groupOf = [] (const juce::String& id)
    {
        for (auto* r : { "grgate", "grsize", "grdens", "grrev", "grpos" }) if (id.startsWith (r)) return 0;
        for (auto* t : { "grspray", "grjit", "grspread", "blur", "srcrough", "grscan" }) if (id.startsWith (t)) return 2;
        return 1; // filter, body, air, frequency shift, ring
    };
    float amount[2][3];
    for (int k = 0; k < 2; ++k)
    {
        const int slot = pair * 2 + k;
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        amount[k][0] = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("mkrhy" + sfx)->load());
        amount[k][1] = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("mkcol" + sfx)->load());
        amount[k][2] = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("mktex" + sfx)->load());
    }
    // the first corner stays near the sound as it is, the others go further (our own choice, to be judged by ear)
    static const float reach[4] = { 0.3f, 0.7f, 0.85f, 1.0f };

    struct R { const char* id; float lo, hi, chance, off; }; // chance: how often the knob is in use at all; off: its value otherwise
    static const R cloud[] = { { "grspray", 0.2f, 0.6f, 1, 0 }, { "grsize", 0.45f, 0.8f, 1, 0 }, { "grdens", 0.5f, 0.9f, 1, 0 }, { "grjit", 0.0f, 0.2f, 0.3f, 0 },
        { "grrev", 0, 0, 0, 0 }, { "grspread", 0.6f, 1.0f, 1, 0 }, { "sfiltype", 0.0f, 0.25f, 1, 0 }, { "sfilcut", 0.45f, 1.0f, 1, 0 }, { "sfilres", 0.1f, 0.4f, 1, 0 },
        { "fshift", 0, 0, 0, 0.5f }, { "ring", 0, 0, 0, 0 }, { "bodytone", 0.3f, 0.7f, 1, 0 }, { "bodyfocus", 0.3f, 0.7f, 1, 0 }, { "bodysize", 0.3f, 0.7f, 1, 0 },
        { "blur", 0.1f, 0.5f, 0.5f, 0 }, { "srcrough", 0, 0, 0, 0 }, { "air", 0.4f, 0.7f, 1, 0 }, { "grgate", 0.5f, 0.5f, 1, 0 } };
    static const R rhythm[] = { { "grgate", 0.2f, 0.95f, 1, 0 }, { "grspray", 0.0f, 0.15f, 1, 0 }, { "grrev", 0.3f, 1.0f, 0.4f, 0 }, { "grspread", 0.2f, 0.8f, 1, 0 },
        { "sfiltype", 0.0f, 1.0f, 1, 0 }, { "sfilcut", 0.3f, 1.0f, 1, 0 }, { "sfilres", 0.2f, 0.7f, 1, 0 }, { "fshift", 0.3f, 0.7f, 0.3f, 0.5f }, { "ring", 0.1f, 0.5f, 0.25f, 0 },
        { "bodytone", 0.3f, 0.8f, 1, 0 }, { "bodyfocus", 0.3f, 0.8f, 1, 0 }, { "bodysize", 0.2f, 0.8f, 1, 0 }, { "blur", 0, 0, 0, 0 }, { "srcrough", 0.1f, 0.5f, 0.4f, 0 },
        { "air", 0.4f, 0.8f, 1, 0 }, { "grsize", 0.4f, 0.6f, 1, 0 }, { "grdens", 0.5f, 0.6f, 1, 0 }, { "grjit", 0, 0, 0, 0 } };
    static const R metal[] = { { "grsize", 0.08f, 0.35f, 1, 0 }, { "grdens", 0.5f, 1.0f, 1, 0 }, { "grspray", 0.0f, 0.2f, 1, 0 }, { "grjit", 0.0f, 0.15f, 0.5f, 0 },
        { "grrev", 0, 0, 0, 0 }, { "grspread", 0.4f, 1.0f, 1, 0 }, { "sfiltype", 0.45f, 0.8f, 1, 0 }, { "sfilcut", 0.4f, 0.9f, 1, 0 }, { "sfilres", 0.4f, 0.8f, 1, 0 },
        { "fshift", 0.2f, 0.8f, 0.7f, 0.5f }, { "ring", 0.15f, 0.7f, 0.6f, 0 }, { "bodytone", 0.45f, 0.85f, 1, 0 }, { "bodyfocus", 0.6f, 0.9f, 1, 0 }, { "bodysize", 0.1f, 0.9f, 1, 0 },
        { "blur", 0, 0, 0, 0 }, { "srcrough", 0.1f, 0.6f, 0.5f, 0 }, { "air", 0.3f, 0.6f, 1, 0 }, { "grgate", 0.5f, 0.5f, 1, 0 } };
    static const R airy[] = { { "grsize", 0.55f, 0.9f, 1, 0 }, { "grdens", 0.6f, 1.0f, 1, 0 }, { "grspray", 0.3f, 0.7f, 1, 0 }, { "grjit", 0.0f, 0.3f, 0.5f, 0 },
        { "grrev", 0.2f, 0.6f, 0.3f, 0 }, { "grspread", 0.7f, 1.0f, 1, 0 }, { "sfiltype", 0.0f, 0.3f, 1, 0 }, { "sfilcut", 0.5f, 1.0f, 1, 0 }, { "sfilres", 0.1f, 0.3f, 1, 0 },
        { "fshift", 0, 0, 0, 0.5f }, { "ring", 0, 0, 0, 0 }, { "bodytone", 0.5f, 0.85f, 1, 0 }, { "bodyfocus", 0.2f, 0.5f, 1, 0 }, { "bodysize", 0.3f, 0.8f, 1, 0 },
        { "blur", 0.3f, 0.8f, 1, 0 }, { "srcrough", 0.1f, 0.4f, 0.5f, 0 }, { "air", 0.72f, 1.0f, 1, 0 }, { "grgate", 0.5f, 0.5f, 1, 0 } };
    static const R backw[] = { { "grrev", 0.6f, 1.0f, 1, 0 }, { "grsize", 0.5f, 0.9f, 1, 0 }, { "grdens", 0.5f, 0.9f, 1, 0 }, { "grspray", 0.3f, 0.7f, 1, 0 },
        { "grjit", 0.1f, 0.4f, 1, 0 }, { "grspread", 0.5f, 1.0f, 1, 0 }, { "sfiltype", 0.0f, 0.25f, 1, 0 }, { "sfilcut", 0.45f, 1.0f, 1, 0 }, { "sfilres", 0.1f, 0.4f, 1, 0 },
        { "fshift", 0.35f, 0.65f, 0.2f, 0.5f }, { "ring", 0, 0, 0, 0 }, { "bodytone", 0.3f, 0.7f, 1, 0 }, { "bodyfocus", 0.3f, 0.7f, 1, 0 }, { "bodysize", 0.3f, 0.7f, 1, 0 },
        { "blur", 0.1f, 0.4f, 0.4f, 0 }, { "srcrough", 0, 0, 0, 0 }, { "air", 0.4f, 0.7f, 1, 0 }, { "grgate", 0.5f, 0.5f, 1, 0 } };
    // the new characters (values are 0..1 of each knob's range; our own recipes, to be judged by ear)
    static const R drone[] = { { "grsize", 0.8f, 1.0f, 1, 0 }, { "grdens", 0.7f, 1.0f, 1, 0 }, { "grspray", 0.0f, 0.1f, 1, 0 }, { "grjit", 0, 0, 0, 0 },
        { "grrev", 0, 0, 0, 0 }, { "grspread", 0.3f, 0.7f, 1, 0 }, { "sfiltype", 0.0f, 0.2f, 1, 0 }, { "sfilcut", 0.15f, 0.45f, 1, 0 }, { "sfilres", 0.1f, 0.3f, 1, 0 },
        { "fshift", 0, 0, 0, 0.5f }, { "ring", 0, 0, 0, 0 }, { "bodytone", 0.1f, 0.4f, 1, 0 }, { "bodyfocus", 0.4f, 0.7f, 1, 0 }, { "bodysize", 0.6f, 0.9f, 1, 0 },
        { "blur", 0.5f, 0.9f, 1, 0 }, { "srcrough", 0, 0, 0, 0 }, { "air", 0.2f, 0.45f, 1, 0 }, { "grgate", 0.5f, 0.5f, 1, 0 } };
    static const R glitch[] = { { "grsize", 0.05f, 0.25f, 1, 0 }, { "grdens", 0.6f, 1.0f, 1, 0 }, { "grspray", 0.4f, 1.0f, 1, 0 }, { "grjit", 0.5f, 1.0f, 1, 0 },
        { "grrev", 0.3f, 1.0f, 0.5f, 0 }, { "grspread", 0.5f, 1.0f, 1, 0 }, { "sfiltype", 0.0f, 1.0f, 1, 0 }, { "sfilcut", 0.4f, 1.0f, 1, 0 }, { "sfilres", 0.2f, 0.6f, 1, 0 },
        { "fshift", 0.3f, 0.7f, 0.3f, 0.5f }, { "ring", 0.2f, 0.6f, 0.3f, 0 }, { "bodytone", 0.4f, 0.8f, 1, 0 }, { "bodyfocus", 0.3f, 0.8f, 1, 0 }, { "bodysize", 0.2f, 0.7f, 1, 0 },
        { "blur", 0, 0, 0, 0 }, { "srcrough", 0.3f, 0.8f, 0.5f, 0 }, { "air", 0.3f, 0.6f, 1, 0 }, { "grgate", 0.1f, 0.6f, 1, 0 } };
    static const R dust[] = { { "grsize", 0.02f, 0.12f, 1, 0 }, { "grdens", 0.05f, 0.25f, 1, 0 }, { "grspray", 0.6f, 1.0f, 1, 0 }, { "grjit", 0.6f, 1.0f, 1, 0 },
        { "grrev", 0.2f, 0.6f, 0.4f, 0 }, { "grspread", 0.8f, 1.0f, 1, 0 }, { "sfiltype", 0.0f, 0.25f, 1, 0 }, { "sfilcut", 0.7f, 1.0f, 1, 0 }, { "sfilres", 0.1f, 0.3f, 1, 0 },
        { "fshift", 0, 0, 0, 0.5f }, { "ring", 0, 0, 0, 0 }, { "bodytone", 0.6f, 0.9f, 1, 0 }, { "bodyfocus", 0.2f, 0.5f, 1, 0 }, { "bodysize", 0.2f, 0.6f, 1, 0 },
        { "blur", 0, 0, 0, 0 }, { "srcrough", 0.2f, 0.6f, 0.6f, 0 }, { "air", 0.6f, 0.9f, 1, 0 }, { "grgate", 0.5f, 0.5f, 1, 0 } };
    static const R bells[] = { { "grsize", 0.3f, 0.6f, 1, 0 }, { "grdens", 0.4f, 0.8f, 1, 0 }, { "grspray", 0.0f, 0.2f, 1, 0 }, { "grjit", 0.0f, 0.1f, 0.3f, 0 },
        { "grrev", 0, 0, 0, 0 }, { "grspread", 0.4f, 1.0f, 1, 0 }, { "sfiltype", 0.45f, 0.8f, 1, 0 }, { "sfilcut", 0.5f, 0.9f, 1, 0 }, { "sfilres", 0.6f, 0.9f, 1, 0 },
        { "fshift", 0.2f, 0.8f, 1, 0.5f }, { "ring", 0.25f, 0.7f, 1, 0 }, { "bodytone", 0.5f, 0.85f, 1, 0 }, { "bodyfocus", 0.8f, 1.0f, 1, 0 }, { "bodysize", 0.2f, 0.6f, 1, 0 },
        { "blur", 0, 0, 0, 0 }, { "srcrough", 0, 0, 0, 0 }, { "air", 0.2f, 0.4f, 1, 0 }, { "grgate", 0.5f, 0.5f, 1, 0 } };
    static const R shimmer[] = { { "grsize", 0.5f, 0.85f, 1, 0 }, { "grdens", 0.6f, 1.0f, 1, 0 }, { "grspray", 0.2f, 0.5f, 1, 0 }, { "grjit", 0.0f, 0.15f, 0.4f, 0 },
        { "grrev", 0, 0, 0, 0 }, { "grspread", 0.7f, 1.0f, 1, 0 }, { "sfiltype", 0.0f, 0.25f, 1, 0 }, { "sfilcut", 0.7f, 1.0f, 1, 0 }, { "sfilres", 0.1f, 0.3f, 1, 0 },
        { "fshift", 0, 0, 0, 0.5f }, { "ring", 0, 0, 0, 0 }, { "bodytone", 0.6f, 0.9f, 1, 0 }, { "bodyfocus", 0.3f, 0.6f, 1, 0 }, { "bodysize", 0.4f, 0.8f, 1, 0 },
        { "blur", 0.2f, 0.6f, 0.6f, 0 }, { "srcrough", 0, 0, 0, 0 }, { "air", 0.7f, 0.95f, 1, 0 }, { "grgate", 0.5f, 0.5f, 1, 0 } };
    static const R arp[] = { { "grgate", 0.4f, 0.9f, 1, 0 }, { "grspray", 0.0f, 0.1f, 1, 0 }, { "grrev", 0, 0, 0, 0 }, { "grspread", 0.3f, 0.8f, 1, 0 },
        { "sfiltype", 0.0f, 0.3f, 1, 0 }, { "sfilcut", 0.45f, 0.95f, 1, 0 }, { "sfilres", 0.2f, 0.5f, 1, 0 }, { "fshift", 0, 0, 0, 0.5f }, { "ring", 0, 0, 0, 0 },
        { "bodytone", 0.4f, 0.8f, 1, 0 }, { "bodyfocus", 0.5f, 0.85f, 1, 0 }, { "bodysize", 0.3f, 0.7f, 1, 0 }, { "blur", 0, 0, 0, 0 }, { "srcrough", 0, 0, 0, 0 },
        { "air", 0.35f, 0.65f, 1, 0 }, { "grsize", 0.3f, 0.5f, 1, 0 }, { "grdens", 0.5f, 0.6f, 1, 0 }, { "grjit", 0, 0, 0, 0 } };
    // Swell and Melt take Cloud's recipe and add their own movement (the pair's level breathing; the Morph between the two)
    const R* tables[13] = { cloud, rhythm, metal, airy, backw, drone, cloud, glitch, dust, bells, shimmer, cloud, arp };
    const bool rhythmic = character == 1 || character == 7 || character == 12;
    const R* table = tables[character];
    const int tableLen = 18;

    const VecSnapshot cur = vectorNow (pair);
    auto gen = [&] (juce::Random& rnd) -> VecSnapshot
    {
    VecSnapshot snap;
    auto& out = snap.entries;
    // what a knob is now, corner by corner (the corners it has, or the knob itself when it has none)
    auto nowOf = [&] (int idx, float* c4)
    {
        for (const auto& e : cur.entries) if (e.idx == idx) { for (int c = 0; c < 4; ++c) c4[c] = e.c[c]; return; }
        const float v = modRanges[(size_t) idx].convertTo0to1 (apvts.getRawParameterValue (modIds[(size_t) idx])->load());
        for (int c = 0; c < 4; ++c) c4[c] = v;
    };
    // one knob: a new place drawn inside the character's limits, and each corner moves from where it is towards it,
    // as far as the sound's amount for that part allows (k: which sound of the pair; group -1: drawn freely)
    auto add = [&] (const juce::String& id, std::function<float()> draw, int k = -1, int group = -1)
    {
        const auto it = modIdxById.find (id.toStdString());
        if (it == modIdxById.end()) return -1;
        VecEntry e; e.idx = it->second;
        // keep it musical (after the listening of 18 Nov 2026: "fast, curly, not musical at all"): frequency shift and ring only
        // where metal is the point (Metal, Bells); no comb filter (its pitch has nothing to do with the key); gentle resonance and grit
        const bool metallic = character == 2 || character == 9;
        if ((id.startsWith ("fshift") || id.startsWith ("ring")) && ! metallic && k >= 0)
        {
            nowOf (e.idx, e.c);
            out.push_back (e);
            return (int) out.size() - 1;
        }
        auto musical = [&id, metallic] (float v)
        {
            if (id.startsWith ("sfiltype")) return metallic ? (v >= 0.5f && v < 0.75f ? v + 0.25f : v) : v * 0.49f;
            if (id.startsWith ("sfilres")) return juce::jmin (v, 0.5f);
            if (id.startsWith ("srcrough")) return juce::jmin (v, 0.25f);
            return v;
        };
        if (k < 0 || group < 0)
            for (auto& c : e.c) c = juce::jlimit (0.0f, 1.0f, musical (draw()));
        else
        {
            float now4[4];
            nowOf (e.idx, now4);
            const float a = amount[k][group];
            const float centre = musical (draw());
            for (int c = 0; c < 4; ++c)
            {
                const float target = musical (centre + (musical (draw()) - centre) * a);
                e.c[c] = juce::jlimit (0.0f, 1.0f, now4[c] + a * reach[c] * (target - now4[c]));
            }
        }
        if (id.startsWith ("sfiltype"))
        {   // the Type knob has four zones (Ladder, SVF, Comb, Formant); crossing from one to the next switches the filter at
            // once, a jump on the stick. The corners stay in the zone of the first corner
            const int zone = juce::jlimit (0, 3, (int) (e.c[0] * 3.999f));
            for (auto& c : e.c) c = juce::jlimit ((float) zone * 0.25f, (float) zone * 0.25f + 0.249f, c);
        }
        out.push_back (e);
        return (int) out.size() - 1;
    };
    int levelAt[2] = { -1, -1 };
    for (int k = 0; k < 2; ++k)
    {
        const int slot = pair * 2 + k;
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        {   // the level stays as it is: Make does not make a sound quieter or louder than its neighbours
            const auto it = modIdxById.find (("level" + sfx).toStdString());
            if (it != modIdxById.end())
            {
                VecEntry e; e.idx = it->second;
                nowOf (e.idx, e.c);
                out.push_back (e);
                levelAt[k] = (int) out.size() - 1;
            }
        }
        {   // grains are taken from a place where the sound is clearly present (not from a faded-out stretch)
            std::vector<float> loud;
            if (const auto smp = getSample (slot); smp != nullptr && smp->play.getNumSamples() > 4096)
            {
                const int n = smp->play.getNumSamples(), bins = 32, w = n / bins;
                float mx = 0.0f;
                std::vector<float> lv ((size_t) bins);
                for (int b2 = 0; b2 < bins; ++b2) { lv[(size_t) b2] = smp->play.getRMSLevel (0, b2 * w, w); mx = juce::jmax (mx, lv[(size_t) b2]); }
                for (int b2 = 0; b2 < bins; ++b2) if (lv[(size_t) b2] >= 0.35f * mx) loud.push_back (((float) b2 + 0.5f) / (float) bins);
            }
            // the stick slides the position between the corners: if the corners sit in two loud places with a quiet or different
            // stretch between them, the slide passes through it (a hole, a jump). So the corners share one unbroken loud stretch
            // (Glitch keeps its broken pieces from anywhere: jumping is its point)
            std::vector<std::vector<float>> runs;
            if (! loud.empty() && character != 7)
                for (size_t i = 0; i < loud.size(); ++i)
                {
                    if (i == 0 || loud[i] - loud[i - 1] > 1.5f / 32.0f) runs.emplace_back();
                    runs.back().push_back (loud[i]);
                }
            const auto it = modIdxById.find (("grpos" + sfx).toStdString());
            if (it != modIdxById.end())
            {   // positions are picked outright (no spreading around a centre: a place between two loud places may be silent)
                VecEntry e; e.idx = it->second;
                float now4[4];
                nowOf (e.idx, now4);
                const float a = amount[k][0];
                bool keep[4];
                for (int c = 0; c < 4; ++c) keep[c] = rnd.nextFloat() >= a * reach[c]; // with the rhythm amount, a corner may move
                // the stick slides the position between the corners: if the corners sit in two loud places with a quiet or
                // different stretch between them, the slide passes through it (a hole, a jump). So all four share one
                // unbroken loud stretch: the one most of the kept corners are in, or one picked by its length; a kept corner
                // outside it moves to the nearest place inside (Glitch keeps its broken pieces from anywhere: jumping is its point)
                std::vector<float> run;
                if (! runs.empty())
                {
                    auto inRun = [] (const std::vector<float>& r, float v) { return v >= r.front() - 1.0f / 64.0f && v <= r.back() + 1.0f / 64.0f; };
                    int best = -1, bestN = 0;
                    for (int r = 0; r < (int) runs.size(); ++r)
                    {
                        int n = 0; for (int c = 0; c < 4; ++c) if (keep[c] && inRun (runs[(size_t) r], now4[c])) ++n;
                        if (n > bestN) { bestN = n; best = r; }
                    }
                    if (best < 0)
                    {
                        int total = 0; for (auto& r : runs) total += (int) r.size();
                        int pick = rnd.nextInt (juce::jmax (1, total));
                        for (int r = 0; r < (int) runs.size(); ++r) { if (pick < (int) runs[(size_t) r].size()) { best = r; break; } pick -= (int) runs[(size_t) r].size(); }
                    }
                    run = runs[(size_t) juce::jmax (0, best)];
                    for (int c = 0; c < 4; ++c)
                        if (keep[c] && ! inRun (run, now4[c]))
                            now4[c] = juce::jlimit (run.front(), run.back(), now4[c]);
                }
                const auto& places = run.empty() ? loud : run;
                const float first = places.empty() ? rnd.nextFloat() : places[(size_t) rnd.nextInt ((int) places.size())];
                for (int c = 0; c < 4; ++c)
                {
                    if (keep[c]) { e.c[c] = now4[c]; continue; }
                    e.c[c] = (character == 7 || rnd.nextFloat() < a) ? (places.empty() ? rnd.nextFloat() : places[(size_t) rnd.nextInt ((int) places.size())]) : first;
                }
                out.push_back (e);
            }
        }
        add ("grscan" + sfx, [&rnd] { return 0.5f + 0.05f * rnd.nextFloat(); }, k, 2); // slow, so the grains stay near the chosen place
        for (int i = 0; i < tableLen; ++i)
        {
            const R r = table[i];
            add (r.id + sfx, [&rnd, r] { return rnd.nextFloat() < r.chance ? r.lo + (r.hi - r.lo) * rnd.nextFloat() : r.off; }, k, groupOf (r.id));
        }
        const bool hadCorners = ! cur.entries.empty();
        snap.grMode[k] = rhythmic ? 2 : 1;
        snap.grOct[k] = character == 10 && (k == 0 || rnd.nextFloat() < 0.5f) ? 1 : 0;
        snap.grArp[k] = character == 12 ? (hadCorners && cur.grArp[k] != 0 && rnd.nextFloat() >= amount[k][0] ? cur.grArp[k] : 1 + rnd.nextInt (9999)) : 0;
        // rhythm: a pattern with 6 to 11 of the 16 steps on, the first step always on; a new one only as often as the rhythm amount says
        int pat = 1;
        const int want = 6 + rnd.nextInt (6);
        for (int tries = 0; tries < 200 && juce::BigInteger (pat).countNumberOfSetBits() < want; ++tries) pat |= 1 << rnd.nextInt (16);
        const bool keepPat = hadCorners && cur.grMode[k] == 2 && rnd.nextFloat() >= amount[k][0];
        snap.grPat[k] = rhythmic ? (character == 12 ? 0xffff : (keepPat ? cur.grPat[k] : pat)) : juce::roundToInt (grP[(size_t) slot][12]->load());
    }
    snap.swell = character == 6;
    if (character == 11) // Melt: the Morph of the pair moves with the stick
        add (pair == 0 ? "morph12" : "morph34", [&rnd] { return 0.15f + 0.85f * rnd.nextFloat(); });
    const int crossAt = add (pair == 0 ? "xmorph12" : "xmorph34", [&rnd] { return rnd.nextFloat(); });
    bool splitOn = false;
    {   // Intermorph: on in all four corners or off in all four. Between "off" and "on" the split has to open up, and on the
        // stick that opening is squeezed into a short stretch next to the "off" corner (heard as a quick lurch); when it is
        // on everywhere, the stick only slides the crossover, an even glide
        const int at = add (pair == 0 ? "imorph12" : "imorph34", [&rnd] { return SlotMixer::kOpen + (0.6f - SlotMixer::kOpen) * rnd.nextFloat(); });
        if (at >= 0)
        {
            auto& e = out[(size_t) at];
            int on = 0; for (float c : e.c) if (c > 0.001f) ++on;
            const bool keepOn = on == 4 || (on > 0 && rnd.nextFloat() < 0.5f);
            splitOn = keepOn;
            for (auto& c : e.c) c = keepOn ? juce::jlimit (SlotMixer::kOpen, 1.0f, c > 0.001f ? c : SlotMixer::kOpen + (0.6f - SlotMixer::kOpen) * rnd.nextFloat()) : 0.0f;
        }
    if (crossAt >= 0 && splitOn)
        for (auto& c : out[(size_t) crossAt].c) // with Intermorph each sound carries only its part of the range (lows / highs):
            c = juce::jlimit (0.2f, 0.8f, c);   // a corner with one sound alone would be only lows or only highs
    }
    if (levelAt[0] >= 0 && levelAt[1] >= 0)
        for (int c = 0; c < 4; ++c)
        {   // the sound that Cross favours in this corner is clearly heard
            const bool second = crossAt >= 0 && out[(size_t) crossAt].c[c] > 0.5f;
            auto& lv = out[(size_t) levelAt[second ? 1 : 0]].c[c];
            if (lv < 0.75f) lv = 0.75f + 0.2f * rnd.nextFloat();
        }
    if ((parts & 7) != 7)
    {   // locked parts keep the corners they have
        auto partOf = [this, pair] (int idx)
        {
            const auto& id = modIds[(size_t) idx];
            if (id.startsWith ("xmorph") || id.startsWith ("imorph")) return 2;
            const juce::juce_wchar last = id.getLastCharacter();
            return ((last >= '2' && last <= '4') ? (int) (last - '1') : 0) - pair * 2;
        };
        if (! cur.entries.empty())
        {
            std::vector<VecEntry> merged;
            for (const auto& e : snap.entries) if ((parts >> partOf (e.idx)) & 1) merged.push_back (e);
            for (const auto& e : cur.entries) if (! ((parts >> partOf (e.idx)) & 1)) merged.push_back (e);
            snap.entries = merged;
            for (int k = 0; k < 2; ++k)
                if (! ((parts >> k) & 1)) { snap.grMode[k] = cur.grMode[k]; snap.grPat[k] = cur.grPat[k]; }
        }
    }
    {   // Make on one sound keeps the pair's own settings (Cross, Intermorph), but not the two that make a jump on the stick:
        // Intermorph off in some corners and on in others, or with it on, a corner that has one sound alone. Only these are
        // mended, as little as needed (the same rules as for a new pair)
        VecEntry* im = nullptr; VecEntry* cr = nullptr;
        for (auto& e : snap.entries)
        {
            const auto& id = modIds[(size_t) e.idx];
            if (id.startsWith ("imorph")) im = &e; else if (id.startsWith ("xmorph")) cr = &e;
        }
        bool split = false;
        if (im != nullptr)
        {
            int on = 0; for (float c : im->c) if (c > 0.001f) ++on;
            split = on >= 2;                                   // most corners decide: on stays on, a single "on" corner goes off
            for (auto& c : im->c) c = split ? juce::jmax (SlotMixer::kOpen, c) : 0.0f;
        }
        if (cr != nullptr && split)
            for (auto& c : cr->c) c = juce::jlimit (0.2f, 0.8f, c);
    }
    return snap;
    };

    // Make tries several drafts and keeps the one that promises the most audible change of colour between the corners,
    // without a corner where the sound gets very quiet or very dark. This is a quick estimate from the settings (no sound is
    // rendered); weights and limits are our own choices, not from a source.
    auto score = [this, pair] (const VecSnapshot& sn)
    {
        float lo[8], hi[8];
        for (int i = 0; i < 8; ++i) { lo[i] = 1.0f; hi[i] = 0.0f; }
        static const char* const keys[8] = { "sfilcut", "bodytone", "air", "bodyfocus", "sfiltype", "bodysize", "fshift", "blur" };
        static const float w[8] = { 2.0f, 1.5f, 1.0f, 0.6f, 0.6f, 0.5f, 0.5f, 0.4f };
        float level[2][4] = { { 1, 1, 1, 1 }, { 1, 1, 1, 1 } }, cross[4] = { 0.5f, 0.5f, 0.5f, 0.5f }, cutMin = 1.0f;
        for (const auto& e : sn.entries)
        {
            const auto& id = modIds[(size_t) e.idx];
            const juce::juce_wchar last = id.getLastCharacter();
            const int k = juce::jlimit (0, 1, ((last >= '2' && last <= '4') ? (int) (last - '1') : 0) - pair * 2);
            if (id.startsWith ("xmorph")) { for (int c = 0; c < 4; ++c) cross[c] = e.c[c]; continue; }
            if (id.startsWith ("level")) { for (int c = 0; c < 4; ++c) level[k][c] = e.c[c]; continue; }
            for (int i = 0; i < 8; ++i)
                if (id.startsWith (keys[i]))
                    for (int c = 0; c < 4; ++c) { lo[i] = juce::jmin (lo[i], e.c[c]); hi[i] = juce::jmax (hi[i], e.c[c]); if (i == 0) cutMin = juce::jmin (cutMin, e.c[c]); }
        }
        float v = 0.0f;
        for (int i = 0; i < 8; ++i) if (hi[i] >= lo[i]) v += w[i] * (hi[i] - lo[i]);
        float dbLo = 0.0f, dbHi = -100.0f;
        for (int c = 0; c < 4; ++c)
        {   // the louder sound of the corner, as Cross lets it through (equal power)
            const float th = cross[c] * juce::MathConstants<float>::halfPi;
            const float a = level[0][c] * level[0][c] * juce::jmin (1.0f, 1.41421356f * std::cos (th));
            const float b = level[1][c] * level[1][c] * juce::jmin (1.0f, 1.41421356f * std::sin (th));
            const float db = juce::Decibels::gainToDecibels (juce::jmax (a, b), -100.0f);
            dbLo = juce::jmin (dbLo, db); dbHi = juce::jmax (dbHi, db);
        }
        if (dbLo < -12.0f) v -= 0.25f * (-12.0f - dbLo);
        if (dbHi - dbLo > 10.0f) v -= 0.2f * (dbHi - dbLo - 10.0f);
        if (cutMin < 0.3f) v -= 2.0f * (0.3f - cutMin);
        return v;
    };
    if (draftsOut != nullptr)
    {   // Make listens: hand over the drafts, the better half by the quick estimate, to be heard and measured
        std::vector<std::pair<float, VecSnapshot>> all;
        for (int d = 0; d < draftCount * 2; ++d)
        {
            juce::Random rnd (master.nextInt());
            auto sn = gen (rnd);
            all.push_back ({ score (sn), std::move (sn) });
        }
        std::stable_sort (all.begin(), all.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
        draftsOut->clear();
        for (int d = 0; d < draftCount && d < (int) all.size(); ++d) draftsOut->push_back (std::move (all[(size_t) d].second));
        return;
    }
    const int drafts = seed != 0 ? 1 : 8;
    VecSnapshot best;
    float bestScore = -1.0e9f;
    for (int d = 0; d < drafts; ++d)
    {
        juce::Random rnd (master.nextInt());
        auto sn = gen (rnd);
        const float sc = score (sn);
        if (sc > bestScore) { bestScore = sc; best = std::move (sn); }
    }
    best.padDb.fill (0.0f); // (a Make that listens measures the pad and fills this in)
    applyVector (pair, best);
}

float NektarProcessor::modded (const std::atomic<float>* raw) const noexcept
{
    const float v = raw->load();
    const auto it = modIdxByRaw.find (raw);
    if (it == modIdxByRaw.end())
        return v;
    const size_t i = (size_t) it->second;
    if (! vecAct[i].load()) // the pair joystick does not set this knob
        return v;
    return modRanges[i].convertFrom0to1 (vecVal[i].load());
}

// Sources are updated once per block (-1..1, the envelope 0..1).
// LFO shape knob: 17 zones - sine, triangle, saw up, saw down, square, pulse 25%, sample and hold, smooth random,
// stairs (4 steps), half sine, exponential decay, double sine, then five shapes after the Hartmann Neuron manual p. 118
// (the steps in the stepped shapes are drawn from the picture, not specified): positive triangle, positive sine,
// positive square (these three run 0..1), stepped triangle, stepped saw down.
void NektarProcessor::updateMods (int numSamples)
{
    const double sr = hostRate.load() > 0.0 ? hostRate.load() : 44100.0;
    const double blockSec = (double) numSamples / sr;
    // pair joysticks: blend the four corners (the stick is smoothed over about 30 ms, so fast moves do not click)
    for (int pair = 0; pair < 2; ++pair)
    {
        if (! vecOn[(size_t) pair].load()) continue;
        const float k = 1.0f - std::exp (-(float) blockSec / 0.03f);
        auto& xs = vecXs[(size_t) pair]; auto& ys = vecYs[(size_t) pair];
        float tx = modded (vecXP[(size_t) pair]), ty = modded (vecYP[(size_t) pair]);
        const int shape = juce::roundToInt (vecShapeP[(size_t) pair]->load());
        if (shape > 0)
        {   // the stick travels the shape around the place where it was left (the Motion knob makes it slower or faster)
            const double mot = std::pow (2.0, 4.0 * ((double) juce::jlimit (0.0f, 1.0f, mSpeedP->load()) - 0.5));
            auto& ph = vecMovePhase[(size_t) pair];
            const double old = ph;
            const int sp = juce::roundToInt (vecSpeedP[(size_t) pair]->load());
            if (hostNow.has && apvts.getRawParameterValue ("tempolock")->load() > 0.5f)
            {   // locked to the beat: 32, 16, 8, 4 or 2 beats per round (halved / doubled by the Motion knob)
                static const double beats[motionshapes::speedSteps] { 32.0, 16.0, 8.0, 4.0, 2.0 };
                const double per = beats[juce::jlimit (0, motionshapes::speedSteps - 1, sp)] / std::pow (2.0, std::round (std::log2 (mot)));
                if (hostNow.hasPpq) ph = hostNow.ppq / per - std::floor (hostNow.ppq / per);
                else ph += blockSec * hostNow.bpm / 60.0 / per;
            }
            else
                ph += blockSec * mot / motionshapes::periodSeconds (sp);
            ph -= std::floor (ph);
            if (ph < old) for (auto& t : vecTg[(size_t) pair]) t = { vecMoveRnd.nextFloat() * 1.4f - 0.7f, vecMoveRnd.nextFloat() * 1.4f - 0.7f };
            static const int pick[4] = { 1, 3, 9, 10 }; // figure eight, spiral, Lissajous, flower
            const auto q = motionshapes::point (pick[juce::jlimit (0, 3, shape - 1)], (float) ph, vecPh[(size_t) pair].data(), vecTg[(size_t) pair].data());
            // the shape fills the whole round pad; the pad's disc is spread onto the square of the four corners
            const auto sq = motionshapes::discToSquare (q.x / 0.85f * 0.99f, -q.y / 0.85f * 0.99f);
            tx = 0.5f + 0.5f * sq.x; ty = 0.5f + 0.5f * sq.y;
        }
        if (pair == 1 && relation.load() == 2)
        {   // Shadow: joystick 2 follows joystick 1 half a second later, mirrored left - right
            const int d = juce::jlimit (1, (int) shadowX.size() - 1, (int) (0.5 / juce::jmax (1.0e-4, blockSec)));
            tx = 1.0f - shadowX[(size_t) ((shadowAt - d + (int) shadowX.size()) % (int) shadowX.size())];
            ty = shadowY[(size_t) ((shadowAt - d + (int) shadowY.size()) % (int) shadowY.size())];
        }
        xs += (juce::jlimit (0.0f, 1.0f, tx) - xs) * k;
        ys += (juce::jlimit (0.0f, 1.0f, ty) - ys) * k;
        if (pair == 0)
        {
            shadowAt = (shadowAt + 1) % (int) shadowX.size();
            shadowX[(size_t) shadowAt] = xs; shadowY[(size_t) shadowAt] = ys;
        }
        vecXShown[(size_t) pair].store (xs); vecYShown[(size_t) pair].store (ys);
        const juce::SpinLock::ScopedTryLockType lk (vecLock);
        if (! lk.isLocked()) continue;
        for (const auto& e : vecEntries[(size_t) pair])
        {
            const float a = e.c[0] + (e.c[1] - e.c[0]) * xs, b = e.c[2] + (e.c[3] - e.c[2]) * xs;
            vecVal[(size_t) e.idx].store (a + (b - a) * ys);
            vecAct[(size_t) e.idx].store (true);
        }
    }
}

void NektarProcessor::addModToState (juce::ValueTree& state)
{
    for (int i = state.getNumChildren(); --i >= 0;) // (older versions also kept modulation routes and a joystick path here)
        if (state.getChild (i).hasType ("MOD") || state.getChild (i).hasType ("JOYPATH"))
            state.removeChild (i, nullptr);
    // pair joysticks: the four corner values of every knob they set
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild (i).hasType ("VEC"))
            state.removeChild (i, nullptr);
    for (int pair = 0; pair < 2; ++pair)
    {
        const juce::SpinLock::ScopedLockType lk (vecLock);
        if (! vecOn[(size_t) pair].load() || vecEntries[(size_t) pair].empty()) continue;
        juce::ValueTree v ("VEC");
        v.setProperty ("pair", pair, nullptr);
        juce::String data;
        for (const auto& e : vecEntries[(size_t) pair])
            data << modIds[(size_t) e.idx] << ":" << juce::String (e.c[0], 4) << "," << juce::String (e.c[1], 4) << "," << juce::String (e.c[2], 4) << "," << juce::String (e.c[3], 4) << ";";
        v.setProperty ("corners", data, nullptr);
        juce::String pad;
        for (int i = 0; i < 9; ++i) pad << juce::String (padDbNow[(size_t) pair][(size_t) i].load(), 2) << (i < 8 ? "," : "");
        v.setProperty ("pad", pad, nullptr);
        state.appendChild (v, nullptr);
    }
}

void NektarProcessor::readModFromState (const juce::ValueTree& state)
{
    {   // pair joysticks
        std::array<std::vector<VecEntry>, 2> got;
        std::array<std::array<float, 9>, 2> pads {};
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            const auto c = state.getChild (i);
            if (! c.hasType ("VEC")) continue;
            const int pair = juce::jlimit (0, 1, (int) c.getProperty ("pair"));
            {
                juce::StringArray pv;
                pv.addTokens (c.getProperty ("pad").toString(), ",", "");
                if (pv.size() == 9) for (int i = 0; i < 9; ++i) pads[(size_t) pair][(size_t) i] = juce::jlimit (-24.0f, 24.0f, pv[i].getFloatValue());
            }
            juce::StringArray items;
            items.addTokens (c.getProperty ("corners").toString(), ";", "");
            for (const auto& item : items)
            {
                const auto it = modIdxById.find (item.upToFirstOccurrenceOf (":", false, false).toStdString());
                juce::StringArray vals;
                vals.addTokens (item.fromFirstOccurrenceOf (":", false, false), ",", "");
                if (it == modIdxById.end() || vals.size() != 4) continue;
                VecEntry e; e.idx = it->second;
                for (int k = 0; k < 4; ++k) e.c[k] = juce::jlimit (0.0f, 1.0f, vals[k].getFloatValue());
                got[(size_t) pair].push_back (e);
            }
        }
        for (int pair = 0; pair < 2; ++pair)
        {
            {
                const juce::SpinLock::ScopedLockType lk (vecLock);
                for (const auto& e : vecEntries[(size_t) pair]) vecAct[(size_t) e.idx].store (false);
                vecEntries[(size_t) pair] = got[(size_t) pair];
            }
            vecHistory[(size_t) pair].clear();
            for (int i = 0; i < 9; ++i) padDbNow[(size_t) pair][(size_t) i].store (pads[(size_t) pair][(size_t) i]);
            setVectorOn (pair, ! got[(size_t) pair].empty());
        }
    }
}

// ---- the main screen: Make / Back / random sounds / random space
juce::String NektarProcessor::makeAll (int target)
{
    int parts[2] = { 0, 0 };
    bool anySound = false;
    for (int pair = 0; pair < 2; ++pair)
    {
        const bool have = getSample (pair * 2) != nullptr || getSample (pair * 2 + 1) != nullptr;
        anySound = anySound || have;
        if (! have) continue;
        for (int k = 0; k < 2; ++k)
            if (! slotLock[(size_t) (pair * 2 + k)].load() && (target < 0 || target == pair * 2 + k)) parts[pair] |= 1 << k;
        if (! blendLock[(size_t) pair].load() && target < 0) parts[pair] |= 4;
    }
    if (! anySound) return "Put sounds into the circles first (Random sounds, or double-click a circle).";
    if (target >= 0 && getSample (target) == nullptr) return "Sound " + juce::String (target + 1) + " is empty.";
    if (target >= 0 && slotLock[(size_t) target].load()) return "Sound " + juce::String (target + 1) + " is locked. Unlock it to change it.";
    if (parts[0] == 0 && parts[1] == 0) return "Everything is locked. Unlock something to change it.";
    if (listenOnMake && ! judging.load())
    {   // Make listens: drafts now, heard and measured in the background, the best one is put in place when done (finishMake)
        if (judge == nullptr)
        {
            judge = std::make_unique<NektarProcessor>();
            judge->listenOnMake = false;
            judge->stopTimer();
            judge->setPlayConfigDetails (0, 2, hostRate.load() > 0.0 ? hostRate.load() : 48000.0, 512);
            judge->prepareToPlay (hostRate.load() > 0.0 ? hostRate.load() : 48000.0, 512);
        }
        JudgeJob job;
        job.target = target;
        for (int pair = 0; pair < 2; ++pair)
        {
            job.parts[pair] = parts[pair];
            if (parts[pair] != 0) newVectorCore (pair, 0, parts[pair], &job.drafts[pair], target >= 0 ? 4 : 3);
        }
        {
            const juce::ScopedLock sl (judgeLock);
            judgeJob = std::move (job);
        }
        judgeDone = false;
        judgeStep = 0;
        judgeSteps = (int) (job.drafts[0].size() + job.drafts[1].size());
        judging = true;
        struct T : juce::Thread { NektarProcessor& o; explicit T (NektarProcessor& p) : juce::Thread ("Make listens"), o (p) {} void run() override { o.runJudge(); } };
        if (judgeThread != nullptr) judgeThread->stopThread (4000);
        judgeThread = std::make_unique<T> (*this);
        judgeThread->startThread();
        return target >= 0 ? "Listening to versions of sound " + juce::String (target + 1) + "..." : juce::String ("Listening to versions of the free sounds...");
    }
    if (judging.load()) return "Still listening...";
    for (int pair = 0; pair < 2; ++pair)
    {
        auto& h = vecHistory[(size_t) pair];
        h.push_back (vectorNow (pair));
        if (h.size() > 24) h.erase (h.begin());
        if (parts[pair] != 0) newVector (pair, 0, parts[pair], false);
    }
    if (target >= 0)
    {
        const juce::String sfx = target == 0 ? juce::String() : juce::String (target + 1);
        auto pc = [this, &sfx] (const char* id) { return juce::String (juce::roundToInt (100.0f * apvts.getRawParameterValue (id + sfx)->load())); };
        return "Sound " + juce::String (target + 1) + " changed: rhythm " + pc ("mkrhy") + ", colour " + pc ("mkcol") + ", texture " + pc ("mktex") + ".";
    }
    return "Made. Hold a key and move the joysticks. Lock what you like, then Make again.";
}

bool NektarProcessor::backAll()
{
    bool any = false;
    for (int pair = 0; pair < 2; ++pair) any = backVector (pair) || any;
    return any;
}

// the corners of one sound are dropped and it plays plainly again (a new sound came into its circle)
void NektarProcessor::clearSlotVector (int slot)
{
    const int pair = slot / 2;
    {
        const juce::SpinLock::ScopedLockType lk (vecLock);
        auto& v = vecEntries[(size_t) pair];
        for (auto it = v.begin(); it != v.end();)
        {
            const auto& id = modIds[(size_t) it->idx];
            const juce::juce_wchar last = id.getLastCharacter();
            const bool blend = id.startsWith ("xmorph") || id.startsWith ("imorph");
            if (! blend && ((last >= '2' && last <= '4') ? (int) (last - '1') : 0) == slot) { vecAct[(size_t) it->idx].store (false); it = v.erase (it); }
            else ++it;
        }
    }
    const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
    if (auto* pa = apvts.getParameter ("grmode" + sfx)) pa->setValueNotifyingHost (0.0f);
    cleanChangedAt[(size_t) slot] = juce::Time::getMillisecondCounter();
    cleanDirty[(size_t) slot] = true;
}

juce::String NektarProcessor::randomSounds()
{
    juce::Array<juce::File> files;
    getLibraryFolder().findChildFiles (files, juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac");
    for (int i = files.size(); --i >= 0;)
        if (files.getReference (i).getSize() > 60 * 1024 * 1024 || files.getReference (i).getFileName().startsWithChar ('.')) files.remove (i);
    if (files.isEmpty()) return "No sounds found. Put WAV files into Music / Nektar Library.";
    {   // with the library map: the sounds of the chosen category nearest the cursor (a quarter of them, at least four)
        auto dots = libIndex.layout (libCategory);
        if (! dots.empty())
        {
            std::sort (dots.begin(), dots.end(), [this] (const LibraryIndex::Dot& a, const LibraryIndex::Dot& b)
            { return juce::Point<float> (a.x, a.y).getDistanceFrom (libCursor) < juce::Point<float> (b.x, b.y).getDistanceFrom (libCursor); });
            const int keep = juce::jmin ((int) dots.size(), juce::jmax (4, (int) dots.size() / 4));
            files.clear();
            for (int i = 0; i < keep; ++i) files.add (dots[(size_t) i].file);
        }
    }
    juce::StringArray used;
    for (int slot = 0; slot < kSlots; ++slot)
        if (slotLock[(size_t) slot].load())
            if (const auto s = getSample (slot)) used.add (juce::File (s->path).getFileName());
    int changed = 0;
    for (int slot = 0; slot < kSlots; ++slot)
    {
        if (slotLock[(size_t) slot].load()) continue;
        for (int tries = 0; tries < 12; ++tries)
        {
            const auto f = files[vecRnd.nextInt (files.size())];
            if (used.contains (f.getFileName()) && used.size() < files.size()) continue;
            if (loadSample (f, slot).isNotEmpty()) continue;
            if (auto* pk = apvts.getParameter (slot == 0 ? juce::String ("skey") : "skey" + juce::String (slot + 1))) pk->setValueNotifyingHost (0.0f);
            if (auto* pt = apvts.getParameter (slot == 0 ? juce::String ("stempo") : "stempo" + juce::String (slot + 1))) pt->setValueNotifyingHost (0.0f);
            used.add (f.getFileName());
            clearSlotVector (slot);
            ++changed;
            break;
        }
    }
    if (changed == 0) return "All four circles are locked.";
    return "New sounds. Play a key to hear them, then press Make.";
}

void NektarProcessor::newSpace()
{
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        auto set = [this, &sfx] (const char* id, float v) { if (auto* pa = apvts.getParameter (id + sfx)) pa->setValueNotifyingHost (pa->convertTo0to1 (v)); };
        set ("spaz", -180.0f + 360.0f * vecRnd.nextFloat());
        set ("spdist", 0.1f + 0.55f * vecRnd.nextFloat());
        const float r = vecRnd.nextFloat();
        set ("spmove", r < 0.25f ? 0.0f : (float) (1 + vecRnd.nextInt (5)));
        set ("spspeed", 0.1f + 0.4f * vecRnd.nextFloat());
    }
}

// ---- Harmony: the key of each sound and the shift that brings it into the chosen key or chord
// Our own design on top of the key finding (Tonality.h): each sound is moved by at most half an octave up or down.
// One key: a major sound is moved so its keynote is the Key; a minor sound so that its relative major is the Key (the
// same seven notes). Chord: the keynotes of the sounds with a key, in circle order, go to the root, fifth, third and
// root of a chord on the Key; the chord is minor when the first of them is minor.
bool NektarProcessor::keyOf (int slot, int& tonic, bool& minor) const
{
    const auto s = getSample (slot);
    if (s == nullptr) return false;
    const int choice = juce::roundToInt (apvts.getRawParameterValue (slot == 0 ? juce::String ("skey") : "skey" + juce::String (slot + 1))->load());
    if (choice == 1) return false;
    if (choice >= 2) { tonic = (choice - 2) / 2; minor = ((choice - 2) % 2) == 1; return true; }
    if (! s->keyKnown || ! s->tonality.tonal) return false;
    tonic = s->tonality.tonic; minor = s->tonality.minor;
    return true;
}

void NektarProcessor::applyHarmony()
{
    const int mode = juce::roundToInt (apvts.getRawParameterValue ("harmony")->load());
    const int key = juce::roundToInt (apvts.getRawParameterValue ("key")->load());
    int tonic[kSlots] {}; bool minor[kSlots] {}, has[kSlots] {};
    juce::String sig;
    for (int slot = 0; slot < kSlots; ++slot)
    {
        has[slot] = keyOf (slot, tonic[slot], minor[slot]);
        const auto s = getSample (slot);
        sig << (s != nullptr ? s->serial : -1) << (s != nullptr && s->keyKnown ? "k" : "") << ":" << (has[slot] ? tonic[slot] * 2 + (minor[slot] ? 1 : 0) : -1) << ";";
    }
    sig << mode << "/" << key;
    if (sig == harmonySig) return;
    const bool waiting = [&] { for (int slot = 0; slot < kSlots; ++slot) { const auto s = getSample (slot); if (s != nullptr && ! s->keyKnown) return true; } return false; }();
    harmonySig = sig;
    auto wrap = [] (int d) { d = ((d % 12) + 12) % 12; return d > 5 ? d - 12 : d; };
    int order = 0;
    const int chordDeg[4] = { 0, 7, 4, 0 };
    bool chordMinor = false;
    for (int slot = 0; slot < kSlots; ++slot) if (has[slot]) { chordMinor = minor[slot]; break; }
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const auto s = getSample (slot);
        if (s == nullptr || (! s->keyKnown && waiting && mode != 0)) continue;
        float shift = 0.0f;
        if (mode == 1 && has[slot])
            shift = (float) wrap (key - (minor[slot] ? tonic[slot] + 3 : tonic[slot]));
        else if (mode == 2 && has[slot])
        {
            int deg = chordDeg[juce::jmin (order, 3)];
            if (deg == 4 && chordMinor) deg = 3;
            shift = (float) wrap (key + deg - tonic[slot]);
            ++order;
        }
        else if (mode != 0 && ! has[slot])
            continue; // a sound without a key is left as it is
        harmonyShift[(size_t) slot] = shift;
        if (auto* pa = apvts.getParameter (slot == 0 ? juce::String ("pitch") : "pitch" + juce::String (slot + 1)))
            pa->setValueNotifyingHost (pa->convertTo0to1 (shift));
    }
    sendChangeMessage();
}

juce::String NektarProcessor::keyText (int slot) const
{
    const auto s = getSample (slot);
    if (s == nullptr) return {};
    if (! s->keyKnown && juce::roundToInt (apvts.getRawParameterValue (slot == 0 ? juce::String ("skey") : "skey" + juce::String (slot + 1))->load()) == 0)
        return "...";
    int t = 0; bool m = false;
    if (! keyOf (slot, t, m)) return "no key";
    juce::String r = juce::String (Tonality::noteName (t)) + (m ? " minor" : " major");
    const float sh = harmonyShift[(size_t) slot];
    if (juce::roundToInt (apvts.getRawParameterValue ("harmony")->load()) != 0 && std::abs (sh) > 0.01f)
        r << " " << (sh > 0 ? "+" : "") << juce::roundToInt (sh);
    return r;
}

void NektarProcessor::randomMovement (int pair)
{
    if (auto* pa = apvts.getParameter (pair == 0 ? "vec1shape" : "vec2shape")) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) (1 + vecRnd.nextInt (motionshapes::count))));
    if (auto* pa = apvts.getParameter (pair == 0 ? "vec1speed" : "vec2speed")) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) vecRnd.nextInt (4)));
}

// ---- Tempo lock (our own rules on top of TempoFind.h)
float NektarProcessor::tempoOf (int slot, const Sample* s) const
{
    if (s == nullptr || ! s->tempoKnown || s->tempo.bpm <= 0.0f) return 0.0f;
    const int fix = juce::roundToInt (apvts.getRawParameterValue (slot == 0 ? juce::String ("stempo") : "stempo" + juce::String (slot + 1))->load());
    if (fix == 1) return 0.0f;
    return s->tempo.bpm * (fix == 2 ? 0.5f : (fix == 3 ? 2.0f : 1.0f));
}

// how much faster the sound is read so that it follows the host tempo; the half or double tempo is used when it is nearer,
// so a loop is never read more than about 1.4 times faster or slower than it was recorded
double NektarProcessor::tempoRatioFor (int slot, const Sample* s, double hostBpm, bool hostHas) const
{
    if (! hostHas || apvts.getRawParameterValue ("tempolock")->load() < 0.5f) return 1.0;
    const float b = tempoOf (slot, s);
    if (b <= 0.0f || hostBpm <= 0.0) return 1.0;
    double r = hostBpm / (double) b;
    while (r > 1.4142) r *= 0.5;
    while (r < 0.7071) r *= 2.0;
    return r;
}

juce::String NektarProcessor::tempoText (int slot) const
{
    const auto s = getSample (slot);
    if (s == nullptr) return {};
    if (! s->tempoKnown) return "...";
    const float b = tempoOf (slot, s.get());
    return b > 0.0f ? juce::String (juce::roundToInt (b)) + " bpm" : juce::String ("no tempo");
}

// one round of a Space movement: 50 s .. 0.5 s by the Speed knob (times the Motion knob); with a host tempo and the lock on,
// the nearest whole number of beats in powers of two (1 .. 256), and in step with the host's bar position when it plays
double NektarProcessor::spaceAdvance (int slot, int numSamples, float motion)
{
    auto& ph = spPhase[(size_t) slot];
    const double old = ph;
    const double perSec = 50.0 * std::pow (0.01, (double) juce::jlimit (0.0f, 1.0f, modded (spSpeedP[(size_t) slot]))) / (double) motion;
    const double sr = hostRate.load() > 0.0 ? hostRate.load() : 44100.0;
    if (hostNow.has && apvts.getRawParameterValue ("tempolock")->load() > 0.5f)
    {
        const double per = juce::jlimit (1.0, 256.0, std::pow (2.0, std::round (std::log2 (perSec * hostNow.bpm / 60.0))));
        if (hostNow.hasPpq) ph = hostNow.ppq / per - std::floor (hostNow.ppq / per);
        else ph += (double) numSamples / sr * hostNow.bpm / 60.0 / per;
    }
    else
        ph += (double) numSamples / sr / perSec;
    ph -= std::floor (ph);
    if (ph < old) for (auto& t : spTg[(size_t) slot]) t = { vecMoveRnd.nextFloat() * 1.4f - 0.7f, vecMoveRnd.nextFloat() * 1.4f - 0.7f };
    return ph;
}

// Arp: the 16 steps' notes, from the seed that Make chose and the key of the sound: the major pentatonic above a major keynote,
// the minor pentatonic above a minor one (so the notes stay in the key that Harmony moved the sound to), up to an octave and a
// half; a sound with no key takes the major pentatonic. Our own rules.
void NektarProcessor::updateArpSteps()
{
    juce::String sig;
    int tonic[kSlots] {}; bool minor[kSlots] {};
    for (int sl = 0; sl < kSlots; ++sl)
    {
        if (! keyOf (sl, tonic[sl], minor[sl])) minor[sl] = false;
        sig << juce::roundToInt (grArpP[(size_t) sl]->load()) << (minor[sl] ? "m" : "M") << ";";
    }
    if (sig == arpSig) return;
    arpSig = sig;
    static const int majP[8] = { 0, 2, 4, 7, 9, 12, 14, 16 }, minP[8] = { 0, 3, 5, 7, 10, 12, 15, 17 };
    for (int sl = 0; sl < kSlots; ++sl)
    {
        const int seed = juce::roundToInt (grArpP[(size_t) sl]->load());
        juce::Random r (seed * 7919 + 13);
        int last = 0;
        for (int st = 0; st < 16; ++st)
        {   // mostly small steps up and down the scale, sometimes a leap, back home on the first step of each half bar
            if (st % 8 == 0) last = 0;
            else last = juce::jlimit (0, 7, last + (r.nextFloat() < 0.2f ? r.nextInt (5) - 2 : r.nextInt (3) - 1));
            arpSteps[(size_t) sl][(size_t) st] = seed > 0 ? (float) (minor[sl] ? minP[last] : majP[last]) : 0.0f;
        }
    }
}

// Harmony -> Chord also opens the key-tuned strings, so the chord sings through noisy sounds too (amount: our own choice, to be judged by ear)
float NektarProcessor::resMixNow() const noexcept
{
    const float own = modded (resMixP);
    const bool chord = juce::roundToInt (apvts.getRawParameterValue ("harmony")->load()) == 2;
    return juce::jlimit (0.0f, 1.0f, own + (chord ? chordRing : 0.0f));
}

// ---- Make listens
// The drafts of Make are played in a hidden copy of Nektar (same sounds, same settings), one sound alone, the stick in each
// corner, and two things are measured: loudness (RMS in dB) and brightness (spectral centroid, in octaves). The draft kept is
// the one whose corners stay near the sound's own loudness and inside the brightness of the other sounds, with no corner
// that falls silent, and with some difference between its corners. Using the spectral centroid as the main correlate of
// perceived brightness follows the timbre literature (e.g. Grey & Gordon 1978; McAdams et al. 1995); the limits and weights
// below are our own choices, to be judged by ear.
NektarProcessor::Heard NektarProcessor::listen (int slot, float x, float y, int pairOnly, float measSec)
{
    auto& j = *judge;
    const int pair = slot >= 0 ? slot / 2 : pairOnly;
    j.onlyPair = slot >= 0 ? -1 : pairOnly;
    if (auto* pa = j.apvts.getParameter (pair == 0 ? "vec1x" : "vec2x")) pa->setValueNotifyingHost (x);
    if (auto* pa = j.apvts.getParameter (pair == 0 ? "vec1y" : "vec2y")) pa->setValueNotifyingHost (y);
    j.setSolo (slot >= 0 ? slot : -1);
    for (auto& gp : j.grainPlayers) gp.restart(); // the same random grains for every measurement: drafts are compared on equal terms
    j.freeBeats = 0.0;                            // and the rhythm starts on the same beat
    {   // nothing left over from the measurement before (echoes of the space, ringing strings, filter states), and the stick
        // already where it is put (no glide): every measurement starts from silence, the same way
        const double rate = j.hostRate.load() > 0.0 ? j.hostRate.load() : 48000.0;
        j.mixer.prepare (rate, 512);
        for (auto& sf : j.slotFilter) sf.prepare (rate, 512);
        for (auto& sh : j.shifters) sh.prepare (rate);
        j.tilt.prepare (rate); j.macroTone.prepare (rate); j.resonator.prepare (rate);
        j.vecXs[(size_t) pair] = x; j.vecYs[(size_t) pair] = y;
        j.vecXShown[(size_t) pair] = x; j.vecYShown[(size_t) pair] = y;
    }
    const double sr = j.hostRate.load() > 0.0 ? j.hostRate.load() : 48000.0;
    const int bs = 512, warm = (int) (0.15 * sr / bs), meas = (int) (measSec * sr / bs), tail = (int) (juce::jmin (0.2f, 0.4f * measSec) * sr / bs);
    juce::AudioBuffer<float> b (2, bs);
    constexpr int order = 10, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) n), work ((size_t) (2 * n));
    for (int i = 0; i < n; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1));
    std::vector<float> mono;
    mono.reserve ((size_t) (meas * bs));
    double energy = 0.0; long count = 0;
    for (int i = 0; i < warm + meas; ++i)
    {
        b.clear();
        juce::MidiBuffer m;
        if (i == 0) { m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0); m.addEvent (juce::MidiMessage::noteOn (1, 67, 0.7f), 0); }
        j.processBlock (b, m);
        if (i < warm) continue;
        for (int k = 0; k < bs; ++k)
        {
            const float v = 0.5f * (b.getSample (0, k) + b.getSample (1, k));
            mono.push_back (v);
            energy += (double) b.getSample (0, k) * b.getSample (0, k) + (double) b.getSample (1, k) * b.getSample (1, k);
            count += 2;
        }
    }
    {   // let it go quiet before the next measurement
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 60), 0); off.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        for (int i = 0; i < tail; ++i) { b.clear(); juce::MidiBuffer none; j.processBlock (b, i == 0 ? off : none); }
    }
    Heard h;
    h.db = (float) (10.0 * std::log10 (energy / (double) juce::jmax (1L, count) + 1.0e-12));
    double num = 0.0, den = 0.0, flatSum = 0.0; int frames = 0;
    for (size_t start = 0; start + (size_t) n <= mono.size(); start += (size_t) n / 2)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < n; ++i) work[(size_t) i] = mono[start + (size_t) i] * win[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (work.data());
        double logSum = 0.0, linSum = 0.0, frameEnergy = 0.0;
        for (int bin = 1; bin < n / 2; ++bin)
        {
            const double p = (double) work[(size_t) bin] * work[(size_t) bin];
            num += p * (double) bin * sr / n; den += p;
            logSum += std::log (p + 1.0e-20); linSum += p; frameEnergy += p;
        }
        if (frameEnergy > 1.0e-12)
        {   // spectral flatness: geometric over arithmetic mean of the power spectrum (0 = tonal, 1 = noise; see Madhu 2009)
            const double bins = (double) (n / 2 - 1);
            flatSum += std::exp (logSum / bins) / (linSum / bins);
            ++frames;
        }
    }
    h.bright = den > 1.0e-18 ? (float) std::log2 (juce::jmax (20.0, num / den) / 20.0) : 0.0f;
    h.flat = frames > 0 ? (float) (flatSum / frames) : 0.0f;
    {   // how much the level moves: RMS in 10 ms steps, their spread against their mean
        const int step = (int) (0.01 * sr);
        std::vector<float> e;
        for (size_t st = 0; st + (size_t) step <= mono.size(); st += (size_t) step)
        {
            double a = 0.0; for (int i = 0; i < step; ++i) a += (double) mono[st + (size_t) i] * mono[st + (size_t) i];
            e.push_back ((float) std::sqrt (a / step));
        }
        double m = 0.0, v = 0.0; for (float q : e) m += q; m /= juce::jmax ((size_t) 1, e.size());
        for (float q : e) v += (q - m) * (q - m); v /= juce::jmax ((size_t) 1, e.size());
        h.mod = m > 1.0e-6 ? (float) (std::sqrt (v) / m) : 0.0f;
    }
    j.onlyPair = -1;
    return h;
}

void NektarProcessor::runJudge()
{
    auto& j = *judge;
    JudgeJob job;
    {
        const juce::ScopedLock sl (judgeLock);
        job = judgeJob;
    }
    auto exitNow = [this] { return judgeThread != nullptr && judgeThread->threadShouldExit(); };
    auto settle = [&j, &exitNow]
    {   // the copy bakes its colours; wait until it is done and has taken the results
        // done only when nothing is left: no baking, no finished bake waiting to be taken, no sound waiting for the audio,
        // no knob change waiting to be baked (under a heavy load these can lag behind, and the copy would be heard as before)
        juce::AudioBuffer<float> b (2, 512);
        auto busy = [&j]
        {
            if (j.cleanPool.getNumJobs() > 0) return true;
            for (int s = 0; s < kSlots; ++s) if (j.cleanDirty[(size_t) s].load()) return true;
            {
                const juce::SpinLock::ScopedLockType lk (j.sampleLock);
                for (bool p : j.samplePending) if (p) return true;
            }
            const juce::ScopedLock sl (j.resultLock);
            for (auto& r : j.results) if (r != nullptr) return true;
            return false;
        };
        for (int guard = 0; guard < 6000 && ! exitNow(); ++guard)
        {
            for (int s = 0; s < kSlots; ++s)
                if (j.cleanDirty[(size_t) s].load()) { j.cleanDirty[(size_t) s] = false; j.scheduleClean (s); }
            j.timerCallback();
            b.clear(); juce::MidiBuffer m; j.processBlock (b, m);
            if (! busy()) break;
            juce::Thread::sleep (3);
        }
        for (int i = 0; i < 2; ++i) { b.clear(); juce::MidiBuffer m; j.processBlock (b, m); }
    };
    // 1. the copy gets the same settings and sounds as now
    // (only what changed since the last Make: the sounds are copied and prepared once, the second Make is quick)
    bool anyNew = false;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (auto* q = j.apvts.getParameter (rp->paramID); q != nullptr && std::abs (q->getValue() - rp->getValue()) > 1.0e-6f)
                q->setValueNotifyingHost (rp->getValue());
    {   // the copy listens to the stick where it is put: no motion shapes, no moving places in the space, no Shadow or
        // Call and response (they move the stick or the level by themselves, and every measurement would depend on the moment)
        auto zero = [&j] (const juce::String& id) { if (auto* q = j.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (0.0f)); };
        zero ("vec1shape"); zero ("vec2shape");
        for (const char* id : { "spmove", "spmove2", "spmove3", "spmove4" }) zero (id);
        const int rel = juce::roundToInt (j.apvts.getRawParameterValue ("relation")->load());
        if (rel == 2 || rel == 3) zero ("relation");
    }
    for (int slot = 0; slot < kSlots; ++slot)
        if (const auto s = getSample (slot); s != nullptr && s->serial != judgeSeen[(size_t) slot])
        {   // a new sound in this slot (the same sound baked again keeps its serial)
            judgeSeen[(size_t) slot] = s->serial;
            j.publishSample (std::make_shared<Sample> (*s), slot);
            anyNew = true;
        }
    auto same = [] (const VecSnapshot& a, const VecSnapshot& b)
    {
        if (a.entries.size() != b.entries.size() || a.swell != b.swell) return false;
        for (int k = 0; k < 2; ++k)
            if (a.grMode[k] != b.grMode[k] || a.grPat[k] != b.grPat[k] || a.grOct[k] != b.grOct[k] || a.grArp[k] != b.grArp[k]) return false;
        for (size_t i = 0; i < a.entries.size(); ++i)
        {
            if (a.entries[i].idx != b.entries[i].idx) return false;
            for (int c = 0; c < 4; ++c) if (std::abs (a.entries[i].c[c] - b.entries[i].c[c]) > 1.0e-6f) return false;
        }
        return true;
    };
    for (int pair = 0; pair < 2; ++pair)
        if (const auto v = vectorNow (pair); ! same (v, j.vectorNow (pair)) || j.vecOn[(size_t) pair].load() != vecOn[(size_t) pair].load())
            j.applyVector (pair, v);
        else
            for (int slot = pair * 2; slot < pair * 2 + 2; ++slot) if (! anyNew) j.cleanDirty[(size_t) slot] = false; // already baked as it is
    bool mainReady = cleanPool.getNumJobs() == 0;
    for (int slot = 0; slot < kSlots; ++slot) mainReady = mainReady && ! cleanDirty[(size_t) slot].load();
    {
        const juce::ScopedLock sl (resultLock);
        for (auto& r : results) mainReady = mainReady && r == nullptr;
    }
    for (int slot = 0; slot < kSlots; ++slot)
        if (anyNew || j.cleanDirty[(size_t) slot].load())
        {
            const auto mine = getSample (slot);
            if (mainReady && mine != nullptr && mine->model != nullptr)
                j.adoptSample (slot, std::make_shared<Sample> (*mine)); // already prepared as things are now: no second baking
            else { j.cleanDirty[(size_t) slot] = false; j.scheduleClean (slot); }
        }
    settle();
    // 2. how the sounds are now: loudness of each, brightness of the ones that stay as they are
    const float x0[2] = { j.apvts.getRawParameterValue ("vec1x")->load(), j.apvts.getRawParameterValue ("vec2x")->load() };
    const float y0[2] = { j.apvts.getRawParameterValue ("vec1y")->load(), j.apvts.getRawParameterValue ("vec2y")->load() };
    Heard now[kSlots];
    bool changing[kSlots] = { false, false, false, false };
    for (int slot = 0; slot < kSlots; ++slot) changing[slot] = ((job.parts[slot / 2] >> (slot % 2)) & 1) != 0 && getSample (slot) != nullptr;
    float bLo = 1.0e9f, bHi = -1.0e9f;
    for (int slot = 0; slot < kSlots && ! exitNow(); ++slot)
    {
        if (getSample (slot) == nullptr) continue;
        now[slot] = listen (slot, j.apvts.getParameter (slot < 2 ? "vec1x" : "vec2x")->getValue(), j.apvts.getParameter (slot < 2 ? "vec1y" : "vec2y")->getValue());
        if (! changing[slot] && now[slot].db > -70.0f) { bLo = juce::jmin (bLo, now[slot].bright); bHi = juce::jmax (bHi, now[slot].bright); }
    }
    juce::ignoreUnused (x0, y0);
    // 3. every draft: each changing sound in its four corners
    for (int pair = 0; pair < 2 && ! exitNow(); ++pair)
    {
        float bestScore = -1.0e9f;
        for (int d = 0; d < (int) job.drafts[pair].size() && ! exitNow(); ++d)
        {
            judgeStep = judgeStep.load() + 1;
            {   // heard without Swell: it breathes on purpose and would make every measurement depend on the moment
                auto meas = job.drafts[pair][(size_t) d];
                meas.swell = false;
                meas.padDb.fill (0.0f);
                j.applyVector (pair, meas);
            }
            for (int slot = pair * 2; slot < pair * 2 + 2; ++slot) if (changing[slot]) j.scheduleClean (slot);
            for (int slot = pair * 2; slot < pair * 2 + 2; ++slot) j.cleanDirty[(size_t) slot] = false;
            settle();
            float sc = 0.0f;
            for (int slot = pair * 2; slot < pair * 2 + 2; ++slot)
            {
                if (! changing[slot]) continue;
                const float lo = bHi >= bLo ? bLo - 0.5f : now[slot].bright - 1.0f, hi = bHi >= bLo ? bHi + 0.5f : now[slot].bright + 1.0f;
                float bMin = 1.0e9f, bMax = -1.0e9f, mMin = 1.0e9f, mMax = -1.0e9f;
                for (int c = 0; c < 4; ++c)
                {
                    const auto h = listen (slot, (c & 1) ? 1.0f : 0.0f, (c & 2) ? 1.0f : 0.0f);
                    const float dl = std::abs (h.db - now[slot].db);
                    sc -= juce::jmax (0.0f, dl - 3.0f);                                  // keeps its loudness (within 3 dB)
                    if (h.db < now[slot].db - 20.0f || h.db < -70.0f) sc -= 10.0f;       // a corner that falls silent
                    sc -= 2.0f * (h.bright < lo ? lo - h.bright : (h.bright > hi ? h.bright - hi : 0.0f)); // fits the others' brightness
                    sc -= 8.0f * juce::jmax (0.0f, h.flat - now[slot].flat - 0.05f);   // stays as tonal as it was (not turned into noise)
                    bMin = juce::jmin (bMin, h.bright); bMax = juce::jmax (bMax, h.bright);
                    mMin = juce::jmin (mMin, h.mod); mMax = juce::jmax (mMax, h.mod);
                }
                // the corners should differ, in colour or in how they move (a Glitch differs in its motion more than its colour)
                const float differ = juce::jmax (bMax - bMin, 2.0f * (mMax - mMin));
                sc += 0.5f * juce::jmin (1.5f, differ);
                if (differ < 0.3f) sc -= 3.0f * (0.3f - differ) / 0.3f;
            }
            if (sc > bestScore) // (the grid only takes points away: a draft already behind the best cannot win, so it is not heard twice)
            {   // the pair as it is heard, on a 3x3 grid: the middle of each edge and of the pad should be a blend of the
                // corners next to it, not something else (no cliffs, no holes); the levels also give the pad's loudness map
                Heard g[9];
                for (int k = 0; k < 9 && ! exitNow(); ++k) g[k] = listen (-1, (float) (k % 3) * 0.5f, (float) (k / 3) * 0.5f, pair, 0.3f);
                auto blendOf = [&] (int m, std::initializer_list<int> cs)
                {
                    float lo = 1.0e9f, hi = -1.0e9f, dLo = 1.0e9f;
                    for (int c : cs) { lo = juce::jmin (lo, g[c].bright); hi = juce::jmax (hi, g[c].bright); dLo = juce::jmin (dLo, g[c].db); }
                    const float b = g[m].bright;
                    sc -= 2.0f * (b < lo - 0.3f ? lo - 0.3f - b : (b > hi + 0.3f ? b - hi - 0.3f : 0.0f));
                    if (g[m].db < dLo - 6.0f) sc -= 0.5f * (dLo - 6.0f - g[m].db);
                };
                blendOf (1, { 0, 2 }); blendOf (3, { 0, 6 }); blendOf (5, { 2, 8 }); blendOf (7, { 6, 8 }); blendOf (4, { 0, 2, 6, 8 });
                for (int c = 0; c < 4 && ! exitNow(); ++c)
                {   // a corner unlike the pad just next to it is heard only at the very corner: one small move and it is gone
                    // (a dull corner among bright ones turns bright at once). An eighth of the way in, it should still be itself
                    const float cx = (c & 1) ? 1.0f : 0.0f, cy = (c & 2) ? 1.0f : 0.0f;
                    const auto in = listen (-1, cx + (cx > 0.5f ? -0.125f : 0.125f), cy + (cy > 0.5f ? -0.125f : 0.125f), pair, 0.3f);
                    const auto& at = g[(c & 1) * 2 + (c & 2) * 3];
                    sc -= 2.0f * juce::jmax (0.0f, std::abs (in.bright - at.bright) - 0.4f);
                }
                float sum = 0.0f; int n = 0;
                for (auto& h : g) if (h.db > -70.0f) { sum += h.db; ++n; }
                const float target = n > 0 ? sum / (float) n : 0.0f;
                auto& pd = job.drafts[pair][(size_t) d].padDb;
                for (int k = 0; k < 9; ++k)
                {
                    pd[(size_t) k] = g[k].db > -70.0f ? juce::jlimit (-12.0f, 9.0f, target - g[k].db) : 0.0f;
                    if (g[k].db > -70.0f && target - g[k].db > 9.0f) sc -= 0.5f * (target - g[k].db - 9.0f); // a hole the map cannot fill
                }
            }
            if (sc > bestScore) { bestScore = sc; job.best[pair] = d; }
        }
        if (! job.drafts[pair].empty() && ! exitNow())
        {   // the copy keeps the chosen version (it becomes how things are), with the loudness map measured while judging
            const auto& chosen = job.drafts[pair][(size_t) job.best[pair]];
            j.applyVector (pair, chosen);
            for (int slot = pair * 2; slot < pair * 2 + 2; ++slot) { j.cleanDirty[(size_t) slot] = false; j.scheduleClean (slot); }
            const juce::ScopedLock sl (judgeLock);
            judgeJob.drafts[pair][(size_t) job.best[pair]].padDb = chosen.padDb;
        }
    }
    {
        const juce::ScopedLock sl (judgeLock);
        judgeJob.best[0] = job.best[0];
        judgeJob.best[1] = job.best[1];
    }
    if (! exitNow()) judgeDone = true;
    else judging = false;
}

void NektarProcessor::finishMake()
{
    JudgeJob job;
    {
        const juce::ScopedLock sl (judgeLock);
        job = judgeJob;
    }
    int heard = 0;
    for (int pair = 0; pair < 2; ++pair)
    {
        if (job.parts[pair] == 0 || job.drafts[pair].empty()) continue;
        auto& h = vecHistory[(size_t) pair];
        h.push_back (vectorNow (pair));
        if (h.size() > 24) h.erase (h.begin());
        applyVector (pair, job.drafts[pair][(size_t) juce::jlimit (0, (int) job.drafts[pair].size() - 1, job.best[pair])]);
        heard += (int) job.drafts[pair].size();
    }
    judging = false;
    juce::String msg;
    if (job.target >= 0)
    {
        const juce::String sfx = job.target == 0 ? juce::String() : juce::String (job.target + 1);
        auto pc = [this, &sfx] (const char* id) { return juce::String (juce::roundToInt (100.0f * apvts.getRawParameterValue (id + sfx)->load())); };
        msg = "Sound " + juce::String (job.target + 1) + " changed (heard " + juce::String (heard) + " versions): rhythm " + pc ("mkrhy") + ", colour " + pc ("mkcol") + ", texture " + pc ("mktex") + ".";
    }
    else
        msg = "Made (heard " + juce::String (heard) + " versions). Hold a key and move the joysticks. Lock what you like, then Make again.";
    const juce::ScopedLock sl (judgeLock);
    makeMessage = msg;
}
