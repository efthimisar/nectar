#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

// A sound described as a set of sine waves (partials) that change slowly in time.
// Written for Nektar from the usual sinusoidal-modelling steps (STFT, peak picking with parabolic interpolation,
// peak continuation into tracks): McAulay & Quatieri 1986, Serra & Smith 1990, and as described in Caetano 2021.
// The part the sines do not explain (breath, bow, room, the grain of a pad) is kept as a "residual": for every frame
// the strength of the sound outside the partial peaks, in kBands bands, to be played back as shaped noise
// (the stochastic part of Serra & Smith 1990). No phases are kept: the voice makes its own.
struct SineModel
{
    static constexpr int kFft = 4096;
    static constexpr int kHop = 512;
    static constexpr int kFirstCentre = kFft / 2; // time of the middle of the first analysis frame
    static constexpr int kMaxTracks = 8192;
    static constexpr int kMaxPerFrame = 96;       // the strongest peaks of each frame are kept
    static constexpr int kMinTrackFrames = 4;
    static constexpr int kMaxHarm = 128;          // highest harmonic number that is labelled
    static constexpr int kBands = 48;             // residual bands, spaced evenly on a log-frequency axis
    static constexpr int kBodyBins = 100;         // "body": time-averaged strength of the partials per twelfth of an octave...
    static constexpr double kBodyLow = 40.0;      // ...from 40 Hz upwards (100 bins reach about 12.9 kHz)
    static constexpr double kBandLow = 60.0;      // Hz, lower edge of band 0 (below it band 0 is used)

    // One piece of a track, between analysis frame m and m + 1 (frequency in Hz, linear amplitude).
    struct Seg { int track; float fa, aa, fb, ab; };

    struct Channel
    {
        std::vector<Seg> segs;
        std::vector<int> segStart; // numFrames + 1 entries: segment m is segs[segStart[m] .. segStart[m + 1])
        int numFrames = 0;
        // residual: numFrames * kBands values; the standard deviation of white noise that would have the same strength
        // in that band as the sound has outside its partial peaks. Empty if there is none.
        std::vector<float> noise;
        // harmonic labels (for Morph between two sounds): harm[track] = harmonic number of the track (0 = none)
        std::vector<unsigned char> harm;
        float f0 = 0.0f;           // one fundamental for the whole sound in Hz, 0 if none was found
        float harmonicity = 0.0f;  // share (0..1) of the sine energy that sits on whole multiples of f0
    };

    std::vector<Channel> ch; // 1 or 2 channels
    double rate = 0.0;
    int numTracks = 0;       // largest track id + 1, over the channels
    std::vector<float> body;  // kBodyBins energies (smoothed over +-1/3 octave), empty if the model has no tracks
    float levelFix = 1.0f;    // gain that makes the sines alone as loud as the sound they were taken from
    double bandRatio = 1.0;  // frequency ratio between neighbouring residual bands

    // Slow (a second or so for a minute of stereo): call from a background thread only. Returns nullptr if cancelled.
    static std::shared_ptr<SineModel> analyse (const juce::AudioBuffer<float>& x, double rate, const std::atomic<bool>* cancel = nullptr);
};
