#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

// Extreme time stretch, written for Nektar from the published idea of random-phase spectral stretching:
// every frame of the sound is taken at a slowly moving position, its spectrum magnitudes are kept,
// the phases are replaced by random ones, and the frames are overlapped.
// The sound can then be stretched by very large factors without the "machine gun" of a normal stretcher,
// and it keeps sounding as long as the key is held (the source position wraps around).
// Pitch is done in the spectrum (bins are moved), so there is no aliasing from reading faster.
class SpectralStretch
{
public:
    void prepare (double sampleRate);
    // pos = where in the sample to start (in samples)
    void start (const juce::AudioBuffer<float>& src, double startPos, double ratio);
    // Writes n samples. stretch >= 1 (time slows down by that factor), ratio = pitch ratio (1 = original),
    // blur 0..1 (smooths the spectrum, loudness stays), freeze 0..1 (1 = the position in the sound stops).
    void process (const juce::AudioBuffer<float>& src, double stretch, double ratio, float blur, float freeze,
                  float* outL, float* outR, int n);

    int frameSize() const noexcept { return N; }

private:
    void makeFrame (const juce::AudioBuffer<float>& src, double stretch, double ratio, float blur, float freeze, bool advance);

    static constexpr float kGain = 1.41421356f; // measured: restores the level that the random phases and the two windows take away

    int order = 14, N = 1 << 14, hop = 1 << 13;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, work, mag, shifted, smooth, prefix;
    std::vector<float> tail[2], hopOut[2];
    int readIdx = 0;
    double pos = 0.0;
    juce::Random rng { 12345 };
};
