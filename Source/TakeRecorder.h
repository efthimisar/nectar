#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

// A "take" for ichochroma: while it records, it only LOOKS at what Nectar does (it never changes the sound).
// The audio thread copies the finished blocks (the output as heard, and the stem of each slot after its own processing and
// before the mix and Space) into a ring; a background thread writes the WAV files, the 20 Hz levels, the notes, the markers
// and the JSON file. Nothing is written, allocated or locked in the audio thread.
// Each kind of information is a small "collector" in the writer thread (notes, levels, markers); a new kind is added the same way.
class TakeRecorder : private juce::Thread
{
public:
    static constexpr int kSlots = 4;
    static constexpr int kCh = 2 + kSlots * 2;        // master L R, then L R of each slot
    static constexpr int kVersion = 1;
    static constexpr int kRingFrames = 1 << 18;       // ~5 s at 48 kHz: how long the disk may stall before blocks are lost
    static constexpr int kStageFrames = 1 << 15;      // the largest host block that can be copied (larger ones are counted as lost)
    static constexpr int kLevelRateHz = 20;

    TakeRecorder() : juce::Thread ("Nectar take writer")
    {
        ring.resize ((size_t) kCh * (size_t) kRingFrames, 0.0f);
        stage.resize ((size_t) kCh * (size_t) kStageFrames, 0.0f);
        scratch.resize ((size_t) kCh * (size_t) kStageFrames, 0.0f);
        events.resize (kEventSlots);
        meta.resize (kMetaSlots);
    }
    ~TakeRecorder() override { abort(); }

    // ------------------------------------------------------------ message thread
    bool isRecording() const { const int s = state.load(); return s == 1 || s == 2; }
    bool isBusy() const { return state.load() != 0; }          // still recording or still writing the files
    double seconds() const { return sampleRate > 0.0 ? (double) takePosShown.load() / sampleRate : 0.0; }

    // slotsInfo: array of what Nectar knows about each loaded sound at this moment; tempoInfo: object
    bool start (const juce::File& takesFolder, double sr, int latencySamples, const juce::var& slotsInfo, const juce::var& tempoInfo)
    {
        if (state.load() != 0 || sr <= 0.0) return false;
        waitForThreadToExit (5000);
        const auto now = juce::Time::getCurrentTime();
        folder = takesFolder.getChildFile (now.formatted ("%Y-%m-%d_%H.%M.%S"));
        if (! folder.createDirectory().wasOk()) { result = "The take folder could not be made."; return false; }
        zipFile = takesFolder.getChildFile (folder.getFileName() + ".zip");
        sampleRate = sr; latency = latencySamples; slots = slotsInfo; tempo = tempoInfo;
        startWall = now.toISO8601 (true);
        takePos = 0; takePosShown.store (0); stopReq.store (false);
        droppedBlocks.store (0); droppedEvents.store (0);
        blockFifo.reset(); metaFifo.reset(); eventFifo.reset();
        written = 0; masterWriter.reset();
        for (auto& w : stemWriter) w.reset();
        everPresent.fill (false);
        notes.clear(); openNote.assign (16 * 128, -1);
        rows.clear(); levelWinIndex = 0; levelWinPos = 0; levelWinEnd = windowEnd (0); levelSum.fill (0.0); levelPresent.fill (false);
        { const juce::ScopedLock sl (markLock); marks.clear(); }
        state.store (1);
        startThread();
        return true;
    }

    // stops at the end of the block that is running; the files are finished in the background
    void stop()
    {
        stopReq.store (true);
        int e = 1;
        if (state.compare_exchange_strong (e, 3)) return;           // the audio never started it
        for (int i = 0; i < 60 && state.load() == 2; ++i) juce::Thread::sleep (5);
        e = 2;
        state.compare_exchange_strong (e, 3);                        // the audio is not running: end it here
    }

    void abort() { if (state.load() == 1 || state.load() == 2) stop(); stopThread (15000); }

    // something that happened during the take (a preset, a press); ignored when no take is running
    void addMarker (const juce::String& type, const juce::String& detail = {})
    {
        if (state.load() != 2) return;
        const juce::ScopedLock sl (markLock);
        marks.push_back ({ takePosShown.load(), type, detail });
    }

    // the message about a finished take, once
    juce::String takeResult() { const juce::ScopedLock sl (markLock); auto r = result; result = {}; return r; }

    // ------------------------------------------------------------ audio thread
    bool beginBlock (int n)
    {
        int s = state.load();
        if (s == 0 || s == 3) return false;
        if (s == 1) { int e = 1; if (! state.compare_exchange_strong (e, 2)) return false; takePos = 0; }
        else if (stopReq.load()) { int e = 2; state.compare_exchange_strong (e, 3); return false; }
        curN = n; curMask = 0; curStart = takePos; curOk = n > 0 && n <= kStageFrames;
        if (curOk)
            for (int c = 0; c < kCh; ++c) std::fill_n (stage.data() + (size_t) c * kStageFrames, n, 0.0f);
        return true;
    }
    void captureMidi (const juce::MidiBuffer& mb, int source)       // source 0 = MIDI in, 1 = the on-screen / computer keyboard
    {
        for (const auto m : mb)
        {
            const auto msg = m.getMessage();
            const bool on = msg.isNoteOn(), off = msg.isNoteOff();
            if (! on && ! off) continue;
            int start1, size1, start2, size2;
            eventFifo.prepareToWrite (1, start1, size1, start2, size2);
            if (size1 < 1) { droppedEvents.fetch_add (1); continue; }
            events[(size_t) start1] = { curStart + (juce::int64) m.samplePosition, (juce::uint8) juce::jlimit (0, 15, msg.getChannel() - 1),
                                        (juce::uint8) msg.getNoteNumber(), (juce::uint8) (on ? msg.getVelocity() : 0), on, (juce::uint8) source };
            eventFifo.finishedWrite (1);
        }
    }
    void captureStem (int slot, const juce::AudioBuffer<float>& view)   // after the sound's own processing, before the mix and Space
    {
        if (! curOk || slot < 0 || slot >= kSlots) return;
        const int n = juce::jmin (curN, view.getNumSamples());
        for (int c = 0; c < 2; ++c)
            std::copy_n (view.getReadPointer (juce::jmin (c, view.getNumChannels() - 1)), n, stage.data() + (size_t) (2 + slot * 2 + c) * kStageFrames);
        curMask |= 1 << slot;
    }
    void endBlock (const juce::AudioBuffer<float>& master)             // the output as heard (after the last safety stage)
    {
        const int n = curN;
        if (curOk)
        {
            for (int c = 0; c < 2; ++c)
                std::copy_n (master.getReadPointer (juce::jmin (c, master.getNumChannels() - 1)), juce::jmin (n, master.getNumSamples()), stage.data() + (size_t) c * kStageFrames);
            int s1, z1, s2, z2, m1, mz1, m2, mz2;
            blockFifo.prepareToWrite (n, s1, z1, s2, z2);
            metaFifo.prepareToWrite (1, m1, mz1, m2, mz2);
            if (z1 + z2 < n || mz1 < 1) droppedBlocks.fetch_add (1);
            else
            {
                for (int c = 0; c < kCh; ++c)
                {
                    const float* src = stage.data() + (size_t) c * kStageFrames;
                    float* dst = ring.data() + (size_t) c * kRingFrames;
                    std::copy_n (src, z1, dst + s1);
                    if (z2 > 0) std::copy_n (src + z1, z2, dst + s2);
                }
                blockFifo.finishedWrite (n);
                meta[(size_t) m1] = { curStart, n, curMask };
                metaFifo.finishedWrite (1);
            }
        }
        else droppedBlocks.fetch_add (1);
        takePos += n;
        takePosShown.store (takePos);
    }

private:
    struct Ev { juce::int64 t; juce::uint8 ch, note, vel; bool on; juce::uint8 src; };
    struct Meta { juce::int64 start; int n; int mask; };
    struct Note { juce::int64 t, dur; int note, vel, src; const char* end; };
    struct Mark { juce::int64 pos; juce::String type, detail; };
    static constexpr int kEventSlots = 4096, kMetaSlots = 4096;

    // ---- writer thread
    void run() override
    {
        while (! threadShouldExit())
        {
            const bool did = drain();
            if (! did)
            {
                if (state.load() == 3 && ! drain()) break;
                wait (15);
            }
        }
        finish();
    }

    bool drain()
    {
        bool did = false;
        while (eventFifo.getNumReady() > 0)
        {
            int s1, z1, s2, z2;
            eventFifo.prepareToRead (1, s1, z1, s2, z2);
            const Ev e = events[(size_t) s1];
            eventFifo.finishedRead (1);
            noteEvent (e);
            did = true;
        }
        while (metaFifo.getNumReady() > 0)
        {
            int s1, z1, s2, z2;
            metaFifo.prepareToRead (1, s1, z1, s2, z2);
            const Meta m = meta[(size_t) s1];
            metaFifo.finishedRead (1);
            int a1, y1, a2, y2;
            blockFifo.prepareToRead (m.n, a1, y1, a2, y2);
            for (int c = 0; c < kCh; ++c)
            {
                const float* src = ring.data() + (size_t) c * kRingFrames;
                float* dst = scratch.data() + (size_t) c * kStageFrames;
                std::copy_n (src + a1, y1, dst);
                if (y2 > 0) std::copy_n (src + a2, y2, dst + y1);
            }
            blockFifo.finishedRead (m.n);
            if (m.start > written) consumeSilence (m.start - written);   // a lost block: keep every file on the same time line
            consume (m.n, m.mask);
            did = true;
        }
        return did;
    }

    juce::int64 windowEnd (juce::int64 k) const { return (juce::int64) std::llround ((double) (k + 1) * sampleRate / (double) kLevelRateHz); }

    std::unique_ptr<juce::AudioFormatWriter> makeWriter (const juce::File& f)
    {
        f.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream> (f);
        if (stream->failedToOpen()) return nullptr;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
        if (w != nullptr) stream.release();
        return w;
    }

    void writeZeros (juce::AudioFormatWriter& w, juce::int64 count)
    {
        std::fill (scratchZero.begin(), scratchZero.end(), 0.0f);
        const float* z[2] = { scratchZero.data(), scratchZero.data() };
        while (count > 0) { const int k = (int) std::min<juce::int64> (count, (juce::int64) scratchZero.size()); w.writeFromFloatArrays (z, 2, k); count -= k; }
    }

    void consumeSilence (juce::int64 count)
    {
        std::fill (scratch.begin(), scratch.end(), 0.0f);
        while (count > 0)
        {
            const int k = (int) std::min<juce::int64> (count, (juce::int64) kStageFrames);
            consume (k, 0);
            count -= k;
        }
    }

    // `scratch` holds n frames of all channels
    void consume (int n, int mask)
    {
        if (masterWriter == nullptr) masterWriter = makeWriter (folder.getChildFile ("Master.wav"));
        for (int s = 0; s < kSlots; ++s)
            if ((mask & (1 << s)) != 0 && ! everPresent[(size_t) s])
            {
                everPresent[(size_t) s] = true;
                stemWriter[(size_t) s] = makeWriter (folder.getChildFile ("Slot_" + juce::String (s + 1) + ".wav"));
                if (stemWriter[(size_t) s] != nullptr) writeZeros (*stemWriter[(size_t) s], written);   // before it was loaded: silence, same sample 0
            }
        auto ptr = [this] (int c) { return scratch.data() + (size_t) c * kStageFrames; };
        if (masterWriter != nullptr) { const float* d[2] = { ptr (0), ptr (1) }; masterWriter->writeFromFloatArrays (d, 2, n); }
        for (int s = 0; s < kSlots; ++s)
            if (stemWriter[(size_t) s] != nullptr) { const float* d[2] = { ptr (2 + s * 2), ptr (3 + s * 2) }; stemWriter[(size_t) s]->writeFromFloatArrays (d, 2, n); }

        // levels collector: RMS of each slot stem per 1/20 s (null where the slot had no sound)
        int done = 0;
        while (done < n)
        {
            const int take = (int) std::min<juce::int64> ((juce::int64) (n - done), levelWinEnd - (written + done));
            for (int s = 0; s < kSlots; ++s)
            {
                if ((mask & (1 << s)) == 0) continue;
                levelPresent[(size_t) s] = true;
                const float* l = ptr (2 + s * 2) + done; const float* r = ptr (3 + s * 2) + done;
                double acc = 0.0;
                for (int i = 0; i < take; ++i) acc += (double) l[i] * l[i] + (double) r[i] * r[i];
                levelSum[(size_t) s] += acc;
            }
            done += take;
            if (written + done >= levelWinEnd) emitRow (levelWinEnd - levelWinPos);
        }
        written += n;
    }

    void emitRow (juce::int64 len)
    {
        std::array<float, kSlots> row;
        for (int s = 0; s < kSlots; ++s)
        {
            if (! levelPresent[(size_t) s] || len <= 0) row[(size_t) s] = std::numeric_limits<float>::quiet_NaN();
            else
            {
                const double rms = std::sqrt (levelSum[(size_t) s] / (2.0 * (double) len));
                row[(size_t) s] = (float) juce::jmax (-120.0, 20.0 * std::log10 (juce::jmax (1.0e-9, rms)));
            }
            levelSum[(size_t) s] = 0.0; levelPresent[(size_t) s] = false;
        }
        rows.push_back (row);
        levelWinPos = levelWinEnd; ++levelWinIndex; levelWinEnd = windowEnd (levelWinIndex);
    }

    // notes collector
    void noteEvent (const Ev& e)
    {
        const size_t key = (size_t) e.ch * 128 + e.note;
        int& open = openNote[key];
        if (e.on)
        {
            if (open >= 0) { notes[(size_t) open].dur = e.t - notes[(size_t) open].t; notes[(size_t) open].end = "retrigger"; }
            notes.push_back ({ e.t, -1, e.note, e.vel, e.src, "take_end" });
            open = (int) notes.size() - 1;
        }
        else if (open >= 0)
        {
            notes[(size_t) open].dur = e.t - notes[(size_t) open].t; notes[(size_t) open].end = "off";
            open = -1;
        }
    }

    static double r6 (double v) { return std::round (v * 1.0e6) / 1.0e6; }

    void finish()
    {
        const juce::int64 total = takePosShown.load();
        if (written < total) consumeSilence (total - written);   // the last blocks were lost
        if (levelWinEnd > levelWinPos && written > levelWinPos) emitRow (written - levelWinPos);
        for (auto& n : notes) if (n.dur < 0) n.dur = total - n.t;
        if (masterWriter == nullptr) masterWriter = makeWriter (folder.getChildFile ("Master.wav"));
        masterWriter.reset();
        for (auto& w : stemWriter) w.reset();

        auto obj = [] { return new juce::DynamicObject(); };
        auto line = [] (const juce::String& key, const juce::var& v) { return "  " + juce::JSON::toString (juce::var (key), true) + ": " + juce::JSON::toString (v, true); };

        auto* rec = obj();
        rec->setProperty ("sample_rate", sampleRate);
        rec->setProperty ("length_samples", (juce::int64) total);
        rec->setProperty ("start_wallclock", startWall);   // when the button was pressed; the take itself starts at the next audio block
        rec->setProperty ("latency_samples", latency);
        rec->setProperty ("bit_depth", 24);

        juce::Array<juce::var> sl;
        if (slots.isArray()) for (int i = 0; i < slots.size(); ++i) sl.add (slots[i]);
        for (int s = 0; s < kSlots; ++s)
        {
            if (! everPresent[(size_t) s]) continue;
            bool known = false;
            for (auto& v : sl) if ((int) v.getProperty ("index", 0) == s + 1) known = true;
            if (known) continue;
            auto* o = obj();   // a sound that was loaded after the take began: only its stem is known
            o->setProperty ("index", s + 1);
            o->setProperty ("loaded_during_take", true);
            sl.add (juce::var (o));
        }
        for (auto& v : sl)
            if (auto* o = v.getDynamicObject())
            {
                const int i = (int) o->getProperty ("index");
                o->setProperty ("file", i >= 1 && i <= kSlots && everPresent[(size_t) (i - 1)] ? juce::var ("Slot_" + juce::String (i) + ".wav") : juce::var());
            }

        juce::Array<juce::var> nl;
        for (auto& n : notes)
        {
            auto* o = obj();
            o->setProperty ("t", r6 ((double) n.t / sampleRate));
            o->setProperty ("duration", r6 ((double) n.dur / sampleRate));
            o->setProperty ("note", n.note);
            o->setProperty ("velocity", n.vel);
            o->setProperty ("source", n.src == 0 ? "midi" : "keyboard");
            o->setProperty ("end", n.end);
            nl.add (juce::var (o));
        }
        auto* ev = obj();
        ev->setProperty ("notes", nl);
        ev->setProperty ("note_info", "t and duration in seconds from sample 0 of the take; velocity is the MIDI value 1..127; duration = key held (the sustain pedal is not followed); "
                                      "midi notes are sample-accurate as the host sends them, keyboard notes are placed at the start of the audio block they arrive in");

        juce::String out = "{\n";
        out << line ("version", kVersion) << ",\n";
        out << line ("recording", juce::var (rec)) << ",\n";
        out << line ("tempo", tempo) << ",\n";
        out << line ("slots", juce::var (sl)) << ",\n";
        out << line ("events", juce::var (ev)) << ",\n";
        out << "  \"series\": {\n    \"rate_hz\": " << kLevelRateHz << ",\n    \"t0\": 0,\n    \"unit\": \"dB (RMS of the stem, both channels; -120 = silence)\",\n"
            << "    \"columns\": [\"slot1\", \"slot2\", \"slot3\", \"slot4\"],\n    \"rows\": [\n";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            out << "      [";
            for (int s = 0; s < kSlots; ++s)
                out << (s ? ", " : "") << (std::isnan (rows[i][(size_t) s]) ? juce::String ("null") : juce::String (rows[i][(size_t) s], 2));
            out << (i + 1 < rows.size() ? "],\n" : "]\n");
        }
        out << "    ]\n  },\n";

        juce::Array<juce::var> ml;
        auto addMark = [&] (juce::int64 pos, const juce::String& type, const juce::String& detail)
        {
            auto* o = obj();
            o->setProperty ("t", r6 ((double) pos / sampleRate));
            o->setProperty ("type", type);
            if (detail.isNotEmpty()) o->setProperty ("detail", detail);
            ml.add (juce::var (o));
        };
        addMark (0, "take_start", {});
        { const juce::ScopedLock sl2 (markLock); for (auto& m : marks) addMark (m.pos, m.type, m.detail); }
        addMark (total, "take_stop", {});
        out << line ("markers", juce::var (ml)) << ",\n";

        auto* cap = obj();
        cap->setProperty ("lost_blocks", droppedBlocks.load());
        cap->setProperty ("lost_note_events", droppedEvents.load());
        out << line ("capture", juce::var (cap)) << "\n}\n";

        folder.getChildFile ("nektar_paint.json").replaceWithText (out, false, false, "\n");

        juce::ZipFile::Builder zb;
        juce::Array<juce::File> files;
        folder.findChildFiles (files, juce::File::findFiles, false);
        for (auto& f : files) zb.addFile (f, 1, folder.getFileName() + "/" + f.getFileName());
        bool ok = false;
        {
            zipFile.deleteFile();
            juce::FileOutputStream zs (zipFile);
            if (zs.openedOk()) { ok = zb.writeToStream (zs, nullptr); zs.flush(); }
        }
        {
            const juce::ScopedLock sl2 (markLock);
            result = ok ? "Take saved: " + folder.getFileName() + " (folder and zip, in Takes)" : "Take saved in the folder " + folder.getFileName() + " (the zip could not be made).";
        }
        state.store (0);
    }

    // ---- state
    std::atomic<int> state { 0 };               // 0 idle, 1 waiting for the next block, 2 recording, 3 audio finished, files being written
    std::atomic<bool> stopReq { false };
    std::atomic<juce::int64> takePosShown { 0 };
    std::atomic<int> droppedBlocks { 0 }, droppedEvents { 0 };
    double sampleRate = 0.0;
    int latency = 0;
    juce::String startWall;
    juce::var slots, tempo;
    juce::File folder, zipFile;
    juce::String result;
    juce::CriticalSection markLock;
    std::vector<Mark> marks;

    // audio thread only
    juce::int64 takePos = 0, curStart = 0;
    int curN = 0, curMask = 0;
    bool curOk = false;
    std::vector<float> stage;

    // audio -> writer
    std::vector<float> ring;
    std::vector<Ev> events;
    std::vector<Meta> meta;
    juce::AbstractFifo blockFifo { kRingFrames }, metaFifo { kMetaSlots }, eventFifo { kEventSlots };

    // writer thread only
    std::vector<float> scratch;
    std::vector<float> scratchZero = std::vector<float> (8192, 0.0f);
    juce::int64 written = 0;
    std::unique_ptr<juce::AudioFormatWriter> masterWriter;
    std::array<std::unique_ptr<juce::AudioFormatWriter>, kSlots> stemWriter;
    std::array<bool, kSlots> everPresent {};
    std::vector<Note> notes;
    std::vector<int> openNote;
    std::vector<std::array<float, kSlots>> rows;
    juce::int64 levelWinIndex = 0, levelWinPos = 0, levelWinEnd = 0;
    std::array<double, kSlots> levelSum {};
    std::array<bool, kSlots> levelPresent {};
};
