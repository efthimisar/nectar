#pragma once
#include <array>
#include <cmath>
#include "SineModel.h"

// The key of a sound, from its sine description.
// Pitch-class profile: the amplitude of every partial between 55 Hz and 5 kHz, summed per pitch class (C = 0 .. B = 11).
// Key: the Krumhansl-Schmuckler method - Pearson correlation of that profile with the 24 rotated Krumhansl-Kessler key
// profiles (major / minor probe-tone ratings, Krumhansl & Kessler 1982, Psychological Review; values as given in
// Krumhansl 1990, "Cognitive Foundations of Musical Pitch"; checked against several independent implementations,
// 17 Nov 2026, the original tables not seen). The best key wins.
// Our own choices (not from a source): amplitude weighting, the frequency limits, and when a sound counts as having a key
// at all (best correlation >= 0.55 and a clearly uneven profile, spread >= 0.6 of the mean; raised from 0.25 on 18 Nov 2026
// so that doors, chains and hats are left alone rather than given a key they do not have).
struct Tonality
{
    bool tonal = false;   // a key was found
    int tonic = 0;        // 0 = C .. 11 = B
    bool minor = false;
    float fit = 0.0f;     // the best correlation
    std::array<float, 12> profile {};

    static Tonality of (const SineModel& m)
    {
        static const float maj[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static const float mnr[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
        Tonality t;
        double pc[12] = {};
        for (const auto& ch : m.ch)
            for (const auto& s : ch.segs)
            {
                if (s.fa < 55.0f || s.fa > 5000.0f || s.aa <= 0.0f) continue;
                const double midi = 69.0 + 12.0 * std::log2 ((double) s.fa / 440.0);
                const int k = ((int) std::lround (midi) % 12 + 12) % 12;
                pc[k] += (double) s.aa;
            }
        double mean = 0.0;
        for (double v : pc) mean += v;
        mean /= 12.0;
        if (mean <= 0.0) return t;
        double sd = 0.0;
        for (int i = 0; i < 12; ++i) { t.profile[(size_t) i] = (float) (pc[i] / mean); sd += (pc[i] - mean) * (pc[i] - mean); }
        sd = std::sqrt (sd / 12.0);
        auto corr = [&] (const float* prof, int root)
        {
            double pm = 0.0;
            for (int i = 0; i < 12; ++i) pm += prof[i];
            pm /= 12.0;
            double num = 0.0, da = 0.0, db = 0.0;
            for (int i = 0; i < 12; ++i)
            {
                const double a = pc[(i + root) % 12] - mean, b = prof[i] - pm;
                num += a * b; da += a * a; db += b * b;
            }
            return da > 0.0 && db > 0.0 ? num / std::sqrt (da * db) : 0.0;
        };
        double best = -2.0;
        for (int root = 0; root < 12; ++root)
            for (int mi = 0; mi < 2; ++mi)
            {
                const double c = corr (mi ? mnr : maj, root);
                if (c > best) { best = c; t.tonic = root; t.minor = mi == 1; }
            }
        t.fit = (float) best;
        t.tonal = best >= 0.55 && sd / mean >= 0.6;
        return t;
    }

    static const char* noteName (int pc)
    {
        static const char* const n[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return n[((pc % 12) + 12) % 12];
    }
};
