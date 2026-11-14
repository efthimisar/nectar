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

void NektarProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& pl : players)
        pl.prepare (sampleRate);
    keyboardState.reset();
    if (recordingAllowed && recState.load() == 0)
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
    p.loop = apvts.getRawParameterValue ("loop")->load() > 0.5f;
    p.keepLength = apvts.getRawParameterValue ("keeplength")->load();
    p.stretch = apvts.getRawParameterValue ("stretch")->load();

    // the voices are shared between the slots that have a sound (a fixed budget keeps the CPU load in check)
    int loaded = 0;
    for (auto& pl : players)
        if (pl.hasSample()) ++loaded;
    const int limit = juce::jmax (8, 32 / juce::jmax (1, loaded));

    buffer.clear();
    for (int slot = 0; slot < kSlots; ++slot)
    {
        auto& pl = players[(size_t) slot];
        if (! pl.hasSample())
            continue;
        p.level = levelP[(size_t) slot]->load();
        p.pitchSemis = pitchP[(size_t) slot]->load();
        p.releaseSec = tailP[(size_t) slot]->load();
        pl.setVoiceLimit (limit);
        pl.process (buffer, midi, p, false);
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
    recSlot.store (juce::jlimit (-1, kSlots - 1, slot));
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
    s->name = "Rec " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
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
