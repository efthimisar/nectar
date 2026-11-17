#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include "SamplePlayer.h"
#include "Granular.h"
#include "Shifter.h"
#include "TiltFilter.h"
#include "SlotMixer.h"
#include "CharacterFilter.h"
#include <unordered_map>
#include <string>
#include "Resonator.h"
#include "MacroTone.h"
#include "MotionShapes.h"
#include "LibraryIndex.h"

class NektarProcessor : public juce::AudioProcessor,
                        public juce::ChangeBroadcaster,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    NektarProcessor();
    ~NektarProcessor() override;

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nektar"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static constexpr int kSlots = 4;
    // Loads a sound into a slot (0..3, message thread). Returns an error text, or an empty string on success.
    juce::String loadSample (const juce::File& file, int slot);

    // Recording from the microphone (standalone app only).
    static constexpr double kMaxRecordSeconds = 60.0;
    bool canRecord() const { return recordingAllowed; }
    static constexpr int kToLibrary = -2; // a recording or resample that becomes a new sound in the library
    void startRecording (int slot);  // the recording goes into this slot when it stops (-1 = room tone, kToLibrary = a new sound in the library)
    juce::String stopRecording();    // message thread: turns what was recorded into the sound of that slot; returns a message, if any
    bool isRecordingRoomTone() const { return recState.load() == 1 && recSlot.load() < 0; }
    bool hasRoomTone() const;
    bool isRecording() const { return recState.load() == 1; }
    int recordingSlot() const { return recSlot.load(); }
    // Resample: records the output of the synth itself (all slots, resonator, tilt) into a slot. Works in the plugin too.
    void startResample (int slot);
    bool isResampling() const { return recState.load() == 1 && recSource.load() == 1; }
    void recleanAll();               // runs the cleaning again for every slot (e.g. after a new room tone)
    bool recordingFinishedByLimit() const { return recState.load() == 2; }
    double getRecordedSeconds() const;
    float getInputLevel() const { return inputLevel.load(); }
    juce::String saveToLibrary (int slot); // writes the slot as a WAV into the library folder; returns a message
    static juce::File getLibraryFolder();

    // Modulation: every knob can follow a source (LFO 1, LFO 2, envelope, joystick ...) by an amount (-1..1 of the knob's range).
    enum ModSrc { kModNone = 0, kModLfo1, kModLfo2, /* 3..5 were the slicers: the numbers stay free so saved sounds keep their sources */ kModEnv = 6, kModJoyX, kModJoyY, kModVel, kModKey, kModRamp, kModCount };
    struct ModRoute { int src = 0; float amount = 0.0f; };
    ModRoute getModRoute (const juce::String& paramId) const;
    void setModRoute (const juce::String& paramId, ModRoute r);
    bool joyIsRecording() const { return joyRecState.load() == 1; }
    bool joyIsArmed() const { return joyRecState.load() == 2; }
    bool joyHasPath() const { return joyLen.load() > 0; }
    void joyClearPath() { joyLen.store (0); }
    // Pair joystick (pair 0 = samples 1+2, pair 1 = samples 3+4)
    // "New": four fresh corner settings in the chosen Character (0 = a random seed).
    // parts: bit 0 = the first sound of the pair, bit 1 = the second, bit 2 = how the two blend (Cross, Inter); a part that
    // is left out keeps the corners it has. history: remember the corners before, for Back.
    void newVector (int pair, int seed = 0, int parts = 7, bool history = true);
    // The main screen: a lock per sound and per pair blend; Make draws new corners for everything that is not locked.
    void setSlotLock (int slot, bool on) { slotLock[(size_t) slot].store (on); }
    bool slotLocked (int slot) const { return slotLock[(size_t) slot].load(); }
    void setBlendLock (int pair, bool on) { blendLock[(size_t) pair].store (on); }
    bool blendLocked (int pair) const { return blendLock[(size_t) pair].load(); }
    juce::String makeAll();          // returns a message
    bool backAll();                  // the corners before the last Make
    juce::String randomSounds();     // a sound from the library, picked at random, into every circle that is not locked
    void newSpace();                 // a random place and movement for each of the four sounds
    void randomMovement (int pair);  // a random shape and speed for a joystick
    // the library map: its index, the chosen category ("" = all) and the cursor (unit disc); Random sounds picks near it
    LibraryIndex libIndex;
    juce::String libCategory;
    juce::Point<float> libCursor { 0.0f, -0.78f };
    void rescanLibrary() { libIndex.scan (getLibraryFolder()); }
    juce::String keyText (int slot) const;
    juce::String tempoText (int slot) const;          // "124 bpm", "no tempo", "..."
    float tempoOf (int slot, const Sample* s) const;  // 0 = none
    double tempoRatioFor (int slot, const Sample* s, double hostBpm, bool hostHas) const;
    struct HostNow { double bpm = 120.0; bool has = false; double ppq = 0.0; bool hasPpq = false; };
    HostNow hostNow;                                   // audio thread
    std::atomic<int> relation { 0 };
    std::array<float, 2> pairGainNow { { 1.0f, 1.0f } };
    std::array<float, 4> pairGainWas { { 1.0f, 1.0f, 1.0f, 1.0f } };
    double freeBeats = 0.0;
    std::array<float, 256> shadowX {}, shadowY {};
    int shadowAt = 0;
    std::array<std::atomic<float>*, 4> grOctP {}, grArpP {};
    std::array<std::array<float, 16>, 4> arpSteps {};   // semitones per step (Arp), from the seed and the sound's key
    juce::String arpSig;
    void updateArpSteps();
public:
    // for the screen
    std::array<std::array<std::atomic<float>, 16>, 4> tapPos {};
    std::array<std::atomic<int>, 4> tapCount {}, tapStep {};
    std::array<std::atomic<float>, 4> slotLevel {};
    std::atomic<float> mixLevel { 0.0f }, beatNow { 0.0f };
    std::atomic<bool> beatOn { false };
    float playheadOf (int slot) const { return players[(size_t) slot].playhead.load(); }
    bool baking() { return cleanPool.getNumJobs() > 0; }
    float knobNow01 (const juce::String& id) const   // a knob as the joystick has it now (0..1 of its range)
    {
        const auto it = modIdxById.find (id.toStdString());
        if (it == modIdxById.end()) return 0.0f;
        if (vecAct[(size_t) it->second].load()) return vecVal[(size_t) it->second].load();
        if (auto* pa = apvts.getParameter (id)) return pa->getValue();
        return 0.0f;
    }
    double spaceAdvance (int slot, int numSamples, float motion);  // e.g. "G minor +3" (the key and the shift Harmony gives it), "no key", "..."
    bool keyOf (int slot, int& tonic, bool& minor) const;
    void setSolo (int slot) { soloSlot.store (slot); } // -1 = all
    int solo() const { return soloSlot.load(); }
    float spaceAzNow (int slot) const { return spAzShown[(size_t) slot].load(); }     // where the sound is right now (with its movement)
    float spaceDistNow (int slot) const { return spDistShown[(size_t) slot].load(); }
    bool backVector (int pair);                 // "Back": the corners that were there before the last New
    int vectorHistory (int pair) const { return (int) vecHistory[(size_t) pair].size(); }
    void pinVector (const juce::String& paramId, float value01); // a knob moved by hand: the same in all four corners from now on
    void setVectorOn (int pair, bool on);
    float vectorX (int pair) const { return vecXShown[(size_t) pair].load(); }
    float vectorY (int pair) const { return vecYShown[(size_t) pair].load(); }
    bool vectorOn (int pair) const { return vecOn[(size_t) pair].load(); }
    float modSourceNow (int src) const { return modShown[(size_t) juce::jlimit (0, (int) kModCount - 1, src)].load(); }

    // Presets: all knob values plus the four sounds, as one small file in "Nektar Library/Presets".
    // A sound that has no file of its own (a recording, a resample) is written into the library first.
    static juce::File getPresetFolder() { return getLibraryFolder().getChildFile ("Presets"); }
    // Snapshots A and B (for comparing two settings): all knobs, modulation routes and the joystick path, kept in memory
    // (not saved with the project, and the four sounds are not part of them).
    void storeSnapshot (int which);
    bool hasSnapshot (int which) const { return snapshots[(size_t) juce::jlimit (0, 1, which)].isValid(); }
    bool recallSnapshot (int which);
    juce::String savePreset (const juce::String& name);
    juce::String loadPreset (const juce::File& file);
    static juce::Array<juce::File> listPresets();
    juce::String renamePreset (const juce::File& file, const juce::String& newName);
    juce::String deletePreset (const juce::File& file); // to the Trash, so it can be got back
    static juce::StringArray factoryNames();
    juce::String loadFactory (int index); // settings only (the sounds in the slots stay)
    std::shared_ptr<const Sample> getSample (int slot) const;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState; // computer-keyboard notes from the UI

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publishSample (std::shared_ptr<Sample> s, int slot);
    void parameterChanged (const juce::String& id, float) override;
    void timerCallback() override;
    void scheduleClean (int slot);
    void installResult (int slot, std::shared_ptr<Sample> s);
    friend class CleanJob;

    std::array<SamplePlayer, kSlots> players;
    std::array<GrainPlayer, kSlots> grainPlayers;
    static constexpr int kGr = 13;
    static constexpr int kCol = 10; // per-sample colour/time: keeplength, stretch, blur, freeze, bodytone, bodyfocus, bodysize, srcrough, model, modelnoise
    std::array<std::array<std::atomic<float>*, kCol>, kSlots> colP {};
    std::array<std::array<std::atomic<float>*, 4>, kSlots> sfilP {}; // per-sample filter: type, cutoff, reso, mix
    std::array<CharacterFilter, kSlots> slotFilter;
    std::array<std::atomic<float>*, kSlots> airP {}, fshiftP {}, ringP {}, revP {};
    std::array<Shifter, kSlots> shifters;
    std::array<std::atomic<float>*, 2> pairMorphP {};
    std::atomic<float>* scaleP = nullptr;
    std::atomic<float>* keyP = nullptr;
    std::array<juce::int8, 128> scaleMap {}; // which note a held key was turned into (for its note-off)
    std::array<std::array<std::atomic<float>*, kGr>, kSlots> grP {};
    TiltFilter tilt;

    // modulation (audio thread reads the routes; the message thread writes them)
    float modded (const std::atomic<float>* raw) const noexcept; // value of a knob including its modulation
    void updateMods (int numSamples, const juce::MidiBuffer& midi);
    void buildModTable();
    void addModToState (juce::ValueTree& state);
    void readModFromState (const juce::ValueTree& state);
    // Pair joystick: each pair (1+2, 3+4) has four corner settings of its knobs; the stick blends between the four, live.
    struct VecEntry { int idx; float c[4]; };       // knob (index in the modulation table) and its four corner values (0..1 of its range)
    std::array<std::vector<VecEntry>, 2> vecEntries; // guarded by vecLock
    juce::SpinLock vecLock;
    struct VecSnapshot { std::vector<VecEntry> entries; int grMode[2] = { 0, 0 }; int grPat[2] = { 0, 0 }; int grOct[2] = { 0, 0 }; int grArp[2] = { 0, 0 }; bool swell = false; };
    std::array<std::vector<VecSnapshot>, 2> vecHistory; // message thread only
    VecSnapshot vectorNow (int pair);
    void applyVector (int pair, const VecSnapshot& snap);
    juce::Random vecRnd;
    std::array<std::atomic<bool>, 2> vecOn { { { false }, { false } } };
    std::vector<std::atomic<float>> vecVal;          // per knob: its blended value right now (0..1 of its range)
    std::vector<std::atomic<bool>> vecAct;           // per knob: the stick is in charge of it
    std::array<std::atomic<float>*, 2> vecXP {}, vecYP {};
    std::array<std::atomic<float>*, 2> vecShapeP {}, vecSpeedP {};
    std::array<double, 2> vecMovePhase {};
    std::array<std::array<float, 4>, 2> vecPh {};
    std::array<std::array<juce::Point<float>, 4>, 2> vecTg {};
    std::array<std::array<float, 4>, 4> spPh {};
    std::array<std::array<juce::Point<float>, 4>, 4> spTg {};
    juce::Random vecMoveRnd;
    std::array<float, 2> vecXs { { 0.5f, 0.5f } }, vecYs { { 0.5f, 0.5f } }; // smoothed stick position (audio thread)
    std::array<std::atomic<float>, 2> vecXShown { { { 0.5f }, { 0.5f } } }, vecYShown { { { 0.5f }, { 0.5f } } };
    std::vector<std::atomic<int>> routeSrc;
    std::vector<std::atomic<float>> routeAmt;
    std::vector<juce::NormalisableRange<float>> modRanges;
    std::unordered_map<const void*, int> modIdxByRaw;
    std::unordered_map<std::string, int> modIdxById;
    std::vector<juce::String> modIds;
    std::array<float, kModCount> modVal {};
    std::array<std::atomic<float>, kModCount> modShown {};
    std::atomic<float>* lfoRateP[2] {};
    std::atomic<float>* lfoShapeP[2] {};
    double lfoPhase[2] { 0.0, 0.0 };
    float lfoHold[2] { 0.0f, 0.0f };
    juce::Random lfoRng;
    float lfoPrev[2] { 0.0f, 0.0f }, lfoNext[2] { 0.0f, 0.0f };
    // envelope (ADSR) driven by the held keys
    std::array<juce::ValueTree, 2> snapshots;
    double lfoAge = 1.0e9;                 // seconds since the first key of a phrase (LFO delay / fade)
    std::atomic<float>* lfoDelayP = nullptr;
    std::atomic<float>* lfoFadeP = nullptr;
    std::atomic<float>* lfoSpiralP = nullptr;
    std::atomic<float>* masterP = nullptr;
    std::atomic<float>* widthP = nullptr;
    float lastWidth = 1.0f;
    float lastMaster = 1.0f;
    float rampLevel = 1.0f;                // 0 -> 1 over the Ramp time after a note starts with no key held
    std::atomic<float>* rampTimeP = nullptr;
    std::array<std::atomic<float>*, 2> chromoP {};
    std::atomic<float>* chromoMoveP = nullptr;
    std::atomic<float>* chromoBackP = nullptr;
    std::atomic<float>* chromoSpeedP = nullptr;
    std::atomic<float>* chromoDullP = nullptr;
    int heldNotes = 0;
    float lastVel = 0.0f, lastKey = 0.0f; // last note-on: velocity 0..1, key relative to C3 (-1..1 over +-3 octaves)
    int envStage = 0; // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
    float envLevel = 0.0f;
    std::atomic<float>* envP[4] {};
    // joystick: a path of (x, y) samples at 200 Hz, recorded while Rec is on and looped while Play is on
    static constexpr int kJoyMax = 200 * 30;
    std::vector<float> joyPathX, joyPathY;
    std::atomic<int> joyLen { 0 }, joyRecState { 0 };
    double joyAcc = 0.0, joyPlayPos = 0.0;
    bool joyWasRec = false;
    // armed recording / note-triggered playback (audio thread only; joyResetReq is set from the message thread)
    bool joyRunning = false, joyHaveHold = false;
    float joyArmX = 0.5f, joyArmY = 0.5f;
    std::atomic<bool> joyResetReq { false };
    std::atomic<float>* joyXP = nullptr; std::atomic<float>* joyYP = nullptr;
    std::atomic<float>* joyRecP = nullptr; std::atomic<float>* joyPlayP = nullptr;
    SlotMixer mixer;
    std::array<std::atomic<float>*, kSlots> panP {};
    std::array<std::atomic<float>*, kSlots> spAzP {}, spDistP {};
    std::atomic<float>* spMixP = nullptr; std::atomic<float>* spSpkP = nullptr;
    std::atomic<float>* spFarP = nullptr; std::atomic<float>* spBackP = nullptr;
    std::array<std::atomic<float>*, kSlots> spMoveP {}, spSpeedP {};
    std::array<double, kSlots> spPhase {};
    std::array<std::atomic<float>, kSlots> spAzShown {}, spDistShown {};
    std::atomic<float>* mSpeedP = nullptr; std::atomic<float>* mWeightP = nullptr; std::atomic<float>* mLightP = nullptr;
    MacroTone macroTone;
    std::array<std::atomic<bool>, kSlots> slotLock {};
    std::array<std::atomic<bool>, 2> blendLock {};
    std::atomic<int> soloSlot { -1 };
    int soloBeforeResample = -2; // the solo before a one-sound resample (-2 = none running)
    void clearSlotVector (int slot);
    void applyHarmony();
    juce::String harmonySig;
    std::array<float, kSlots> harmonyShift {};
    std::array<std::atomic<float>*, 2> imorphP {};
    std::array<std::atomic<float>*, 2> xmorphP {};
    Resonator resonator, resonatorB;
    juce::AudioBuffer<float> dryScratch, resBScratch;
    CharacterFilter charFilter;
    std::atomic<float>* filTypeP = nullptr;
    std::atomic<float>* filCutP = nullptr;
    std::atomic<float>* filResP = nullptr;
    std::atomic<float>* filMixP = nullptr;
    std::atomic<float>* resTuneP = nullptr;
    std::atomic<float>* resDecayP = nullptr;
    std::atomic<float>* resMatP = nullptr;
    std::atomic<float>* resMixP = nullptr;
    std::atomic<float>* resTuneBP = nullptr;
    std::atomic<float>* resMixBP = nullptr;
    juce::AudioFormatManager formats;
    mutable juce::SpinLock sampleLock;
    std::array<std::shared_ptr<const Sample>, kSlots> currentSamples; // owned by the UI side
    std::array<std::shared_ptr<const Sample>, kSlots> pendingSamples; // handed to the audio thread
    std::array<bool, kSlots> samplePending {};
    std::atomic<int> recSlot { 0 };
    std::array<std::atomic<float>*, kSlots> levelP {}, pitchP {}, tailP {};
    std::array<std::atomic<float>*, kSlots> noiseP {}, humP {}, lowCutP {};
    std::atomic<float>* brightP = nullptr;
    std::atomic<float>* blurP = nullptr;
    std::atomic<float>* freezeP = nullptr;
    std::atomic<float>* morphP = nullptr;
    std::atomic<float>* bodyToneP = nullptr;
    std::atomic<float>* bodyFocusP = nullptr;
    std::atomic<float>* bodySizeP = nullptr;
    std::atomic<float>* srcRoughP = nullptr;
    std::atomic<float>* modelP = nullptr;
    std::atomic<float>* modelNoiseP = nullptr;

    // cleaning (analysis, noise, hum, low cut) runs in a background thread; the result replaces the sound of the slot
    std::array<std::atomic<bool>, kSlots> cleanDirty {};
    std::array<std::atomic<juce::uint32>, kSlots> cleanChangedAt {};
    std::array<std::atomic<int>, kSlots> latestJob {};
    std::array<int, kSlots> serials {};                       // message thread only
    juce::CriticalSection resultLock;
    std::array<std::shared_ptr<Sample>, kSlots> results;      // finished by the background thread, waiting to be installed
    mutable juce::CriticalSection roomLock;
    SampleCleaner::NoiseProfile roomProfile;                  // recorded room tone (all slots), not saved with the project
    juce::ThreadPool cleanPool { 1 };
    std::atomic<double> hostRate { 44100.0 };

    bool recordingAllowed = false;
    juce::AudioBuffer<float> recBuffer;      // pre-allocated, filled by the audio thread
    std::atomic<int> recState { 0 };         // 0 idle, 1 recording, 2 full (waiting to be collected)
    std::atomic<int> recPos { 0 };
    std::atomic<int> rsSilent { -1 };         // resample: samples of silence since the sound ended (-1 = no sound heard yet)
    std::atomic<int> recSource { 0 };        // 0 microphone, 1 output of the synth (resample)
    std::atomic<int> recChannels { 1 };
    std::atomic<float> inputLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NektarProcessor)
};
