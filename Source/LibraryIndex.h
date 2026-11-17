#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

// The sound library on disk (Music / Nektar Library): every audio file, its category (the first folder under the library;
// files directly in the library have none) and two measures of its sound, worked out once in the background and kept in a
// small cache file next to the sounds:
//   brightness - the energy-weighted mean spectral centroid (log2 Hz) of its first seconds
//   attack     - log10 of the rise time of its loudness envelope from 10 % to 90 % of its peak
// Spectral centroid and (log) rise time are the two main acoustic correlates of timbre-space dimensions found in
// listening studies (McAdams et al. 1995, Psychological Research; Lakatos 2000, Perception & Psychophysics; abstracts read).
// The 10-90 % rise, the frame sizes and the map layout are our own choices.
class LibraryIndex : private juce::Thread
{
public:
    struct Entry { juce::File file; juce::String category; float bright = 0.0f, attack = 0.0f; };
    struct Dot { juce::File file; float x = 0.0f, y = 0.0f; };  // place on the map (unit disc)

    LibraryIndex() : juce::Thread ("Nektar library index") {}
    ~LibraryIndex() override { stopThread (4000); }

    void scan (const juce::File& folder)
    {
        stopThread (4000);
        root = folder;
        startThread (juce::Thread::Priority::low);
    }
    bool busy() const { return isThreadRunning(); }
    int version() const { return ver.load(); }

    std::vector<Entry> entries() const { const juce::ScopedLock sl (lock); return list; }
    juce::StringArray categories() const
    {
        juce::StringArray c;
        const juce::ScopedLock sl (lock);
        for (const auto& e : list) if (e.category.isNotEmpty()) c.addIfNotAlreadyThere (e.category);
        c.sortNatural();
        return c;
    }

    // The map of one category ("" = all): brightness goes round the circle (by rank, so the dots spread evenly),
    // a soft start lies outside, a sharp hit inside; the middle of the circle is left free (the Space pad sits there).
    std::vector<Dot> layout (const juce::String& category) const
    {
        std::vector<Entry> sel;
        {
            const juce::ScopedLock sl (lock);
            for (const auto& e : list) if (category.isEmpty() || e.category == category) sel.push_back (e);
        }
        std::vector<Dot> out;
        const int n = (int) sel.size();
        if (n == 0) return out;
        std::vector<int> ib (n), ia (n), rb (n), ra (n);
        for (int i = 0; i < n; ++i) ib[(size_t) i] = ia[(size_t) i] = i;
        std::sort (ib.begin(), ib.end(), [&] (int a, int b) { return sel[(size_t) a].bright < sel[(size_t) b].bright; });
        std::sort (ia.begin(), ia.end(), [&] (int a, int b) { return sel[(size_t) a].attack < sel[(size_t) b].attack; });
        for (int i = 0; i < n; ++i) { rb[(size_t) ib[(size_t) i]] = i; ra[(size_t) ia[(size_t) i]] = i; }
        for (int i = 0; i < n; ++i)
        {
            const float ub = n > 1 ? (float) rb[(size_t) i] / (float) (n - 1) : 0.5f;   // 0 dark .. 1 bright
            const float ua = n > 1 ? (float) ra[(size_t) i] / (float) (n - 1) : 0.5f;   // 0 sharp hit .. 1 slow start
            const float an = juce::MathConstants<float>::pi * (0.5f + 1.8f * ub);      // dark at the bottom left, round over the top to bright
            const float r = 0.62f + 0.32f * ua;
            out.push_back ({ sel[(size_t) i].file, r * std::cos (an), r * std::sin (an) });
        }
        return out;
    }

private:
    void run() override
    {
        if (! root.isDirectory()) { const juce::ScopedLock sl (lock); list.clear(); ++ver; return; }
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        // the cache: path | size | modification time | brightness | attack
        const auto cacheFile = root.getChildFile (".nektar-index");
        std::map<juce::String, Entry> cached;
        std::map<juce::String, juce::String> stamp;
        {
            juce::StringArray lines;
            lines.addLines (cacheFile.loadFileAsString());
            for (const auto& l : lines)
            {
                const auto t = juce::StringArray::fromTokens (l, "|", "");
                if (t.size() < 5) continue;
                Entry e; e.file = juce::File (t[0]); e.bright = t[3].getFloatValue(); e.attack = t[4].getFloatValue();
                cached[t[0]] = e; stamp[t[0]] = t[1] + "|" + t[2];
            }
        }
        juce::Array<juce::File> files;
        root.findChildFiles (files, juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac");
        std::vector<Entry> found;
        juce::String cacheOut;
        int sinceShow = 0;
        for (const auto& f : files)
        {
            if (threadShouldExit()) return;
            const auto rel = f.getRelativePathFrom (root);
            if (rel.startsWithIgnoreCase ("Presets") || f.getFileName().startsWithChar ('.')) continue;
            Entry e;
            e.file = f;
            e.category = rel.containsChar (juce::File::getSeparatorChar()) ? rel.upToFirstOccurrenceOf (juce::String::charToString (juce::File::getSeparatorChar()), false, false) : juce::String();
            const auto key = f.getFullPathName();
            const juce::String st = juce::String (f.getSize()) + "|" + juce::String (f.getLastModificationTime().toMilliseconds());
            if (stamp.count (key) && stamp[key] == st) { e.bright = cached[key].bright; e.attack = cached[key].attack; }
            else if (! measure (fm, f, e)) continue;
            found.push_back (e);
            cacheOut << key << "|" << st << "|" << e.bright << "|" << e.attack << "\n";
            if (++sinceShow >= 40) { sinceShow = 0; const juce::ScopedLock sl (lock); list = found; ++ver; } // show the map while it grows
        }
        { const juce::ScopedLock sl (lock); list = found; ++ver; }
        cacheFile.replaceWithText (cacheOut);
    }

    static bool measure (juce::AudioFormatManager& fm, const juce::File& f, Entry& e)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->sampleRate <= 0.0 || r->lengthInSamples < 256) return false;
        const int n = (int) std::min<long long> ((long long) r->lengthInSamples, (long long) (r->sampleRate * 6.0));
        juce::AudioBuffer<float> b ((int) juce::jmax (1u, juce::jmin (2u, r->numChannels)), n);
        r->read (&b, 0, n, 0, true, b.getNumChannels() > 1);
        const double sr = r->sampleRate;
        // brightness
        constexpr int N = 2048;
        juce::dsp::FFT fft (11);
        std::vector<float> buf (2 * N);
        double wsum = 0.0, csum = 0.0;
        for (int s = 0; s + N <= n; s += N / 2)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
            {
                float m = 0.0f;
                for (int c = 0; c < b.getNumChannels(); ++c) m += b.getSample (c, s + i);
                buf[(size_t) i] = m * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N));
            }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            double num = 0.0, den = 0.0;
            for (int k = 1; k < N / 2; ++k) { const double a = buf[(size_t) k]; num += a * k * sr / N; den += a; }
            if (den > 1.0e-9) { const double c = num / den; csum += den * std::log2 (juce::jmax (20.0, c)); wsum += den; }
        }
        e.bright = wsum > 0.0 ? (float) (csum / wsum) : 10.0f;
        // attack: rise of the loudness envelope (5 ms frames) from 10 % to 90 % of its peak
        const int hop = juce::jmax (16, (int) (sr * 0.005));
        std::vector<float> env;
        for (int s = 0; s + hop <= n; s += hop)
        {
            float v = 0.0f;
            for (int c = 0; c < b.getNumChannels(); ++c) v += b.getRMSLevel (c, s, hop);
            env.push_back (v);
        }
        const float mx = env.empty() ? 0.0f : *std::max_element (env.begin(), env.end());
        if (mx <= 1.0e-6f) { e.attack = 0.0f; return true; }
        int t10 = -1, t90 = -1;
        for (int i = 0; i < (int) env.size(); ++i)
        {
            if (t10 < 0 && env[(size_t) i] >= 0.1f * mx) t10 = i;
            if (t90 < 0 && env[(size_t) i] >= 0.9f * mx) { t90 = i; break; }
        }
        const double rise = juce::jmax (1, t90 - juce::jmax (0, t10)) * (double) hop / sr;
        e.attack = (float) std::log10 (juce::jlimit (0.001, 6.0, rise));
        return true;
    }

    juce::File root;
    juce::CriticalSection lock;
    std::vector<Entry> list;
    std::atomic<int> ver { 0 };
};
