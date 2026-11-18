#include "SamplePlayer.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace
{
// Windowed-sinc kernel (Blackman window), 8 zero crossings each side, sampled finely.
// Reading faster than the output rate uses a stretched kernel = low-pass, so nothing folds back as aliasing.
constexpr int kHalf = 8;
constexpr int kRes = 256;

const float* kernelTable()
{
    static const std::array<float, kHalf * kRes + 2> table = []
    {
        std::array<float, kHalf * kRes + 2> t {};
        const double pi = juce::MathConstants<double>::pi;
        for (size_t j = 0; j < t.size(); ++j)
        {
            const double u = (double) j / kRes;
            const double s = u < 1.0e-9 ? 1.0 : std::sin (pi * u) / (pi * u);
            const double x = juce::jmin (1.0, u / kHalf);
            t[j] = (float) (s * (0.42 + 0.5 * std::cos (pi * x) + 0.08 * std::cos (2.0 * pi * x)));
        }
        return t;
    }();
    return table.data();
}

// One output value from `src` at fractional position `pos`, band-limited. Positions outside the sound are silence.
// loopEnd/loopStart > 0 wraps reads that run past the loop end back into the loop.
inline float sincRead (const float* src, int n, double pos, float cut, int loopStart, int loopEnd)
{
    const float* T = kernelTable();
    const int hw = (int) std::ceil ((float) kHalf / cut);
    const int i0 = (int) std::floor (pos);
    float acc = 0.0f, wsum = 0.0f;
    for (int k = 1 - hw; k <= hw; ++k)
    {
        int idx = i0 + k;
        const float u = std::abs ((float) ((double) idx - pos)) * cut;
        if (u >= (float) kHalf)
            continue;
        const float fi = u * (float) kRes;
        const int ti = (int) fi;
        const float w = cut * (T[ti] + (T[ti + 1] - T[ti]) * (fi - (float) ti));
        wsum += w;
        if (loopEnd > loopStart)
            while (idx >= loopEnd)
                idx -= loopEnd - loopStart;
        if (idx < 0 || idx >= n)
            continue;
        acc += w * src[idx];
    }
    return wsum > 1.0e-6f ? acc / wsum : 0.0f;
}
} // namespace

SamplePlayer::SamplePlayer() = default;
SamplePlayer::~SamplePlayer() = default;

// ---------------------------------------------------------------- preparing a sound for playback
// How loud the sound is to the ear, over the parts where it actually sounds: the 2048-sample blocks within 30 dB of the
// loudest block are averaged, each weighted by the A-weighting curve (IEC 61672 formula). Returns the gain that brings
// that level to the common target (limited to +-24 dB; the target is set so that a typical sample stays about as it was).
static float measureAutoGain (const juce::AudioBuffer<float>& x, double rate)
{
    constexpr int N = 2048;
    const int len = x.getNumSamples();
    if (len < N * 2) return 1.0f;
    juce::dsp::FFT fft (11);
    std::vector<float> buf ((size_t) N * 2), w ((size_t) N);
    for (int i = 0; i < N; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N);
    std::vector<float> aw ((size_t) N / 2);
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = (double) k * rate / N, f2 = f * f;
        const double ra = (12194.0 * 12194.0 * f2 * f2) / ((f2 + 20.6 * 20.6) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
        aw[(size_t) k] = (float) ra;
    }
    std::vector<double> blocks;
    for (int s = 0; s + N <= juce::jmin (len, (int) (60.0 * rate)); s += N)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int c = 0; c < x.getNumChannels(); ++c)
            for (int i = 0; i < N; ++i) buf[(size_t) i] += x.getSample (c, s + i) * w[(size_t) i] / (float) x.getNumChannels();
        fft.performRealOnlyForwardTransform (buf.data());
        double e = 0.0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double re = buf[(size_t) 2 * k], im = buf[(size_t) 2 * k + 1];
            e += (re * re + im * im) * (double) aw[(size_t) k] * (double) aw[(size_t) k];
        }
        blocks.push_back (e / ((double) N * (double) N * 0.375));
    }
    if (blocks.empty()) return 1.0f;
    double mx = 0.0;
    for (double b : blocks) mx = std::max (mx, b);
    if (mx <= 1.0e-14) return 1.0f;
    double sum = 0.0; int cnt = 0;
    for (double b : blocks) if (b >= mx * 1.0e-3) { sum += b; ++cnt; }
    const double db = 10.0 * std::log10 (sum / (double) cnt) ;
    return std::pow (10.0f, (float) juce::jlimit (-24.0, 24.0, -31.0 - db) / 20.0f);
}

void SamplePlayer::makePlayable (Sample& s, double hostRate)
{
    const auto& whole = s.cleaned.getNumSamples() > 0 ? s.cleaned : s.data; // the cleaned sound, if there is one
    const int ch = whole.getNumChannels();
    // the chosen part of the sound (short fades at cut edges, so it never clicks)
    juce::AudioBuffer<float> part;
    {
        const int nW = whole.getNumSamples();
        const float lo = juce::jlimit (0.0f, 1.0f, juce::jmin (s.partA, s.partB)), hi = juce::jlimit (0.0f, 1.0f, juce::jmax (s.partA, s.partB));
        if ((lo > 0.0005f || hi < 0.9995f) && nW > 4096)
        {
            int a = (int) (lo * (float) nW), b = (int) (hi * (float) nW);
            if (b - a < 2048) { b = juce::jmin (nW, a + 2048); a = juce::jmax (0, b - 2048); }
            part.setSize (ch, b - a);
            const int fade = juce::jmin ((b - a) / 4, (int) (0.005 * s.rate));
            for (int c = 0; c < ch; ++c)
            {
                part.copyFrom (c, 0, whole, c, a, b - a);
                if (a > 0) part.applyGainRamp (c, 0, fade, 0.0f, 1.0f);
                if (b < nW) part.applyGainRamp (c, b - a - fade, fade, 1.0f, 0.0f);
            }
        }
    }
    if (s.reverse)
    {
        if (part.getNumSamples() == 0) part = whole;
        part.reverse (0, part.getNumSamples());
    }
    const auto& src0 = part.getNumSamples() > 0 ? part : whole;
    const int nIn = src0.getNumSamples();

    // 1. convert to the host sample rate (so everything after this is simple)
    if (std::abs (s.rate - hostRate) < 0.5)
        s.play = src0;
    else
    {
        const double step = s.rate / hostRate; // input samples per output sample
        const int nOut = juce::jmax (2, (int) std::floor ((double) nIn / step));
        const float cut = juce::jlimit (0.05f, 1.0f, (float) (1.0 / step));
        s.play.setSize (ch, nOut);
        for (int c = 0; c < ch; ++c)
        {
            const float* src = src0.getReadPointer (c);
            float* dst = s.play.getWritePointer (c);
            for (int i = 0; i < nOut; ++i)
                dst[i] = sincRead (src, nIn, (double) i * step, cut, 0, 0);
        }
    }
    s.playRate = hostRate;

    s.autoGain = measureAutoGain (s.play, hostRate);

    // the sudden hits: where a fast level follower (2 ms) rises well above a slow one (60 ms)
    {
        const int n = s.play.getNumSamples(), chs = s.play.getNumChannels();
        s.transient.setSize (chs, n);
        s.hitGate.assign ((size_t) n, 0.0f);
        const float cf = 1.0f - std::exp (-1.0f / (0.002f * (float) hostRate)), cs = 1.0f - std::exp (-1.0f / (0.060f * (float) hostRate));
        const float cg = 1.0f - std::exp (-1.0f / (0.004f * (float) hostRate));
        float fast = 0.0f, slow = 0.0f, g = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float a = 0.0f;
            for (int c = 0; c < chs; ++c) a = juce::jmax (a, std::abs (s.play.getSample (c, i)));
            fast += (a - fast) * (a > fast ? 1.0f : cf);
            slow += (a - slow) * cs;
            const float want = juce::jlimit (0.0f, 1.0f, (fast / (slow + 1.0e-6f) - 2.0f) * 0.5f);
            g += (want - g) * (want > g ? 1.0f : cg);
            s.hitGate[(size_t) i] = g;
            for (int c = 0; c < chs; ++c) s.transient.setSample (c, i, s.play.getSample (c, i) * g);
        }
    }
}

// ---------------------------------------------------------------- player

double SamplePlayer::stretchFactor (float knob) noexcept
{
    return std::pow (2.0, 1.0 + 7.0 * (double) juce::jlimit (0.0f, 1.0f, knob));
}

void SamplePlayer::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& sp : spectrals)
        sp.prepare (sr);
    specL.assign ((size_t) kChunk, 0.0f);
    specR.assign ((size_t) kChunk, 0.0f);
    addL.assign ((size_t) kChunk, 0.0f);
    addR.assign ((size_t) kChunk, 0.0f);
    for (auto& a : additives)
        a.prepare();
    for (auto& a : morphs)
        a.prepare();
    for (auto& v : voices)
    {
        v.active = false;
        v.adsr.setSampleRate (sr);
    }
    for (auto& st : stretchers)
    {
        st = std::make_unique<Stretch>();
        st->presetCheaper (2, (float) sr);
    }
    const size_t cap = 900000; // enough for the largest read-ahead (very high keys)
    inL.assign (cap, 0.0f);
    inR.assign (cap, 0.0f);
    outL.assign ((size_t) kChunk, 0.0f);
    outR.assign ((size_t) kChunk, 0.0f);
}

void SamplePlayer::setSample (std::shared_ptr<const Sample> s)
{
    sample = std::move (s);
    for (auto& v : voices)
        v.active = false;
}

void SamplePlayer::fillInput (Voice& v, int count, const Params& p, float* l, float* r)
{
    juce::ignoreUnused (p);
    const auto& d = sample->play;
    const int n = d.getNumSamples();
    const float* sl = d.getReadPointer (0);
    const float* sr_ = d.getNumChannels() > 1 ? d.getReadPointer (1) : sl;
    for (int i = 0; i < count; ++i)
    {
        const int pos = (int) v.pos;
        if (pos >= 0 && pos < n)
        {
            l[i] = sl[pos];
            r[i] = sr_[pos];
        }
        else
            l[i] = r[i] = 0.0f;
        v.pos = (double) pos + 1.0;
    }
}

void SamplePlayer::noteOn (int note, float velocity, const Params& p)
{
    if (sample == nullptr || sample->play.getNumSamples() < 2)
        return;

    int index = -1;
    for (int i = 0; i < voiceLimit; ++i)
        if (! voices[(size_t) i].active) { index = i; break; }
    if (index < 0) // steal the oldest voice
    {
        index = 0;
        for (int i = 0; i < voiceLimit; ++i)
            if (voices[(size_t) i].startedAt < voices[(size_t) index].startedAt) index = i;
    }

    auto& v = voices[(size_t) index];
    v.active = true;
    v.note = note;
    v.pos = 0.0;
    v.inAccum = 0.0;
    v.velocity = velocity;
    v.startedAt = ++counter;
    v.additive = modelUsable (index, p);
    v.modelMix = p.model;
    v.morphAdd = v.additive && partnerModelUsable (p);
    // with the model at full strength and nothing else asked of the spectral engine, only the model plays
    v.spectral = index < kSpectralVoices && (v.additive ? p.model < 0.99f : (spectralOn (p) || partnerBuffer (p) != nullptr));
    v.stretched = ! v.spectral && index < kStretchVoices && (p.keepLength > 0.02f || std::abs (p.tempoRatio - 1.0) > 1.0e-4);
    v.adsr.setSampleRate (sr);
    v.adsr.setParameters ({ kAttackSec, 0.0f, 1.0f, p.releaseSec });
    v.adsr.noteOn();

    if (v.additive)
    {
        const auto seed = (juce::uint32) (v.startedAt * 2654435761u + (juce::uint32) note);
        if (v.morphAdd)
            morphs[(size_t) index].start (seed);
        else
            additives[(size_t) index].start (seed);
    }
    if (v.spectral)
    {
        const double R = std::pow (2.0, ((double) (note - 60) + (double) p.pitchSemis) / 12.0);
        spectrals[(size_t) index].start (sample->play, 0.0, R, partnerBuffer (p), p.morph, p.body);
    }
    else if (v.stretched)
    {
        auto& st = *stretchers[(size_t) index];
        const double R = std::pow (2.0, ((double) (note - 60) + (double) p.pitchSemis) / 12.0);
        const double speed = std::pow (R, 1.0 - (double) p.keepLength) * p.tempoRatio;
        st.setTransposeFactor ((float) R); // the pitch always follows the key; "keep length" only sets how fast we read
        // pre-roll so the sound starts right away instead of after the stretcher's latency
        const int len = juce::jmin ((int) inL.size(), st.outputSeekLength ((float) speed));
        fillInput (v, len, p, inL.data(), inR.data());
        float* ins[2] = { inL.data(), inR.data() };
        st.outputSeek (ins, len);
    }
}

void SamplePlayer::noteOff (int note, const Params& p)
{
    for (auto& v : voices)
        if (v.active && v.note == note && v.adsr.isActive())
        {
            v.adsr.setParameters ({ kAttackSec, 0.0f, 1.0f, p.releaseSec });
            v.adsr.noteOff();
        }
}

void SamplePlayer::renderTape (Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p)
{
    const auto& d = sample->play;
    const int n = d.getNumSamples();
    const double ratio = std::pow (2.0, ((double) (v.note - 60) + (double) p.pitchSemis) / 12.0);
    const float cut = juce::jlimit (0.25f, 1.0f, (float) (1.0 / ratio));
    const float gain = p.level * p.level * p.autoGain * v.velocity;
    const float* srcL = d.getReadPointer (0);
    const float* srcR = d.getNumChannels() > 1 ? d.getReadPointer (1) : srcL;
    float* oL = out.getWritePointer (0);
    float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    for (int i = 0; i < num; ++i)
    {
        if (v.pos >= (double) n)
        {
            v.active = false;
            return;
        }
        const float env = v.adsr.getNextSample() * gain;
        oL[start + i] += sincRead (srcL, n, v.pos, cut, 0, 0) * env;
        if (oR != nullptr)
            oR[start + i] += sincRead (srcR, n, v.pos, cut, 0, 0) * env;
        v.pos += ratio;
        if (! v.adsr.isActive())
        {
            v.active = false;
            return;
        }
    }
}

void SamplePlayer::renderStretch (int index, Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p)
{
    auto& st = *stretchers[(size_t) index];
    const int n = sample->play.getNumSamples();
    const double R = std::pow (2.0, ((double) (v.note - 60) + (double) p.pitchSemis) / 12.0);
    const double speed = std::pow (R, 1.0 - (double) p.keepLength) * p.tempoRatio; // locked to the host tempo: read faster or slower
    st.setTransposeFactor ((float) R); // the pitch always follows the key; "keep length" only sets how fast we read
    const float gain = p.level * p.level * p.autoGain * v.velocity;
    float* oL = out.getWritePointer (0);
    float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    int done = 0;
    while (done < num)
    {
        const int chunk = juce::jmin (kChunk, num - done);
        v.inAccum += (double) chunk * speed;
        const int inCount = juce::jmin ((int) inL.size(), (int) v.inAccum);
        v.inAccum -= (double) inCount;
        fillInput (v, inCount, p, inL.data(), inR.data());
        float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { outL.data(), outR.data() };
        st.process (ins, inCount, outs, chunk);
        for (int i = 0; i < chunk; ++i)
        {
            const float env = v.adsr.getNextSample() * gain;
            oL[start + done + i] += outL[(size_t) i] * env;
            if (oR != nullptr)
                oR[start + done + i] += outR[(size_t) i] * env;
        }
        done += chunk;
        const bool ended = v.pos >= (double) (n + 2 * st.blockSamples());
        if (ended || ! v.adsr.isActive())
        {
            v.active = false;
            return;
        }
    }
}

const juce::AudioBuffer<float>* SamplePlayer::partnerBuffer (const Params& p) const noexcept
{
    if (p.morph <= 0.01f || partner == nullptr || partner->play.getNumSamples() < 2)
        return nullptr;
    return &partner->play;
}

bool SamplePlayer::partnerModelUsable (const Params& p) const noexcept
{
    return p.morph > 0.01f && partner != nullptr && partner->model != nullptr && std::abs (partner->modelRate - sr) < 0.5
           && ! partner->model->ch.empty() && partner->model->ch[0].numFrames >= 2;
}

bool SamplePlayer::modelUsable (int index, const Params& p) const noexcept
{
    return index < kSpectralVoices && p.model > 0.01f && sample != nullptr && sample->model != nullptr
           && std::abs (sample->modelRate - sr) < 0.5 && ! sample->model->ch.empty() && sample->model->ch[0].numFrames >= 2;
}

void SamplePlayer::renderAdditive (int index, Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p)
{
    const double R = std::pow (2.0, ((double) (v.note - 60) + (double) p.pitchSemis) / 12.0);
    const double factor = (stretchOn (p.stretch) ? stretchFactor (p.stretch) : 1.0) / juce::jmax (0.25, p.tempoRatio);
    const double advance = (1.0 - (double) p.freeze) / factor;
    const float gain = p.level * p.level * p.autoGain * v.velocity;
    const float mix = v.morphAdd ? p.model : v.modelMix; // the pair morph can move while the note is held
    float* oL = out.getWritePointer (0);
    float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    int done = 0;
    while (done < num)
    {
        const int chunk = juce::jmin (kChunk, num - done);
        std::fill (addL.begin(), addL.begin() + chunk, 0.0f);
        std::fill (addR.begin(), addR.begin() + chunk, 0.0f);
        if (v.morphAdd)
        {
            // the partner can be swapped or removed while the note plays: then this voice simply goes on silent
            const auto pm = partner != nullptr ? partner->model : nullptr;
            if (pm != nullptr && std::abs (partner->modelRate - sr) < 0.5)
            {
                const double tp0 = morphs[(size_t) index].position();
                morphs[(size_t) index].process (*sample->model, *pm, advance, R, p.morph, p.modelNoise, addL.data(), addR.data(), chunk);
                // the hits of both sounds, which the sine description smooths away, are laid back over the morph
                {
                    const auto& ta = sample->transient; const auto& tb = partner->transient;
                    const int na = ta.getNumSamples(), nb2 = tb.getNumSamples();
                    // every held key lays back the same hits, so together they would add up louder than separate notes do:
                    // with N keys each one gets 1 / sqrt (N)
                    int keys = 0;
                    for (const auto& o : voices) if (o.active && o.morphAdd) ++keys;
                    const float hs = 1.0f / std::sqrt ((float) juce::jmax (1, keys));
                    const float wa = (1.0f - p.morph), wb = p.morph;
                    const float mg = wa * sample->modelGain + wb * partner->modelGain; // the description as loud as the sounds themselves
                    for (int i = 0; i < chunk; ++i)
                    {
                        const int q = (int) (tp0 + advance * (double) i);
                        const float gm = wa * (q < na ? sample->hitGate[(size_t) q] : 0.0f) + wb * (q < nb2 ? partner->hitGate[(size_t) q] : 0.0f);
                        addL[(size_t) i] *= mg * (1.0f - gm); addR[(size_t) i] *= mg * (1.0f - gm); // where a hit is laid back, the smoothed copy steps aside
                        if (q < na) { addL[(size_t) i] += hs * wa * ta.getSample (0, q); addR[(size_t) i] += hs * wa * ta.getSample (juce::jmin (1, ta.getNumChannels() - 1), q); }
                        if (q < nb2) { addL[(size_t) i] += hs * wb * tb.getSample (0, q); addR[(size_t) i] += hs * wb * tb.getSample (juce::jmin (1, tb.getNumChannels() - 1), q); }
                    }
                }
                // Auto level: the partner has its own level correction, so the morph moves from this sound's to the partner's
                if (std::abs (p.autoGain - sample->autoGain) < 1.0e-6f && sample->autoGain > 1.0e-6f)
                {
                    const float g = std::pow (juce::jlimit (0.06f, 16.0f, partner->autoGain / sample->autoGain), p.morph);
                    for (int i = 0; i < chunk; ++i) { addL[(size_t) i] *= g; addR[(size_t) i] *= g; }
                }
            }
        }
        else
            {
                additives[(size_t) index].setShape (nullptr);
                additives[(size_t) index].process (*sample->model, advance, R, addL.data(), addR.data(), chunk, p.modelNoise);
            }
        if (v.spectral)
            spectrals[(size_t) index].process (sample->play, factor, R, p.blur, p.freeze, specL.data(), specR.data(), chunk,
                                               partnerBuffer (p), p.morph, p.body);
        for (int i = 0; i < chunk; ++i)
        {
            const float env = v.adsr.getNextSample() * gain;
            const float l = v.spectral ? specL[(size_t) i] * (1.0f - mix) + addL[(size_t) i] * mix : addL[(size_t) i];
            const float r = v.spectral ? specR[(size_t) i] * (1.0f - mix) + addR[(size_t) i] * mix : addR[(size_t) i];
            oL[start + done + i] += l * env;
            if (oR != nullptr)
                oR[start + done + i] += r * env;
        }
        done += chunk;
        if (! v.adsr.isActive())
        {
            v.active = false;
            return;
        }
    }
}

void SamplePlayer::renderSpectral (int index, Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p)
{
    const double R = std::pow (2.0, ((double) (v.note - 60) + (double) p.pitchSemis) / 12.0);
    const double factor = (stretchOn (p.stretch) ? stretchFactor (p.stretch) : 1.0) / juce::jmax (0.25, p.tempoRatio);
    const float gain = p.level * p.level * p.autoGain * v.velocity;
    float* oL = out.getWritePointer (0);
    float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    int done = 0;
    while (done < num)
    {
        const int chunk = juce::jmin (kChunk, num - done);
        spectrals[(size_t) index].process (sample->play, factor, R, p.blur, p.freeze, specL.data(), specR.data(), chunk,
                                           partnerBuffer (p), p.morph, p.body);
        for (int i = 0; i < chunk; ++i)
        {
            const float env = v.adsr.getNextSample() * gain;
            oL[start + done + i] += specL[(size_t) i] * env;
            if (oR != nullptr)
                oR[start + done + i] += specR[(size_t) i] * env;
        }
        done += chunk;
        if (! v.adsr.isActive())
        {
            v.active = false;
            return;
        }
    }
}

void SamplePlayer::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, bool clearOutput)
{
    if (clearOutput)
        out.clear();
    const int total = out.getNumSamples();
    int done = 0;

    auto renderTo = [&] (int upTo)
    {
        const int num = upTo - done;
        if (num <= 0 || sample == nullptr)
        {
            done = juce::jmax (done, upTo);
            return;
        }
        for (int i = 0; i < kVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            if (! v.active)
                continue;
            if (v.additive)
                renderAdditive (i, v, out, done, num, p);
            else if (v.spectral)
                renderSpectral (i, v, out, done, num, p);
            else if (v.stretched)
                renderStretch (i, v, out, done, num, p);
            else
                renderTape (v, out, done, num, p);
        }
        done = upTo;
    };

    for (const auto meta : midi)
    {
        renderTo (juce::jlimit (0, total, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber(), p);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : voices)
                if (v.active) v.adsr.noteOff();
    }
    renderTo (total);
    {   // for the screen: where the first sounding voice reads
        float ph = -1.0f;
        const int n = sample != nullptr ? sample->play.getNumSamples() : 0;
        for (const auto& v : voices)
            if (v.active && n > 0) { ph = (float) juce::jlimit (0.0, 1.0, v.pos / (double) n); break; }
        playhead.store (ph);
    }
}
