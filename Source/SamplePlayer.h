#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <vector>
#include "SpectralStretch.h"
#include "SampleCleaner.h"
#include "SineModel.h"
#include "AdditiveVoice.h"
#include "MorphVoice.h"

// A loaded sound. The UI shows `data`; the audio thread plays `play` / `playLoop` (prepared at the host sample rate).
struct Sample
{
    juce::AudioBuffer<float> data; // as read from the file
    juce::AudioBuffer<float> cleaned;                // data after cleaning (0 samples = not cleaned); what is played
    SampleCleaner::Analysis analysis;                // what was found in the sound
    SampleCleaner::NoiseProfile autoProfile;         // noise estimated from the quietest parts
    int serial = 0;                                  // which loaded sound this belongs to
    double rate = 44100.0;         // file sample rate
    juce::String name;
    juce::String path;
    float modelGain = 1.0f;             // makes the sine description (with its noise part) as loud as the sound itself
    std::vector<float> hitGate;         // 0..1 per sample: how much of `play` counts as a hit at that moment
    juce::AudioBuffer<float> transient; // only the sudden hits of `play` (same length); laid back over the morph, which smooths them
    juce::AudioBuffer<float> grainSrcHi; // second coloured copy: the other end of a colour knob that is being moved (LFO etc.)
    int moveParam = -1;                 // which colour knob moves between grainSrc and grainSrcHi (-1 = none, 100 = Air)
    float moveLo = 0.0f, moveHi = 1.0f;
    juce::AudioBuffer<float> grainSrcY, grainSrcXY; // with a second, differently moved knob: its other end, and both other ends
    int moveParam2 = -1;
    float moveLo2 = 0.0f, moveHi2 = 1.0f;
    juce::AudioBuffer<float> grainSrc;  // `play` with the colour (Tone / Focus / Size) baked in, for the grains (0 samples = use play)
    float cleanN = -1.0f, cleanH = -1.0f, cleanL = -1.0f; // the cleaning settings `cleaned` was made with
    bool reverse = false;               // the chosen part is played backwards
    float partA = 0.0f, partB = 1.0f; // chosen part of the sound (0..1); everything that is played is made from this part only

    juce::AudioBuffer<float> play;     // data converted to the host sample rate
    float autoGain = 1.0f;             // gain that brings the loud parts of this sound to a common A-weighted level (Auto level)
    juce::AudioBuffer<float> playLoop; // same, with a smooth cross-fade baked in at the loop seam
    double playRate = 0.0;             // host rate that play/playLoop were made for
    int loopStart = 0, loopEnd = 0;    // sustain loop (in play/playLoop samples)

    std::shared_ptr<const SineModel> model; // sine-wave description of `play` (made in the background; may be missing)
    double modelRate = 0.0;                  // host rate the model was made for
};

namespace signalsmith { namespace stretch { template<typename, class> struct SignalsmithStretch; } }

// Polyphonic one-sample player. Root note = middle C (MIDI 60).
// "Keep length" blends between two ways of playing a key:
//   0 = tape style (higher key = faster and shorter), 1 = the length stays the same, only the pitch changes.
class SamplePlayer
{
public:
    struct Params
    {
        float autoGain = 1.0f;   // from the sample (Auto level), 1 = off
        float level = 0.8f;      // 0..1
        float pitchSemis = 0.0f; // -12..+12
        float releaseSec = 0.25f;
        float keepLength = 1.0f; // 0..1
        bool loop = false;       // sustain: a smooth loop in a steady part of the sound
        float stretch = 0.0f;    // 0 = off, otherwise extreme stretch (see stretchFactor)
        float blur = 0.0f;       // 0..1 smooths the spectrum (spectral voices)
        float freeze = 0.0f;     // 0..1 stops the position in the sound (spectral voices)
        float morph = 0.0f;      // 0..1 moves the colour (spectral envelope) towards the partner sound
        SpectralBody body;       // Tone / Focus / Size of the spectral envelope (0.5 = untouched)
        float modelNoise = 0.0f; // 0..1 level of the residual (shaped noise) of the sine model
        float model = 0.0f;      // 0..1 blend from the normal engine to the sine-wave (additive) model
        float chromo = 0.0f;     // 0..1 Chromophonic: this sound's partials take the body of the body partner (needs the model)
        float chromoMove = 0.0f; // 0..1 depth of the slow travelling filter sweep that goes with it
        float chromoSpeed = 0.5f; // 0..1 speed of the sweep (0.03 .. 1 turn per second, 0.5 = about 0.17)
        float chromoBack = 0.5f; // 0..1 how far back the lower sound of the pair is pushed (level, 0.5 = -6 dB)
        float chromoDull = 0.5f; // 0..1 how dull the back sound is (high-frequency slope, 0.5 = -2 dB/oct above 1 kHz)
    };

    SamplePlayer();
    ~SamplePlayer();

    // Message thread: resamples to the host rate and finds a smooth sustain loop.
    static void makePlayable (Sample& s, double hostRate);

    // knob 0..1 -> how many times slower the sound moves (0 / tiny = off). 0.01 -> x2, 1 -> x256
    static double stretchFactor (float knob) noexcept;
    static bool stretchOn (float knob) noexcept { return knob > 0.01f; }
    // the spectral (random-phase) voices are used when stretch, blur or freeze is up
    static bool spectralOn (const Params& p) noexcept { return p.stretch > 0.01f || p.blur > 0.01f || p.freeze > 0.01f || p.body.active(); }

    void prepare (double sampleRate);
    void setSample (std::shared_ptr<const Sample> s);
    // clearOutput = false adds this player's sound to what is already in `out` (used to mix the slots).
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, bool clearOutput = true);
    bool hasSample() const noexcept { return sample != nullptr; }
    std::shared_ptr<const Sample> getSample() const { return sample; }
    // The sound this one morphs towards (set from the audio thread; nullptr = none).
    void setPartner (const std::shared_ptr<const Sample>& s) { if (s.get() != partner.get()) partner = s; }
    // The sound whose body (see SineModel::body) this one takes on in Chromophonic (set from the audio thread).
    void setBodyPartner (const std::shared_ptr<const Sample>& s) { if (s.get() != bodyPartner.get()) bodyPartner = s; }
    // How many voices a new note may use (the processor shares a fixed budget between the loaded slots).
    void setVoiceLimit (int n) noexcept { voiceLimit = juce::jlimit (1, kVoices, n); }
    static constexpr int kMaxVoices = 32;

private:
    using Stretch = signalsmith::stretch::SignalsmithStretch<float, void>;
    static constexpr int kVoices = 32;
    static constexpr int kStretchVoices = 16;
    static constexpr int kSpectralVoices = 8;
    static constexpr int kChunk = 256;
    static constexpr float kAttackSec = 0.005f;

    struct Voice
    {
        bool active = false;
        bool stretched = false;
        bool spectral = false;
        bool morphAdd = false; // the sine model is morphed towards the partner sound (MorphVoice)
        bool additive = false; // plays the sine model (alone or blended with the spectral engine)
        float modelMix = 0.0f;
        int note = -1;
        double pos = 0.0;      // read position (tape style)
        double inAccum = 0.0;  // fractional input samples (stretch style)
        float velocity = 1.0f;
        juce::uint64 startedAt = 0;
        juce::ADSR adsr;
    };

    const juce::AudioBuffer<float>* partnerBuffer (const Params& p) const noexcept;
    void noteOn (int note, float velocity, const Params& p);
    void noteOff (int note, const Params& p);
    void renderTape (Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p);
    void renderStretch (int index, Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p);
    void renderSpectral (int index, Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p);
    void renderAdditive (int index, Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p);
    bool modelUsable (int index, const Params& p) const noexcept;
    bool partnerModelUsable (const Params& p) const noexcept;
    void fillInput (Voice& v, int count, const Params& p, float* l, float* r);

    std::array<Voice, kVoices> voices;
    std::array<std::unique_ptr<Stretch>, kStretchVoices> stretchers;
    std::array<SpectralStretch, kSpectralVoices> spectrals;
    std::array<AdditiveVoice, kSpectralVoices> additives;
    std::array<MorphVoice, kSpectralVoices> morphs;
    std::vector<float> inL, inR, outL, outR, specL, specR, addL, addR;
    std::shared_ptr<const Sample> sample, partner, bodyPartner;
    std::array<float, SineModel::kBodyBins> bodyRatio {};
    AdditiveVoice::BodyShape bodyShape;
    bool bodyShapeOn = false;
    double sr = 44100.0;
    int voiceLimit = 32;
    juce::uint64 counter = 0;
};
