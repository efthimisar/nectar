#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include "SamplePlayer.h"
#include "Granular.h"
#include "Shifter.h"
#include "TiltFilter.h"
#include "BenchTone.h"
#include "TimbreBorrow.h"
#include "SlotMixer.h"
#include "CharacterFilter.h"
#include <unordered_map>
#include <string>
#include "Resonator.h"
#include "MacroTone.h"
#include "MotionShapes.h"
#include "LibraryIndex.h"
#include "FxRack.h"

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
    static constexpr float kPadBoost = 24.0f; // the most the pad's loudness map may lift a point (dB)
    // Loads a sound into a slot (0..3, message thread). Returns an error text, or an empty string on success.
    juce::String loadSample (const juce::File& file, int slot);

    // Recording from the microphone (standalone app only).
    static constexpr double kMaxRecordSeconds = 60.0;
    bool canRecord() const { return recordingAllowed; }
    static constexpr int kToLibrary = -2; // a recording or resample that becomes a new sound in the library
    void startRecording (int slot);  // the recording goes into this slot when it stops (kToLibrary = a new sound in the library)
    juce::String stopRecording();    // message thread: turns what was recorded into the sound of that slot; returns a message, if any
    bool isRecording() const { return recState.load() == 1; }
    int recordingSlot() const { return recSlot.load(); }
    // Resample: records the output of the synth itself (all slots, resonator, tilt) into a slot. Works in the plugin too.
    void startResample (int slot);
    bool isResampling() const { return recState.load() == 1 && recSource.load() == 1; }
    bool recordingFinishedByLimit() const { return recState.load() == 2; }
    double getRecordedSeconds() const;
    float getInputLevel() const { return inputLevel.load(); }
    juce::String saveToLibrary (int slot); // writes the slot as a WAV into the library folder; returns a message
    static juce::File getLibraryFolder();

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
    juce::String makeAll (int target = -1);          // returns a message
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
    std::array<std::array<std::atomic<float>, 9>, 2> padDbNow {};  // the pad's level correction now (audio thread reads)
    std::atomic<int> onlyPair { -1 };                                 // the hidden copy listens to one pair alone
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
    std::shared_ptr<int> aliveToken = std::make_shared<int> (0); // async work checks that Nektar is still there
    FxRack fxRack;                                     // two VST3 effects at the end, on the whole sound
    std::atomic<float> outPeak { 0.0f };               // the editor reads and lowers it
    float chordRing = 0.3f;                            // how much the strings ring with Harmony -> Chord (our own choice)
    float resMixNow() const noexcept;
    double hostBpmShown() const { return hostBpmAtomic.load(); }
    std::atomic<double> hostBpmAtomic { 0.0 };          // 0 = the host gives no tempo
    float playheadOf (int slot) const { return players[(size_t) slot].playhead.load(); }
    bool baking() { return cleanPool.getNumJobs() > 0 || judging.load(); }
    // Make listens: the versions are played in a hidden copy of Nektar and measured before one is chosen
    juce::String takeMakeMessage() { const juce::ScopedLock sl (judgeLock); auto m = makeMessage; makeMessage = {}; return m; }
    bool listenOnMake = true;   // off in quick tests
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
    float harmonyShiftOf (int slot) const { return harmonyShift[(size_t) juce::jlimit (0, kSlots - 1, slot)]; }
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
    void adoptSample (int slot, std::shared_ptr<Sample> s); // the hidden copy takes a sound the main one has already prepared (no baking again)
    friend class CleanJob;

    std::array<SamplePlayer, kSlots> players;
    std::array<GrainPlayer, kSlots> grainPlayers;
    static constexpr int kGr = 13;
    static constexpr int kCol = 5;  // per-sample colour: blur, body tone, body focus, body size, grit
    enum { kBlur = 0, kTone, kFocus, kSize, kGrit };
    static constexpr float kTail = 0.25f; // release of a note (s)
    std::array<std::array<std::atomic<float>*, kCol>, kSlots> colP {};
    std::array<std::array<std::atomic<float>*, 4>, kSlots> sfilP {}; // per-sample filter: type, cutoff, reso, mix
    std::array<CharacterFilter, kSlots> slotFilter;
    std::array<std::atomic<float>*, kSlots> airP {}, fshiftP {}, ringP {};
    std::array<Shifter, kSlots> shifters;
    std::array<std::array<TimbreBorrow, 2>, 2> borrow;                // Conversation, vertical: [pair][0: sound 1 takes the colour of 2, 1: the other way]
    std::array<BenchTone, kSlots> benchTone;                         // the bench: Light, Body, Air (live)
    std::array<std::array<std::atomic<float>*, 3>, kSlots> benchP {}; // blight, bbody, bair
    std::array<std::atomic<float>*, 2> pairMorphP {};
    std::atomic<float>* keyP = nullptr;
    std::array<std::array<std::atomic<float>*, kGr>, kSlots> grP {};
    TiltFilter tilt;

    // a knob as it sounds: its own value, or what the pair joystick makes of it
    float modded (const std::atomic<float>* raw) const noexcept;
    void updateMods (int numSamples);
    void buildModTable();
    void addModToState (juce::ValueTree& state);
    void readModFromState (const juce::ValueTree& state);
    // Pair joystick: each pair (1+2, 3+4) has four corner settings of its knobs; the stick blends between the four, live.
    struct VecEntry { int idx; float c[4]; };       // knob (index in the modulation table) and its four corner values (0..1 of its range)
    std::array<std::vector<VecEntry>, 2> vecEntries; // guarded by vecLock
    juce::SpinLock vecLock;
    struct VecSnapshot { std::vector<VecEntry> entries; int grMode[2] = { 0, 0 }; int grPat[2] = { 0, 0 }; int grOct[2] = { 0, 0 }; int grArp[2] = { 0, 0 }; bool swell = false;
                         std::array<float, 9> padDb {}; }; // padDb: level correction (dB) on a 3x3 grid of the pad, so it sounds equally loud everywhere
    std::array<std::vector<VecSnapshot>, 2> vecHistory; // message thread only
    // ---- Make listens (see PluginProcessor.cpp, "Make listens")
    struct Heard { float db = -100.0f, bright = 0.0f, flat = 0.0f, mod = 0.0f, loud = -100.0f; }; // loud: level of the sounding parts (gaps left out, as the ear does) // mod: how much the level moves (10 ms steps), std / mean // loudness (dB) and brightness (spectral centroid, octaves above 20 Hz)
    struct JudgeJob { std::vector<VecSnapshot> drafts[2]; int parts[2] = { 0, 0 }; int target = -1; std::vector<VecSnapshot> chosen; int best[2] = { 0, 0 }; };
    std::unique_ptr<NektarProcessor> judge;              // the hidden copy (made on the message thread, used by the judge thread)
    std::unique_ptr<juce::Thread> judgeThread;
    std::array<int, 4> judgeSeen { { -1, -1, -1, -1 } }; // the sounds (by serial) the copy already has
    std::atomic<bool> judging { false }, judgeDone { false };
public:
    std::atomic<int> judgeStep { 0 }, judgeSteps { 0 };   // how far Make has listened (shown under Make)
private:
    juce::CriticalSection judgeLock;
    JudgeJob judgeJob;
    juce::String makeMessage;
    void newVectorCore (int pair, int seed, int parts, std::vector<VecSnapshot>* draftsOut, int draftCount, int forceChar = -1);
    // Flavours: each sound has its own mix of the 13 characters, as many as wanted, each with an amount (its share of
    // the recipe). composeDrafts builds the drafts of every flavour in use and mixes each sound's knobs by its shares.
    void composeDrafts (int pair, int parts, std::vector<VecSnapshot>& out, int count);
    void bakeVector (int pair);                       // a draft's values into the knobs themselves
    std::vector<std::pair<juce::String, float>> knobsOfDraft (int pair, const VecSnapshot& snap) const;
    void rememberKnobs (int pair, const VecSnapshot& snap);
    std::array<std::vector<std::vector<std::pair<juce::String, float>>>, 2> knobHistory; // for Back
public:
    float stickX (int pair) const { return vecXShown[(size_t) pair].load(); }
    float convDev (int pair) const { return convDevNow[(size_t) pair].load(); }
    void startFromFlavour (int slot, int ch);         // a flavour as a place to start from: the bench's knobs move there
private:
    void updateCharKnobs();                           // message thread: the live offsets of the characters' three knobs
    std::array<std::vector<std::array<float, 4>>, 2> vecOff; // per entry and corner (guarded by vecLock)
public:
    static constexpr int kChars = 13, kCharKnobs = 3;
    std::vector<std::pair<int, float>> flavours (int slot) const; // the flavours of a sound in use, with their shares (summing to 1)
    int mainFlavour (int slot) const;                 // the flavour with the biggest share
    static const char* charKnobName (int ch, int k);  // the three knobs of each character (shown in the panel)
private:
    void runJudge();                                     // judge thread
    Heard listen (int slot, float x, float y, int pairOnly = -1, float measSec = 0.5f, float warmSec = 0.15f); // judge thread, on the copy: one sound alone (or with slot -1 one pair alone), the stick at x, y
    void finishMake();                                   // message thread
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
    std::vector<juce::NormalisableRange<float>> modRanges;
    std::unordered_map<const void*, int> modIdxByRaw;
    std::unordered_map<std::string, int> modIdxById;
    std::vector<juce::String> modIds;
    std::array<juce::ValueTree, 2> snapshots;
    std::atomic<float>* masterP = nullptr;
    float lastMaster = 1.0f;
    float limGain = 1.0f;                       // the peak limiter's gain now (audio thread)
    struct Conv { float env = 0.0f, peak = 0.0f, gainWas = 1.0f, dev = 0.4f; };
    std::array<std::atomic<float>, 2> convDevNow { 1.0f, 1.0f }; // how much the leader moves (the editor says when there is nothing to follow)
    std::array<Conv, 2> conv {};                 // the Conversation of each pair (audio thread)
    std::array<float, 4> convDens {};            // extra grain density of a following sound (audio thread)
    std::atomic<float> limGainShown { 1.0f };   // the lowest since the screen last looked
    SlotMixer mixer;
    std::array<std::atomic<float>*, kSlots> spAzP {}, spDistP {};
    std::atomic<float>* spSpkP = nullptr;
    std::array<std::atomic<float>*, kSlots> spMoveP {}, spSpeedP {};
    std::array<double, kSlots> spPhase {};
    std::array<double, kSlots> spPer {}, spOff {};  // the beats of one round now, and the phase gap left by a change of it (it fades out, so no jump)
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
    Resonator resonator;
    std::atomic<float>* resTuneP = nullptr;
    std::atomic<float>* resDecayP = nullptr;
    std::atomic<float>* resMatP = nullptr;
    std::atomic<float>* resMixP = nullptr;
    juce::AudioFormatManager formats;
    mutable juce::SpinLock sampleLock;
    std::array<std::shared_ptr<const Sample>, kSlots> currentSamples; // owned by the UI side
    std::array<std::shared_ptr<const Sample>, kSlots> pendingSamples; // handed to the audio thread
    std::array<bool, kSlots> samplePending {};
    std::atomic<int> recSlot { 0 };
    std::array<std::atomic<float>*, kSlots> levelP {}, pitchP {};
    std::atomic<float>* brightP = nullptr;

    // cleaning (analysis, noise, hum, low cut) runs in a background thread; the result replaces the sound of the slot
    std::array<std::atomic<bool>, kSlots> cleanDirty {};
    std::array<std::atomic<juce::uint32>, kSlots> cleanChangedAt {};
    std::array<std::atomic<int>, kSlots> latestJob {};
    std::array<int, kSlots> serials {};                       // message thread only
    juce::CriticalSection resultLock;
    std::array<std::shared_ptr<Sample>, kSlots> results;      // finished by the background thread, waiting to be installed
    juce::ThreadPool cleanPool { 2 }; // two sounds baked at the same time (each bakes its corners on cores of its own)
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
