#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

// A sound described as a set of sine waves (partials) that change slowly in time.
// Written for Nektar from the usual sinusoidal-modelling steps (STFT, peak picking with parabolic interpolation,
// peak continuation into tracks): McAulay & Quatieri 1986, Serra & Smith 1990, and as described in Caetano 2021.
// Here only the sine part is modelled (no noise residual yet), and no phases are kept: the voice makes its own.
struct SineModel
{
    static constexpr int kFft = 4096;
    static constexpr int kHop = 512;
    static constexpr int kFirstCentre = kFft / 2; // time of the middle of the first analysis frame
    static constexpr int kMaxTracks = 8192;
    static constexpr int kMaxPerFrame = 96;       // the strongest peaks of each frame are kept
    static constexpr int kMinTrackFrames = 4;

    // One piece of a track, between analysis frame m and m + 1 (frequency in Hz, linear amplitude).
    struct Seg { int track; float fa, aa, fb, ab; };

    struct Channel
    {
        std::vector<Seg> segs;
        std::vector<int> segStart; // numFrames + 1 entries: segment m is segs[segStart[m] .. segStart[m + 1])
        int numFrames = 0;
    };

    std::vector<Channel> ch; // 1 or 2 channels
    double rate = 0.0;
    int numTracks = 0;       // largest track id + 1, over the channels

    // Slow (a second or so for a minute of stereo): call from a background thread only. Returns nullptr if cancelled.
    static std::shared_ptr<SineModel> analyse (const juce::AudioBuffer<float>& x, double rate, const std::atomic<bool>* cancel = nullptr);
};
