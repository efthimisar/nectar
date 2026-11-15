#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include "SamplePlayer.h"
#include "TiltFilter.h"
#include "SlotMixer.h"
#include <unordered_map>
#include <string>
#include "Resonator.h"

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
    void startRecording (int slot);  // the recording goes into this slot when it stops (-1 = room tone)
    juce::String stopRecording();    // message thread: turns what was recorded into the sound of that slot; returns a message, if any
    bool isRecordingRoomTone() const { return recState.load() == 1 && recSlot.load() < 0; }
    bool hasRoomTone() const;
    bool isRecording() const { return recState.load() == 1; }
    // Resample: records the output of the synth itself (all slots, resonator, tilt) into a slot. Works in the plugin too.
    void startResample (int slot);
    bool isResampling() const { return recState.load() == 1 && recSource.load() == 1; }
    void recleanAll();               // runs the cleaning again for every slot (e.g. after a new room tone)
    bool recordingFinishedByLimit() const { return recState.load() == 2; }
    double getRecordedSeconds() const;
    float getInputLevel() const { return inputLevel.load(); }
    juce::String saveToLibrary (int slot); // writes the slot as a WAV into the library folder; returns a message
    static juce::File getLibraryFolder();

    // Modulation: every knob can follow a source (LFO 1, LFO 2, Slicer) by an amount (-1..1 of the knob's range).
    enum ModSrc { kModNone = 0, kModLfo1, kModLfo2, kModSlicer, kModCount };
    struct ModRoute { int src = 0; float amount = 0.0f; };
    ModRoute getModRoute (const juce::String& paramId) const;
    void setModRoute (const juce::String& paramId, ModRoute r);
    float modSourceNow (int src) const { return modShown[(size_t) juce::jlimit (0, (int) kModCount - 1, src)].load(); }

    // Presets: all knob values plus the four sounds, as one small file in "Nektar Library/Presets".
    // A sound that has no file of its own (a recording, a resample) is written into the library first.
    static juce::File getPresetFolder() { return getLibraryFolder().getChildFile ("Presets"); }
    juce::String savePreset (const juce::String& name);
    juce::String loadPreset (const juce::File& file);
    static juce::Array<juce::File> listPresets();
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
    TiltFilter tilt;

    // modulation (audio thread reads the routes; the message thread writes them)
    float modded (const std::atomic<float>* raw) const noexcept; // value of a knob including its modulation
    void updateMods (int numSamples);
    void buildModTable();
    void addModToState (juce::ValueTree& state);
    void readModFromState (const juce::ValueTree& state);
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
    SlotMixer mixer;
    std::array<std::atomic<float>*, kSlots> panP {};
    std::array<std::atomic<float>*, 2> imorphP {};
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
    std::atomic<int> recSource { 0 };        // 0 microphone, 1 output of the synth (resample)
    std::atomic<int> recChannels { 1 };
    std::atomic<float> inputLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NektarProcessor)
};
