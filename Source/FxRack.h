#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>

// Two effect plug-ins (VST3) at the end of Nektar, on the whole sound, one after the other.
// The plug-ins on this computer are found by scanning the usual VST3 folders (in the background, once, or when asked);
// the list is kept in a file so the scan is not repeated every time. Effects only (no instruments).
class FxRack
{
public:
    static constexpr int kSlots = 2;

    FxRack()
    {
        formats.addFormat (new juce::VST3PluginFormat());
        if (auto xml = juce::XmlDocument::parse (listFile()))
            known.recreateFromXml (*xml);
    }

    ~FxRack() = default;

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate; bs = blockSize;
        const juce::SpinLock::ScopedLockType lk (lock);
        for (auto& s : slots)
            if (s.inst != nullptr) { s.inst->releaseResources(); s.inst->setRateAndBufferSizeDetails (sr, bs); s.inst->prepareToPlay (sr, bs); }
    }

    // audio thread: the two effects in a row (a slot that is off, or being changed right now, is passed by)
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        const juce::SpinLock::ScopedTryLockType lk (lock);
        if (! lk.isLocked()) return;
        for (auto& s : slots)
            if (s.inst != nullptr && s.on.load() && buffer.getNumChannels() >= 2)
            {
                juce::MidiBuffer none;
                s.inst->processBlock (buffer, s.inst->acceptsMidi() ? midi : none);
            }
    }

    int latency() const
    {
        int l = 0;
        for (auto& s : slots) if (s.inst != nullptr && s.on.load()) l += s.inst->getLatencySamples();
        return l;
    }

    // message thread: put an effect into a slot (an empty description takes it out). Returns an error text, or empty.
    juce::String load (int i, const juce::PluginDescription* desc, const juce::MemoryBlock* state = nullptr)
    {
        std::unique_ptr<juce::AudioPluginInstance> inst;
        if (desc != nullptr)
        {
            juce::String err;
            inst = formats.createPluginInstance (*desc, sr, bs, err);
            if (inst == nullptr) return err.isNotEmpty() ? err : juce::String ("Could not open ") + desc->name;
            inst->enableAllBuses();
            inst->setPlayConfigDetails (2, 2, sr, bs);
            inst->prepareToPlay (sr, bs);
            if (state != nullptr && state->getSize() > 0) inst->setStateInformation (state->getData(), (int) state->getSize());
        }
        if (beforeChange) beforeChange (i); // a window showing the old effect closes first
        std::unique_ptr<juce::AudioPluginInstance> old;
        {
            const juce::SpinLock::ScopedLockType lk (lock);
            old = std::move (slots[(size_t) i].inst);
            slots[(size_t) i].inst = std::move (inst);
            slots[(size_t) i].on = true;
            slots[(size_t) i].desc = desc != nullptr ? *desc : juce::PluginDescription();
        }
        if (onChange) onChange();
        return {};
    }

    juce::AudioPluginInstance* instance (int i) const { return slots[(size_t) i].inst.get(); }
    juce::String name (int i) const { auto* p = instance (i); return p != nullptr ? p->getName() : juce::String(); }
    bool isOn (int i) const { return slots[(size_t) i].on.load(); }
    void setOn (int i, bool on) { slots[(size_t) i].on = on; if (onChange) onChange(); }

    // the effects found on this computer, by maker
    juce::Array<juce::PluginDescription> effects() const
    {
        juce::Array<juce::PluginDescription> r;
        for (const auto& d : known.getTypes())
            if (! d.isInstrument && d.name != "Nektar") r.add (d);
        std::sort (r.begin(), r.end(), [] (const auto& a, const auto& b)
                   { return a.manufacturerName.compareIgnoreCase (b.manufacturerName) != 0 ? a.manufacturerName.compareIgnoreCase (b.manufacturerName) < 0
                                                                                           : a.name.compareIgnoreCase (b.name) < 0; });
        return r;
    }

    bool scanning() const { return scanner != nullptr && scanner->running; }

    // looks for VST3 effects in the usual folders (message thread, step by step); done() is called on the message thread
    void scan (std::function<void()> done)
    {
        if (scanning()) return;
        scanName = {};
        scanProgress = 0.0f;
        scanner = std::make_unique<Scan> (*this, std::move (done));
    }

    // saved with the sound (presets and the host's project)
    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto x = std::make_unique<juce::XmlElement> ("FX");
        for (int i = 0; i < kSlots; ++i)
            if (auto* p = instance (i))
            {
                auto* e = x->createNewChildElement ("Slot");
                e->setAttribute ("index", i);
                e->setAttribute ("on", isOn (i));
                if (auto dx = slots[(size_t) i].desc.createXml()) e->addChildElement (dx.release()); // as it was found (the file is the plug-in's bundle)
                juce::MemoryBlock m;
                p->getStateInformation (m);
                e->setAttribute ("state", m.toBase64Encoding());
            }
        return x;
    }

    // message thread
    void fromXml (const juce::XmlElement* x)
    {
        for (int i = 0; i < kSlots; ++i) load (i, nullptr);
        if (x == nullptr) return;
        for (auto* e : x->getChildWithTagNameIterator ("Slot"))
        {
            const int i = juce::jlimit (0, kSlots - 1, e->getIntAttribute ("index"));
            juce::PluginDescription d;
            if (auto* dx = e->getChildByName ("PLUGIN"); dx == nullptr || ! d.loadFromXml (*dx)) continue;
            juce::MemoryBlock m;
            m.fromBase64Encoding (e->getStringAttribute ("state"));
            load (i, &d, &m);
            setOn (i, e->getBoolAttribute ("on", true));
        }
    }

    std::function<void()> onChange;          // the latency of Nektar follows the effects
    std::function<void (int)> beforeChange;  // message thread, before an effect is replaced or removed

private:
    struct Slot { std::unique_ptr<juce::AudioPluginInstance> inst; std::atomic<bool> on { true }; juce::PluginDescription desc; };
    // Many plug-ins on the Mac may only be opened on the main (message) thread: looking at them from another thread can crash.
    // So the scan runs on the message thread, one plug-in file per tick, and the screen stays alive in between.
    struct Scan : juce::Timer
    {
        Scan (FxRack& r, std::function<void()> d) : rack (r), done (std::move (d))
        {
            for (auto* f : rack.formats.getFormats())
                scanners.push_back (std::make_unique<juce::PluginDirectoryScanner> (rack.known, *f, f->getDefaultLocationsToSearch(), true, rack.pedalFile(), true));
            startTimer (30);
        }
        void timerCallback() override
        {
            stopTimer();
            while (current < scanners.size())
            {
                juce::String name;
                rack.scanName = scanners[current]->getNextPluginFileThatWillBeScanned();
                if (scanners[current]->scanNextFile (true, name)) { rack.scanProgress = scanners[current]->getProgress(); startTimer (5); return; }
                ++current;
            }
            if (auto xml = rack.known.createXml()) { rack.listFile().getParentDirectory().createDirectory(); xml->writeTo (rack.listFile()); }
            running = false;
            auto d = done;
            juce::MessageManager::callAsync ([d] { if (d) d(); });
        }
        FxRack& rack;
        std::function<void()> done;
        std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners;
        size_t current = 0;
        bool running = true;
    };
    friend struct Scan;
public:
    juce::String scanName;          // the plug-in being looked at right now
    float scanProgress = 0.0f;
private:

    static juce::File folder() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Prisma Audio").getChildFile ("Nektar"); }
    juce::File listFile() const { return folder().getChildFile ("fx-list.xml"); }
    juce::File pedalFile() const { return folder().getChildFile ("fx-scan-in-progress.txt"); }  // a plug-in that crashed the last scan is skipped next time

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    std::array<Slot, kSlots> slots;
    juce::SpinLock lock;
    double sr = 48000.0;
    int bs = 512;
    std::unique_ptr<Scan> scanner;
};
