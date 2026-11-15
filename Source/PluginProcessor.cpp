#include "PluginProcessor.h"
#include "PluginEditor.h"

// Cleans one slot in the background: analysis once per sound, then low cut, hum and noise reduction.
class CleanJob : public juce::ThreadPoolJob
{
public:
    CleanJob (NektarProcessor& o, int slotIn, int gen, std::shared_ptr<const Sample> b, SampleCleaner::CleanParams p,
              SampleCleaner::NoiseProfile room, double rate)
        : juce::ThreadPoolJob ("clean"), owner (o), slot (slotIn), generation (gen), base (std::move (b)), params (p),
          roomCopy (std::move (room)), hostRate (rate) {}

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
        if (params.any())
        {
            const auto& prof = roomCopy.valid ? roomCopy : s->autoProfile;
            s->cleaned = SampleCleaner::clean (s->data, s->rate, params, prof, s->analysis.humHz, s->analysis.humHarmonics);
        }
        else
            s->cleaned.setSize (0, 0);
        if (shouldExit() || generation != owner.latestJob[(size_t) slot].load())
            return jobHasFinished;
        SamplePlayer::makePlayable (*s, hostRate);
        // the sine-wave description of what is played (kept when the sound itself did not change)
        if (! (! params.any() && base->model != nullptr && std::abs (base->modelRate - hostRate) < 0.5 && base->cleaned.getNumSamples() == 0))
        {
            auto model = SineModel::analyse (s->play, hostRate);
            if (model == nullptr || shouldExit() || generation != owner.latestJob[(size_t) slot].load())
                return jobHasFinished;
            s->model = std::move (model);
            s->modelRate = hostRate;
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
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "tail" + sfx, 1 }, "Tail" + nm,
                                                             NormalisableRange<float> (0.01f, 5.0f, 0.001f, 0.4f), 0.25f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("s")));
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "pan" + sfx, 1 }, "Pan" + nm,
                                                             NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        {
            static const float defAz[4] = { -60.0f, -20.0f, 20.0f, 60.0f };
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spaz" + sfx, 1 }, "Space direction" + nm,
                                                                 NormalisableRange<float> (-180.0f, 180.0f, 0.1f), defAz[slot],
                                                                 juce::AudioParameterFloatAttributes().withLabel ("deg")));
            l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spdist" + sfx, 1 }, "Space distance" + nm,
                                                                 NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));
        }
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "noise" + sfx, 1 }, "Noise" + nm,
                                                             NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "hum" + sfx, 1 }, "Hum" + nm,
                                                             NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "lowcut" + sfx, 1 }, "Low cut" + nm,
                                                             NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    }
    l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "loop", 1 }, "Loop", false));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "keeplength", 1 }, "Keep length",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "stretch", 1 }, "Stretch",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bright", 1 }, "Bright",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "blur", 1 }, "Blur",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "morph", 1 }, "Morph",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bodytone", 1 }, "Body tone",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bodyfocus", 1 }, "Body focus",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bodysize", 1 }, "Body size",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "model", 1 }, "Model (sine waves)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "modelnoise", 1 }, "Model noise (residual)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    for (int i = 0; i < 2; ++i)
    {
        const juce::String n (i + 1);
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "lfo" + n + "rate", 1 }, "LFO " + n + " rate",
                                                             NormalisableRange<float> (0.02f, 20.0f, 0.001f, 0.35f), i == 0 ? 0.5f : 0.2f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "lfo" + n + "shape", 1 }, "LFO " + n + " shape",
                                                             NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    }
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "slrate", 1 }, "Slicer rate",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "slpattern", 1 }, "Slicer pattern",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.14f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "slsmooth", 1 }, "Slicer smooth",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "sldepth", 1 }, "Slicer depth",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "filtype", 1 }, "Filter type (ladder, SVF, comb, formant)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "filcut", 1 }, "Filter cutoff",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "filres", 1 }, "Filter resonance",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "filmix", 1 }, "Filter mix (0 = off)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "envatk", 1 }, "Envelope attack",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.1f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "envdec", 1 }, "Envelope decay",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "envsus", 1 }, "Envelope sustain",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "envrel", 1 }, "Envelope release",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "joyx", 1 }, "Joystick X",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "joyy", 1 }, "Joystick Y",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "joyrec", 1 }, "Joystick record", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "joyplay", 1 }, "Joystick play path",
                                                          juce::StringArray { "Off", "1shot", "Repeat", "Free" }, 0));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "crbits", 1 }, "Crusher bits",
                                                         NormalisableRange<float> (3.0f, 16.0f, 0.01f), 16.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "crrate", 1 }, "Crusher rate reduction",
                                                         NormalisableRange<float> (1.0f, 32.0f, 0.01f, 0.4f), 1.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "crdither", 1 }, "Crusher dither",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "crmix", 1 }, "Crusher mix (0 = off)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spmix", 1 }, "Space amount (0 = plain pan)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "spspk", 1 }, "Space: speaker check mode", false));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spfar", 1 }, "Space: distance effect",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "spback", 1 }, "Space: darker behind",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "revsize", 1 }, "Reverb note (root pitch)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "revdecay", 1 }, "Reverb decay",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "revshimmer", 1 }, "Reverb shimmer",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "revmix", 1 }, "Reverb mix (0 = off)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "deltime", 1 }, "Delay time (tempo steps)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "delfb", 1 }, "Delay feedback",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "delpitch", 1 }, "Delay pitch per repeat (centre = 0)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "delmix", 1 }, "Delay mix (0 = off)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
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
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "restuneb", 1 }, "Resonator B tune",
                                                         NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 7.0f,
                                                         juce::AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "resmixb", 1 }, "Resonator B mix (0 = off)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "chromo12", 1 }, "Chromophonic 1<>2 (0 = off)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "chromo34", 1 }, "Chromophonic 3<>4 (0 = off)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "chromomove", 1 }, "Chromophonic move",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "ramptime", 1 }, "Ramp time (0.05 .. 20 s)",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "freeze", 1 }, "Freeze",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
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
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        levelP[(size_t) slot] = apvts.getRawParameterValue ("level" + sfx);
        pitchP[(size_t) slot] = apvts.getRawParameterValue ("pitch" + sfx);
        tailP[(size_t) slot] = apvts.getRawParameterValue ("tail" + sfx);
        panP[(size_t) slot] = apvts.getRawParameterValue ("pan" + sfx);
        spAzP[(size_t) slot] = apvts.getRawParameterValue ("spaz" + sfx);
        spDistP[(size_t) slot] = apvts.getRawParameterValue ("spdist" + sfx);
        noiseP[(size_t) slot] = apvts.getRawParameterValue ("noise" + sfx);
        humP[(size_t) slot] = apvts.getRawParameterValue ("hum" + sfx);
        lowCutP[(size_t) slot] = apvts.getRawParameterValue ("lowcut" + sfx);
        apvts.addParameterListener ("noise" + sfx, this);
        apvts.addParameterListener ("hum" + sfx, this);
        apvts.addParameterListener ("lowcut" + sfx, this);
        cleanDirty[(size_t) slot] = false;
        latestJob[(size_t) slot] = 0;
    }
    startTimer (100);
    brightP = apvts.getRawParameterValue ("bright");
    blurP = apvts.getRawParameterValue ("blur");
    freezeP = apvts.getRawParameterValue ("freeze");
    morphP = apvts.getRawParameterValue ("morph");
    bodyToneP = apvts.getRawParameterValue ("bodytone");
    bodyFocusP = apvts.getRawParameterValue ("bodyfocus");
    bodySizeP = apvts.getRawParameterValue ("bodysize");
    srcRoughP = apvts.getRawParameterValue ("srcrough");
    imorphP[0] = apvts.getRawParameterValue ("imorph12");
    imorphP[1] = apvts.getRawParameterValue ("imorph34");
    xmorphP[0] = apvts.getRawParameterValue ("xmorph12");
    xmorphP[1] = apvts.getRawParameterValue ("xmorph34");
    for (int i = 0; i < 2; ++i)
    {
        lfoRateP[i] = apvts.getRawParameterValue ("lfo" + juce::String (i + 1) + "rate");
        lfoShapeP[i] = apvts.getRawParameterValue ("lfo" + juce::String (i + 1) + "shape");
    }
    buildModTable();
    modelP = apvts.getRawParameterValue ("model");
    modelNoiseP = apvts.getRawParameterValue ("modelnoise");
    resTuneP = apvts.getRawParameterValue ("restune");
    resDecayP = apvts.getRawParameterValue ("resdecay");
    resMatP = apvts.getRawParameterValue ("resmat");
    resMixP = apvts.getRawParameterValue ("resmix");
    resTuneBP = apvts.getRawParameterValue ("restuneb");
    rampTimeP = apvts.getRawParameterValue ("ramptime");
    chromoP[0] = apvts.getRawParameterValue ("chromo12");
    chromoP[1] = apvts.getRawParameterValue ("chromo34");
    chromoMoveP = apvts.getRawParameterValue ("chromomove");
    resMixBP = apvts.getRawParameterValue ("resmixb");
    filTypeP = apvts.getRawParameterValue ("filtype");
    filCutP = apvts.getRawParameterValue ("filcut");
    filResP = apvts.getRawParameterValue ("filres");
    filMixP = apvts.getRawParameterValue ("filmix");
    revSizeP = apvts.getRawParameterValue ("revsize");
    revDecayP = apvts.getRawParameterValue ("revdecay");
    revShimP = apvts.getRawParameterValue ("revshimmer");
    revMixP = apvts.getRawParameterValue ("revmix");
    delTimeP = apvts.getRawParameterValue ("deltime");
    delFbP = apvts.getRawParameterValue ("delfb");
    delPitchP = apvts.getRawParameterValue ("delpitch");
    delMixP = apvts.getRawParameterValue ("delmix");
    slRateP = apvts.getRawParameterValue ("slrate");
    slPatP = apvts.getRawParameterValue ("slpattern");
    slSmoothP = apvts.getRawParameterValue ("slsmooth");
    slDepthP = apvts.getRawParameterValue ("sldepth");
    envP[0] = apvts.getRawParameterValue ("envatk");
    envP[1] = apvts.getRawParameterValue ("envdec");
    envP[2] = apvts.getRawParameterValue ("envsus");
    envP[3] = apvts.getRawParameterValue ("envrel");
    spMixP = apvts.getRawParameterValue ("spmix");
    spSpkP = apvts.getRawParameterValue ("spspk");
    spFarP = apvts.getRawParameterValue ("spfar");
    spBackP = apvts.getRawParameterValue ("spback");
    joyXP = apvts.getRawParameterValue ("joyx");
    joyYP = apvts.getRawParameterValue ("joyy");
    joyRecP = apvts.getRawParameterValue ("joyrec");
    joyPlayP = apvts.getRawParameterValue ("joyplay");
    crBitsP = apvts.getRawParameterValue ("crbits");
    crRateP = apvts.getRawParameterValue ("crrate");
    crDitherP = apvts.getRawParameterValue ("crdither");
    crMixP = apvts.getRawParameterValue ("crmix");
    joyPathX.assign ((size_t) kJoyMax, 0.5f);
    joyPathY.assign ((size_t) kJoyMax, 0.5f);
    recordingAllowed = juce::JUCEApplicationBase::isStandaloneApp();
    formats.registerBasicFormats();
}

NektarProcessor::~NektarProcessor()
{
    stopTimer();
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const juce::String sfx = slot == 0 ? juce::String() : juce::String (slot + 1);
        apvts.removeParameterListener ("noise" + sfx, this);
        apvts.removeParameterListener ("hum" + sfx, this);
        apvts.removeParameterListener ("lowcut" + sfx, this);
    }
    cleanPool.removeAllJobs (true, 20000);
}

void NektarProcessor::parameterChanged (const juce::String& id, float)
{
    const juce::juce_wchar last = id.getLastCharacter();
    const int slot = (last >= '2' && last <= '4') ? (int) (last - '1') : 0;
    cleanChangedAt[(size_t) slot] = juce::Time::getMillisecondCounter();
    cleanDirty[(size_t) slot] = true;
}

void NektarProcessor::timerCallback()
{
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
    SampleCleaner::CleanParams cp;
    cp.noise = noiseP[(size_t) slot]->load();
    cp.hum = humP[(size_t) slot]->load();
    cp.lowCut = lowCutP[(size_t) slot]->load();
    SampleCleaner::NoiseProfile room;
    {
        const juce::ScopedLock sl (roomLock);
        room = roomProfile;
    }
    const int gen = ++latestJob[(size_t) slot];
    cleanPool.addJob (new CleanJob (*this, slot, gen, base, cp, std::move (room), hostRate.load()), true);
}

void NektarProcessor::installResult (int slot, std::shared_ptr<Sample> s)
{
    if (s->serial != serials[(size_t) slot])
        return; // another sound was loaded in the meantime
    if (std::abs (s->playRate - hostRate.load()) > 0.5)
        s->serial = ++serials[(size_t) slot];
    SamplePlayer::makePlayable (*s, hostRate.load());
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        currentSamples[(size_t) slot] = s;
        pendingSamples[(size_t) slot] = s;
        samplePending[(size_t) slot] = true;
    }
    sendChangeMessage();
}

void NektarProcessor::recleanAll()
{
    for (int slot = 0; slot < kSlots; ++slot)
        scheduleClean (slot);
}

bool NektarProcessor::hasRoomTone() const
{
    const juce::ScopedLock sl (roomLock);
    return roomProfile.valid;
}

void NektarProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mixer.prepare (sampleRate, samplesPerBlock);
    for (auto& pl : players)
        pl.prepare (sampleRate);
    tilt.prepare (sampleRate);
    resonator.prepare (sampleRate);
    resonatorB.prepare (sampleRate);
    dryScratch.setSize (2, juce::jmax (samplesPerBlock, 8192));
    resBScratch.setSize (2, juce::jmax (samplesPerBlock, 8192));
    slicer.prepare (sampleRate);
    crusher.prepare (sampleRate);
    envStage = 0; envLevel = 0.0f; heldNotes = 0; rampLevel = 1.0f;
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    charFilter.prepare (sampleRate, samplesPerBlock);
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

    updateMods (buffer.getNumSamples(), midi);
    SamplePlayer::Params p;
    p.loop = apvts.getRawParameterValue ("loop")->load() > 0.5f;
    p.keepLength = modded (apvts.getRawParameterValue ("keeplength"));
    p.stretch = modded (apvts.getRawParameterValue ("stretch"));
    p.blur = modded (blurP);
    p.freeze = modded (freezeP);
    p.morph = modded (morphP);
    p.body.tone = modded (bodyToneP);
    p.body.focus = modded (bodyFocusP);
    p.body.size = modded (bodySizeP);
    p.body.rough = modded (srcRoughP);
    p.model = modded (modelP);
    p.modelNoise = modded (modelNoiseP);
    // Morph: slot 1 takes its colour from slot 2, slot 3 from slot 4 (slots 2 and 4 keep playing as they are)
    players[0].setPartner (players[1].getSample());
    players[2].setPartner (players[3].getSample());
    // Chromophonic: each slot of a pair takes the body of the other (both ways)
    players[0].setBodyPartner (players[1].getSample());
    players[1].setBodyPartner (players[0].getSample());
    players[2].setBodyPartner (players[3].getSample());
    players[3].setBodyPartner (players[2].getSample());
    const float baseModel = p.model;

    // the voices are shared between the slots that have a sound (a fixed budget keeps the CPU load in check)
    int loaded = 0;
    for (auto& pl : players)
        if (pl.hasSample()) ++loaded;
    const int limit = juce::jmax (8, 32 / juce::jmax (1, loaded));

    buffer.clear();
    std::array<bool, kSlots> loadedNow {};
    std::array<float, kSlots> pans {};
    for (int slot = 0; slot < kSlots; ++slot)
    {
        auto& pl = players[(size_t) slot];
        if (! pl.hasSample())
            continue;
        p.chromo = modded (chromoP[(size_t) (slot / 2)]);
        p.chromoMove = modded (chromoMoveP);
        p.model = juce::jmax (baseModel, p.chromo); // Chromophonic plays the sine model
        p.level = modded (levelP[(size_t) slot]);
        p.pitchSemis = modded (pitchP[(size_t) slot]);
        p.releaseSec = modded (tailP[(size_t) slot]);
        pl.setVoiceLimit (limit);
        auto view = mixer.scratchFor (slot, buffer.getNumSamples());
        pl.process (view, midi, p, false);
        loadedNow[(size_t) slot] = true;
        pans[(size_t) slot] = modded (panP[(size_t) slot]);
    }
    SlotMixer::Space space;
    space.mix = modded (spMixP);
    space.speakers = spSpkP->load() > 0.5f;
    space.far = modded (spFarP);
    space.back = modded (spBackP);
    for (int slot = 0; slot < kSlots; ++slot)
    {
        space.azDeg[(size_t) slot] = modded (spAzP[(size_t) slot]);
        space.dist[(size_t) slot] = modded (spDistP[(size_t) slot]);
    }
    mixer.setCross ({ modded (xmorphP[0]), modded (xmorphP[1]) });
    mixer.mix (buffer, buffer.getNumSamples(), loadedNow, pans, { modded (imorphP[0]), modded (imorphP[1]) }, space);

    // Resonator: strings tuned to the held keys, fed by the sound of the slots; decay 0..1 -> 0.1 .. 8 s
    // Resonator B runs beside A on the same input (parallel, "Mix"): out = A + B - dry, so each mix knob keeps its meaning.
    // B shares Decay and Material with A and has its own Tune and Mix.
    {
        const float decaySec = 0.1f * std::pow (80.0f, modded (resDecayP)), mat = modded (resMatP);
        const float mixB = modded (resMixBP);
        if (mixB > 0.005f)
        {
            const int n = buffer.getNumSamples(), chs = juce::jmin (2, buffer.getNumChannels());
            if (dryScratch.getNumSamples() < n) { dryScratch.setSize (2, n, false, false, true); resBScratch.setSize (2, n, false, false, true); }
            for (int c = 0; c < chs; ++c) { dryScratch.copyFrom (c, 0, buffer, c, 0, n); resBScratch.copyFrom (c, 0, buffer, c, 0, n); }
            juce::AudioBuffer<float> viewB (resBScratch.getArrayOfWritePointers(), chs, n); // only the first n samples
            resonatorB.process (viewB, midi, modded (resTuneBP), decaySec, mat, mixB);
            resonator.process (buffer, midi, modded (resTuneP), decaySec, mat, modded (resMixP));
            for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
                juce::FloatVectorOperations::add (buffer.getWritePointer (c), resBScratch.getReadPointer (c), buffer.getNumSamples()),
                juce::FloatVectorOperations::subtract (buffer.getWritePointer (c), dryScratch.getReadPointer (c), buffer.getNumSamples());
        }
        else
        {
            resonatorB.process (buffer, midi, 0.0f, decaySec, mat, 0.0f); // off: lets its strings go
            resonator.process (buffer, midi, modded (resTuneP), decaySec, mat, modded (resMixP));
        }
    }

    // Filter with character (ladder, SVF, comb, formant)
    charFilter.process (buffer, buffer.getNumSamples(), modded (filTypeP), modded (filCutP), modded (filResP), modded (filMixP));

    // Bitcrusher (bit depth, sample-rate reduction)
    crusher.process (buffer, buffer.getNumSamples(), modded (crBitsP), modded (crRateP), modded (crDitherP), modded (crMixP));

    // Reverb (pitched FDN with shimmer)
    reverb.process (buffer, buffer.getNumSamples(), modded (revSizeP), modded (revDecayP), modded (revShimP), modded (revMixP));

    // Delay (ping-pong, pitch per repeat), tempo-synced
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) bpm = *b;
        delay.process (buffer, buffer.getNumSamples(), modded (delTimeP), modded (delFbP), modded (delPitchP), modded (delMixP), bpm);
    }

    // Slicer: rhythmic gate at the host tempo (120 bpm when the host gives none); its gate is also a modulation source
    {
        double bpm = 120.0, ppq = -1.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) bpm = *b;
                if (pos->getIsPlaying())
                    if (auto q = pos->getPpqPosition()) ppq = *q;
            }
        const float g = slicer.process (buffer, buffer.getNumSamples(), modded (slRateP), modded (slPatP), modded (slSmoothP),
                                        modded (slDepthP), bpm, ppq);
        modVal[(size_t) kModSlicer] = g * 2.0f - 1.0f; // used by the next block
        modShown[(size_t) kModSlicer].store (g * 2.0f - 1.0f);
        // Slicer 2 and 3: stepped level sequences on the same clock (our own tables, not taken from any product)
        static const float seq3[16] = { 0.9f, -0.4f, 0.3f, -0.9f, 0.6f, 0.0f, -0.6f, 1.0f, -0.2f, 0.5f, -1.0f, 0.1f, 0.8f, -0.7f, 0.2f, -0.3f };
        const int st = slicer.currentStep();
        modVal[(size_t) kModSlicer2] = (float) st / 15.0f * 2.0f - 1.0f; // staircase over one bar of 16 steps
        modVal[(size_t) kModSlicer3] = seq3[st];
        modShown[(size_t) kModSlicer2].store (modVal[(size_t) kModSlicer2]);
        modShown[(size_t) kModSlicer3].store (modVal[(size_t) kModSlicer3]);
    }

    // Bright: tilt of the whole sound (0.5 = off)
    tilt.setTilt ((modded (brightP) - 0.5f) * 2.0f * 9.0f);
    tilt.process (buffer);

    // Safety: a soft knee above -0.9 dBFS-ish (0.9 linear) keeps reverb/delay/shimmer peaks below 1.0; identical below 0.9
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
    recSlot.store (juce::jlimit (-1, kSlots - 1, slot));
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
    recSlot.store (juce::jlimit (0, kSlots - 1, slot));
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
    if (recSlot.load() < 0) // room tone: only the noise profile is kept
    {
        if (n < (int) (hostRate.load() * 0.5))
            return "Room tone is too short (0.5 s or more).";
        juce::AudioBuffer<float> room (ch, n);
        for (int c = 0; c < ch; ++c)
            room.copyFrom (c, 0, recBuffer, c, 0, n);
        auto prof = SampleCleaner::estimateProfile (room, hostRate.load(), false);
        {
            const juce::ScopedLock sl (roomLock);
            roomProfile = prof;
        }
        recleanAll();
        return "Room tone saved.";
    }
    if (n < (int) (hostRate.load() * 0.1)) // shorter than 0.1 s: ignore
        return {};

    auto s = std::make_shared<Sample>();
    s->rate = hostRate.load();
    s->name = juce::String (recSource.load() == 1 ? "Resample " : "Rec ") + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    s->path = {};
    s->data.setSize (ch, n);
    for (int c = 0; c < ch; ++c)
        s->data.copyFrom (c, 0, recBuffer, c, 0, n);
    publishSample (s, recSlot.load());
    return {};
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
    if (juce::File (s->path).getParentDirectory() == dir)
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
        if (path.isNotEmpty() && ! juce::File (path).existsAsFile())
            missing += (missing.isEmpty() ? "" : ", ") + juce::String (slot + 1);
    }
    apvts.replaceState (state);
    readModFromState (state);
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const auto path = state.getProperty ("samplePath" + juce::String (slot + 1)).toString();
        if (path.isNotEmpty() && juce::File (path).existsAsFile())
            loadSample (juce::File (path), slot);
    }
    return missing.isEmpty() ? juce::String() : "Sound file missing for slot " + missing + ".";
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
        copyXmlToBinary (*xml, dest);
}

void NektarProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
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
    routeSrc = std::vector<std::atomic<int>> (modIds.size());
    routeAmt = std::vector<std::atomic<float>> (modIds.size());
    for (auto& a : routeSrc) a.store (0);
    for (auto& a : routeAmt) a.store (0.0f);
    for (auto& a : modShown) a.store (0.0f);
}

NektarProcessor::ModRoute NektarProcessor::getModRoute (const juce::String& id) const
{
    const auto it = modIdxById.find (id.toStdString());
    if (it == modIdxById.end())
        return {};
    return { routeSrc[(size_t) it->second].load(), routeAmt[(size_t) it->second].load() };
}

void NektarProcessor::setModRoute (const juce::String& id, ModRoute r)
{
    const auto it = modIdxById.find (id.toStdString());
    if (it == modIdxById.end())
        return;
    routeAmt[(size_t) it->second].store (juce::jlimit (-1.0f, 1.0f, r.amount));
    routeSrc[(size_t) it->second].store (juce::jlimit (0, (int) kModCount - 1, r.src));
}

float NektarProcessor::modded (const std::atomic<float>* raw) const noexcept
{
    const float v = raw->load();
    const auto it = modIdxByRaw.find (raw);
    if (it == modIdxByRaw.end())
        return v;
    const int src = routeSrc[(size_t) it->second].load();
    if (src <= 0)
        return v;
    const auto& r = modRanges[(size_t) it->second];
    const float n = juce::jlimit (0.0f, 1.0f, r.convertTo0to1 (v) + routeAmt[(size_t) it->second].load() * modVal[(size_t) src]);
    return r.convertFrom0to1 (n);
}

// Sources are updated once per block (-1..1, the envelope 0..1).
// LFO shape knob: 17 zones - sine, triangle, saw up, saw down, square, pulse 25%, sample and hold, smooth random,
// stairs (4 steps), half sine, exponential decay, double sine, then five shapes after the Hartmann Neuron manual p. 118
// (the steps in the stepped shapes are drawn from the picture, not specified): positive triangle, positive sine,
// positive square (these three run 0..1), stepped triangle, stepped saw down.
static float lfoShapeValue (int shape, float t, float hold, float prev, float next)
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    switch (shape)
    {
        case 0:  return std::sin (twoPi * t);
        case 1:  return 1.0f - 4.0f * std::abs (t - 0.5f);
        case 2:  return 2.0f * t - 1.0f;
        case 3:  return 1.0f - 2.0f * t;
        case 4:  return t < 0.5f ? 1.0f : -1.0f;
        case 5:  return t < 0.25f ? 1.0f : -1.0f;
        case 6:  return hold;
        case 7:  { const float c = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t); return prev + (next - prev) * c; }
        case 8:  return std::floor (t * 4.0f) / 1.5f - 1.0f;
        case 9:  return std::sin (juce::MathConstants<float>::pi * t) * 2.0f - 1.0f;
        case 10: return 2.0f * std::exp (-5.0f * t) - 1.0f;
        case 11: return 0.5f * std::sin (twoPi * t) + 0.5f * std::sin (2.0f * twoPi * t);
        case 12: return 1.0f - 2.0f * std::abs (t - 0.5f);
        case 13: return 0.5f + 0.5f * std::sin (twoPi * t);
        case 14: return t < 0.5f ? 1.0f : 0.0f;
        case 15: { const float u = 1.0f - 4.0f * std::abs (t - 0.5f); return std::round (u * 3.0f) / 3.0f; }
        default: return 1.0f - std::floor (t * 8.0f) / 3.5f;
    }
}

void NektarProcessor::updateMods (int numSamples, const juce::MidiBuffer& midi)
{
    const double sr = hostRate.load() > 0.0 ? hostRate.load() : 44100.0;
    const double blockSec = (double) numSamples / sr;
    for (int i = 0; i < 2; ++i)
    {
        const double prev = lfoPhase[i];
        double ph = prev + (double) lfoRateP[i]->load() * blockSec;
        const bool wrapped = ph >= 1.0;
        ph -= std::floor (ph);
        lfoPhase[i] = ph;
        if (wrapped)
        {
            lfoHold[i] = lfoRng.nextFloat() * 2.0f - 1.0f;
            lfoPrev[i] = lfoNext[i];
            lfoNext[i] = lfoHold[i];
        }
        const int shape = juce::jlimit (0, 16, (int) (lfoShapeP[i]->load() * 16.999f));
        const float v = lfoShapeValue (shape, (float) ph, lfoHold[i], lfoPrev[i], lfoNext[i]);
        modVal[(size_t) (kModLfo1 + i)] = v;
        modShown[(size_t) (kModLfo1 + i)].store (v);
    }

    // Envelope: gate = at least one key held; a new key restarts the attack from the current level
    bool retrigger = false;
    const int heldBefore = heldNotes;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())       { ++heldNotes; retrigger = true; lastVel = m.getFloatVelocity(); lastKey = juce::jlimit (-1.0f, 1.0f, (float) (m.getNoteNumber() - 60) / 36.0f); }
        else if (m.isNoteOff()) heldNotes = juce::jmax (0, heldNotes - 1);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) heldNotes = 0;
    }
    if (retrigger) envStage = 1;
    else if (heldNotes == 0 && envStage >= 1 && envStage <= 3) envStage = 4;
    // times: 0..1 -> 1 ms .. 5 s (attack, decay), 10 ms .. 8 s (release); exponential knob law
    const float atk = 0.001f * std::pow (5000.0f, envP[0]->load());
    const float dec = 0.001f * std::pow (5000.0f, envP[1]->load());
    const float sus = envP[2]->load();
    const float rel = 0.01f * std::pow (800.0f, envP[3]->load());
    const float bs = (float) blockSec;
    switch (envStage)
    {
        case 1: envLevel += bs / atk; if (envLevel >= 1.0f) { envLevel = 1.0f; envStage = 2; } break;
        case 2: envLevel -= (1.0f - sus) * bs / dec; if (envLevel <= sus) { envLevel = sus; envStage = 3; } break;
        case 3: envLevel = sus; break;
        case 4: envLevel -= bs / rel; if (envLevel <= 0.0f) { envLevel = 0.0f; envStage = 0; } break;
        default: break;
    }
    modVal[(size_t) kModEnv] = envLevel;
    modShown[(size_t) kModEnv].store (envLevel);
    // Ramp (the Neuron's dynamic crossmorph time, up to 20 s): restarts with the first key of a phrase, then stays at 1
    if (retrigger && heldBefore == 0) rampLevel = 0.0f;
    rampLevel = juce::jmin (1.0f, rampLevel + (float) blockSec / (0.05f * std::pow (400.0f, rampTimeP->load())));
    modVal[(size_t) kModRamp] = rampLevel;
    modShown[(size_t) kModRamp].store (rampLevel);
    modVal[(size_t) kModVel] = lastVel;
    modShown[(size_t) kModVel].store (lastVel);
    modVal[(size_t) kModKey] = lastKey;
    modShown[(size_t) kModKey].store (lastKey);

    // Joystick (after the Hartmann Neuron manual p. 89-92 and 41):
    // Rec = standby, the recording starts when the stick moves or a key is pressed (200 points a second, up to 30 s).
    // Play: 1shot = a note starts the path once, Repeat = it cycles while keys stay held, Free = loops all the time.
    // After a 1shot or after the keys are released the value stays where the path stopped.
    {
        const bool firstNote = retrigger && heldBefore == 0;
        if (joyResetReq.exchange (false))
        {
            joyRunning = false;
            joyHaveHold = false;
            joyWasRec = false;
            joyRecState.store (0);
        }
        const bool rec = joyRecP->load() > 0.5f;
        const int mode = juce::jlimit (0, 3, (int) std::lround (joyPlayP->load()));
        const float liveX = joyXP->load(), liveY = joyYP->load();
        float x = liveX, y = liveY;
        if (rec && ! joyWasRec)
        {
            joyRecState.store (2); // standby
            joyArmX = liveX; joyArmY = liveY;
            joyRunning = false; joyHaveHold = false;
        }
        if (rec)
        {
            if (joyRecState.load() == 2 && (retrigger || std::abs (liveX - joyArmX) + std::abs (liveY - joyArmY) > 0.002f))
            {
                joyLen.store (0);
                joyAcc = 1.0; // store the first point at once
                joyRecState.store (1);
            }
            if (joyRecState.load() == 1)
            {
                joyAcc += blockSec * 200.0;
                int len = joyLen.load();
                while (joyAcc >= 1.0 && len < kJoyMax)
                {
                    joyPathX[(size_t) len] = liveX;
                    joyPathY[(size_t) len] = liveY;
                    ++len;
                    joyAcc -= 1.0;
                }
                joyLen.store (len);
            }
        }
        else
        {
            joyRecState.store (0);
            const int len = joyLen.load();
            if (mode == 0 || len < 2)
            {
                joyRunning = false;
                joyHaveHold = false;
            }
            else if (mode == 3)
            {
                joyRunning = true;
                joyHaveHold = false;
                joyPlayPos += blockSec * 200.0;
                if (joyPlayPos >= (double) len) joyPlayPos = std::fmod (joyPlayPos, (double) len);
            }
            else
            {
                if (firstNote && ! joyRunning)
                {
                    joyRunning = true;
                    joyHaveHold = true;
                    joyPlayPos = 0.0;
                }
                else if (joyRunning)
                {
                    joyPlayPos += blockSec * 200.0;
                    if (joyPlayPos >= (double) (len - 1))
                    {
                        if (mode == 2 && heldNotes > 0) joyPlayPos = std::fmod (joyPlayPos, (double) (len - 1));
                        else { joyPlayPos = (double) (len - 1); joyRunning = false; }
                    }
                    else if (mode == 2 && heldNotes == 0)
                        joyRunning = false;
                }
            }
            if (len >= 2 && (joyRunning || joyHaveHold))
            {
                const int i0 = juce::jmin ((int) joyPlayPos, len - 1), i1 = juce::jmin (i0 + 1, len - 1);
                const float f = (float) (joyPlayPos - (double) i0);
                x = joyPathX[(size_t) i0] + f * (joyPathX[(size_t) i1] - joyPathX[(size_t) i0]);
                y = joyPathY[(size_t) i0] + f * (joyPathY[(size_t) i1] - joyPathY[(size_t) i0]);
            }
        }
        joyWasRec = rec;
        modVal[(size_t) kModJoyX] = x * 2.0f - 1.0f;
        modVal[(size_t) kModJoyY] = y * 2.0f - 1.0f;
        modShown[(size_t) kModJoyX].store (x * 2.0f - 1.0f);
        modShown[(size_t) kModJoyY].store (y * 2.0f - 1.0f);
    }
}

void NektarProcessor::addModToState (juce::ValueTree& state)
{
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild (i).hasType ("MOD") || state.getChild (i).hasType ("JOYPATH"))
            state.removeChild (i, nullptr);
    for (size_t i = 0; i < modIds.size(); ++i)
        if (routeSrc[i].load() > 0)
        {
            juce::ValueTree m ("MOD");
            m.setProperty ("id", modIds[i], nullptr);
            m.setProperty ("src", routeSrc[i].load(), nullptr);
            m.setProperty ("amount", (double) routeAmt[i].load(), nullptr);
            state.appendChild (m, nullptr);
        }
    // the recorded joystick path (16-bit values, base64)
    const int len = joyLen.load();
    if (len > 1)
    {
        juce::MemoryBlock bx ((size_t) len * 2), by ((size_t) len * 2);
        for (int i = 0; i < len; ++i)
        {
            const auto qx = (uint16_t) juce::roundToInt (juce::jlimit (0.0f, 1.0f, joyPathX[(size_t) i]) * 65535.0f);
            const auto qy = (uint16_t) juce::roundToInt (juce::jlimit (0.0f, 1.0f, joyPathY[(size_t) i]) * 65535.0f);
            bx[(size_t) i * 2] = (char) (qx & 0xff); bx[(size_t) i * 2 + 1] = (char) (qx >> 8);
            by[(size_t) i * 2] = (char) (qy & 0xff); by[(size_t) i * 2 + 1] = (char) (qy >> 8);
        }
        juce::ValueTree j ("JOYPATH");
        j.setProperty ("len", len, nullptr);
        j.setProperty ("x", bx.toBase64Encoding(), nullptr);
        j.setProperty ("y", by.toBase64Encoding(), nullptr);
        state.appendChild (j, nullptr);
    }
}

void NektarProcessor::readModFromState (const juce::ValueTree& state)
{
    for (size_t i = 0; i < modIds.size(); ++i)
    {
        routeSrc[i].store (0);
        routeAmt[i].store (0.0f);
    }
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto c = state.getChild (i);
        if (c.hasType ("MOD"))
            setModRoute (c.getProperty ("id").toString(), { (int) c.getProperty ("src"), (float) (double) c.getProperty ("amount") });
    }
    // joystick path: a preset without one clears the path; recording never starts by itself after loading
    joyLen.store (0);
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto c = state.getChild (i);
        if (! c.hasType ("JOYPATH"))
            continue;
        juce::MemoryBlock bx, by;
        const int len = juce::jlimit (0, kJoyMax, (int) c.getProperty ("len"));
        if (bx.fromBase64Encoding (c.getProperty ("x").toString()) && by.fromBase64Encoding (c.getProperty ("y").toString())
            && bx.getSize() >= (size_t) len * 2 && by.getSize() >= (size_t) len * 2)
        {
            for (int k = 0; k < len; ++k)
            {
                joyPathX[(size_t) k] = (float) ((uint8_t) bx[(size_t) k * 2] | ((uint8_t) bx[(size_t) k * 2 + 1] << 8)) / 65535.0f;
                joyPathY[(size_t) k] = (float) ((uint8_t) by[(size_t) k * 2] | ((uint8_t) by[(size_t) k * 2 + 1] << 8)) / 65535.0f;
            }
            joyLen.store (len);
        }
    }
    if (auto* r = apvts.getParameter ("joyrec"))
        r->setValueNotifyingHost (0.0f);
    joyResetReq.store (true);
}
