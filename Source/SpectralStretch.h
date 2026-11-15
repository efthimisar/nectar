#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>
#include <cmath>

// "Body": reshapes the broad spectral envelope (the colour of the sound) while the fine structure
// (pitch, partials) stays: the sound is split into source (fine structure) and body (envelope), as in
// the source-filter idea (Schwarz & Rodet 1999). All three 0..1, 0.5 = untouched.
struct SpectralBody
{
    float tone = 0.5f;  // 0 dark ... 1 bright (tilt of the envelope only)
    float focus = 0.5f; // 0 soft / wide ... 1 sharp / narrow (contrast of the envelope)
    float size = 0.5f;  // 0 small (envelope moves up) ... 1 big (envelope moves down)
    float rough = 0.0f; // Source: 0 smooth ... 1 rough (random level jitter per bin, new for every frame)
    bool shapeActive() const noexcept { return std::abs (tone - 0.5f) > 0.01f || std::abs (focus - 0.5f) > 0.01f || std::abs (size - 0.5f) > 0.01f; }
    bool active() const noexcept { return shapeActive() || rough > 0.01f; }
};

// Extreme time stretch, written for Nektar from the published idea of random-phase spectral stretching:
// every frame of the sound is taken at a slowly moving position, its spectrum magnitudes are kept,
// the phases are replaced by random ones, and the frames are overlapped.
// The sound can then be stretched by very large factors without the "machine gun" of a normal stretcher,
// and it keeps sounding as long as the key is held (the source position wraps around).
// Pitch is done in the spectrum (bins are moved), so there is no aliasing from reading faster.
class SpectralStretch
{
public:
    using Body = SpectralBody;
    void prepare (double sampleRate);
    // pos = where in the sample to start (in samples)
    // partner / morph: see process()
    void start (const juce::AudioBuffer<float>& src, double startPos, double ratio,
                const juce::AudioBuffer<float>* partner = nullptr, float morph = 0.0f, Body body = Body());
    // Writes n samples. stretch >= 1 (time slows down by that factor), ratio = pitch ratio (1 = original),
    // blur 0..1 (smooths the spectrum, loudness stays), freeze 0..1 (1 = the position in the sound stops).
    // Morph (0..1) with a partner sound: the spectral envelope (the broad shape of the spectrum, i.e. the "colour")
    // moves from this sound's towards the partner's, while this sound's pitch and fine structure stay.
    // partner = nullptr or morph = 0: no effect at all.
    void process (const juce::AudioBuffer<float>& src, double stretch, double ratio, float blur, float freeze,
                  float* outL, float* outR, int n,
                  const juce::AudioBuffer<float>* partner = nullptr, float morph = 0.0f, Body body = Body());

    int frameSize() const noexcept { return N; }

private:
    void makeFrame (const juce::AudioBuffer<float>& src, double stretch, double ratio, float blur, float freeze, bool advance,
                    const juce::AudioBuffer<float>* partner, float morph, const Body& body);
    void applyBody (const Body& body);
    void applyMorph (const juce::AudioBuffer<float>& partner, int channel, float morph);
    void envelope (const float* in, float* out); // broad spectral shape (two box filters whose width grows with frequency)

    static constexpr float kGain = 1.41421356f; // measured: restores the level that the random phases and the two windows take away

    int order = 14, N = 1 << 14, hop = 1 << 13;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, work, mag, shifted, smooth, prefix;
    std::vector<float> workB, magB, envA, envB, envTmp, srcSpec; // morph, body
    double rate = 44100.0, posB = 0.0;
    std::vector<float> tail[2], hopOut[2];
    int readIdx = 0;
    double pos = 0.0;
    juce::Random rng { 12345 };
};
