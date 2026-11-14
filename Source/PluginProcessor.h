#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include "SamplePlayer.h"

class NektarProcessor : public juce::AudioProcessor,
                        public juce::ChangeBroadcaster
{
public:
    NektarProcessor();
    ~NektarProcessor() override = default;

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

    // Loads a sound into slot 1 (message thread). Returns an error text, or an empty string on success.
    juce::String loadSample (const juce::File& file);

    // Recording from the microphone (standalone app only).
    static constexpr double kMaxRecordSeconds = 60.0;
    bool canRecord() const { return recordingAllowed; }
    void startRecording();
    void stopRecording();            // message thread: turns what was recorded into the sound in slot 1
    bool isRecording() const { return recState.load() == 1; }
    bool recordingFinishedByLimit() const { return recState.load() == 2; }
    double getRecordedSeconds() const;
    float getInputLevel() const { return inputLevel.load(); }
    juce::String saveToLibrary();    // writes slot 1 as a WAV into the library folder; returns a message
    static juce::File getLibraryFolder();
    std::shared_ptr<const Sample> getSample() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState; // computer-keyboard notes from the UI

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publishSample (std::shared_ptr<Sample> s);

    SamplePlayer player;
    juce::AudioFormatManager formats;
    mutable juce::SpinLock sampleLock;
    std::shared_ptr<const Sample> currentSample;   // owned by the UI side
    std::shared_ptr<const Sample> pendingSample;   // handed to the audio thread
    bool samplePending = false;
    std::atomic<double> hostRate { 44100.0 };

    bool recordingAllowed = false;
    juce::AudioBuffer<float> recBuffer;      // pre-allocated, filled by the audio thread
    std::atomic<int> recState { 0 };         // 0 idle, 1 recording, 2 full (waiting to be collected)
    std::atomic<int> recPos { 0 };
    std::atomic<int> recChannels { 1 };
    std::atomic<float> inputLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NektarProcessor)
};
