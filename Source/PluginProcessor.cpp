#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout NektarProcessor::createLayout()
{
    using juce::NormalisableRange;
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "level", 1 }, "Level",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "pitch", 1 }, "Pitch",
                                                         NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                         juce::AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "tail", 1 }, "Tail",
                                                         NormalisableRange<float> (0.01f, 5.0f, 0.001f, 0.4f), 0.25f,
                                                         juce::AudioParameterFloatAttributes().withLabel ("s")));
    l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "loop", 1 }, "Loop", false));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "keeplength", 1 }, "Keep length",
                                                         NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { "stretch", 1 }, "Stretch",
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
    recordingAllowed = juce::JUCEApplicationBase::isStandaloneApp();
    formats.registerBasicFormats();
}

void NektarProcessor::prepareToPlay (double sampleRate, int)
{
    player.prepare (sampleRate);
    keyboardState.reset();
    if (recordingAllowed && recState.load() == 0)
        recBuffer.setSize (2, (int) (sampleRate * kMaxRecordSeconds));

    // the sound must be converted to the new host rate
    hostRate = sampleRate;
    if (auto s = getSample())
        if (std::abs (s->playRate - sampleRate) > 0.5)
        {
            auto copy = std::make_shared<Sample> (*s);
            SamplePlayer::makePlayable (*copy, sampleRate);
            const juce::SpinLock::ScopedLockType lock (sampleLock);
            currentSample = copy;
            pendingSample = copy;
            samplePending = true;
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
        if (lock.isLocked() && samplePending)
        {
            player.setSample (pendingSample);
            samplePending = false;
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

            if (recState.load() == 1)
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

    SamplePlayer::Params p;
    p.level = apvts.getRawParameterValue ("level")->load();
    p.pitchSemis = apvts.getRawParameterValue ("pitch")->load();
    p.releaseSec = apvts.getRawParameterValue ("tail")->load();
    p.loop = apvts.getRawParameterValue ("loop")->load() > 0.5f;
    p.keepLength = apvts.getRawParameterValue ("keeplength")->load();
    p.stretch = apvts.getRawParameterValue ("stretch")->load();

    player.process (buffer, midi, p);
    midi.clear();
}

juce::String NektarProcessor::loadSample (const juce::File& file)
{
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

    publishSample (s);
    return {};
}

void NektarProcessor::publishSample (std::shared_ptr<Sample> s)
{
    SamplePlayer::makePlayable (*s, hostRate.load());
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        currentSample = s;
        pendingSample = s;
        samplePending = true;
    }
    sendChangeMessage();
}

// ---- microphone recording
void NektarProcessor::startRecording()
{
    if (! recordingAllowed || recState.load() == 1)
        return;
    recChannels.store (juce::jlimit (1, 2, getTotalNumInputChannels()));
    recBuffer.clear();
    recPos.store (0);
    recState.store (1);
}

double NektarProcessor::getRecordedSeconds() const
{
    const double sr = hostRate.load();
    return sr > 0.0 ? (double) recPos.load() / sr : 0.0;
}

void NektarProcessor::stopRecording()
{
    if (recState.load() == 0)
        return;
    recState.store (0);
    const int n = recPos.load();
    const int ch = recChannels.load();
    if (n < (int) (hostRate.load() * 0.1)) // shorter than 0.1 s: ignore
        return;

    auto s = std::make_shared<Sample>();
    s->rate = hostRate.load();
    s->name = "Rec " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    s->path = {};
    s->data.setSize (ch, n);
    for (int c = 0; c < ch; ++c)
        s->data.copyFrom (c, 0, recBuffer, c, 0, n);
    publishSample (s);
}

juce::File NektarProcessor::getLibraryFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Nektar Library");
}

juce::String NektarProcessor::saveToLibrary()
{
    auto s = getSample();
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

std::shared_ptr<const Sample> NektarProcessor::getSample() const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return currentSample;
}

void NektarProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto s = getSample())
        state.setProperty ("samplePath", s->path, nullptr);
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
            const auto path = state.getProperty ("samplePath").toString();
            if (path.isNotEmpty() && juce::File (path).existsAsFile())
                loadSample (juce::File (path));
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
