#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Offline cleaning of a recorded sound (runs in a background thread, never in the audio thread).
namespace SampleCleaner
{
// What was found in the sound (a quick estimate, not a measurement standard).
struct Analysis
{
    bool valid = false;
    float noiseFloorDb = -100.0f; // level of the quietest 10 % of 50 ms blocks, in dBFS
    int humHz = 0;                // 50 or 60, 0 = none found
    int humHarmonics = 0;         // how many of the first harmonics stand out
    int clippedSpots = 0;         // places where the waveform sits on the maximum for 3+ samples in a row
};

// Average power spectrum of the background noise (one value per FFT bin).
struct NoiseProfile
{
    static constexpr int kFft = 2048;
    bool valid = false;
    double rate = 0.0;
    std::vector<float> power; // kFft / 2 + 1 values
};

struct CleanParams
{
    float noise = 0.0f;  // 0 = off .. 1 = strongest
    float hum = 0.0f;    // 0 = off .. 1 = full removal of the mains hum that was found
    float lowCut = 0.0f; // 0 = off; otherwise a 24 dB/oct high-pass between 20 and 400 Hz

    bool any() const noexcept { return noise > 0.01f || hum > 0.01f || lowCut > 0.01f; }
    static float lowCutHz (float knob) noexcept;
};

Analysis analyse (const juce::AudioBuffer<float>& data, double rate);

// quietestOnly = true: use the quietest 15 % of the frames (automatic); false: use everything (a room tone recording).
NoiseProfile estimateProfile (const juce::AudioBuffer<float>& data, double rate, bool quietestOnly);

// Returns the cleaned copy (same length, same channels). Order: low cut, hum notches, noise reduction.
juce::AudioBuffer<float> clean (const juce::AudioBuffer<float>& in, double rate, const CleanParams& p,
                                const NoiseProfile& profile, int humHz, int humHarmonics);
} // namespace SampleCleaner
