#include "SamplePlayer.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>
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
void SamplePlayer::makePlayable (Sample& s, double hostRate)
{
    const auto& src0 = s.cleaned.getNumSamples() > 0 ? s.cleaned : s.data; // the cleaned sound, if there is one
    const int ch = src0.getNumChannels();
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

    // 2. find a steady region for the sustain loop
    const int N = s.play.getNumSamples();
    const int hop = 512, win = 1024;
    const int frames = juce::jmax (1, (N - win) / hop + 1);
    std::vector<float> env ((size_t) frames, 0.0f);
    float maxEnv = 1.0e-9f;
    int peak = 0;
    for (int k = 0; k < frames; ++k)
    {
        double sum = 0.0;
        const int a = k * hop, b = juce::jmin (N, a + win);
        for (int c = 0; c < ch; ++c)
        {
            const float* d = s.play.getReadPointer (c);
            for (int i = a; i < b; ++i)
                sum += (double) d[i] * d[i];
        }
        env[(size_t) k] = (float) std::sqrt (sum / (double) juce::jmax (1, (b - a) * ch));
        if (env[(size_t) k] > maxEnv) { maxEnv = env[(size_t) k]; peak = k; }
    }
    int lastLoud = peak;
    for (int k = 0; k < frames; ++k)
        if (env[(size_t) k] >= 0.25f * maxEnv)
            lastLoud = k;

    const int minLen = juce::jmin (N, (int) (0.15 * hostRate));
    const int maxLen = juce::jmin (N, (int) (2.0 * hostRate));
    int loopEnd = juce::jmin (N, lastLoud * hop + win);
    int loopStart = juce::jmax (0, N - minLen);
    if (loopEnd - minLen >= peak * hop)
    {
        float best = 1.0e9f;
        for (int k = peak; k * hop <= loopEnd - minLen; ++k)
        {
            const int len = loopEnd - k * hop;
            if (len > maxLen)
                continue;
            const float score = std::abs (env[(size_t) k] - env[(size_t) lastLoud]) / maxEnv
                              - 0.02f * (float) len / (float) maxLen;
            if (score < best) { best = score; loopStart = k * hop; }
        }
    }
    else
        loopEnd = N;
    if (loopEnd <= loopStart + 8)
    {
        loopStart = 0;
        loopEnd = N;
    }
    s.loopStart = loopStart;
    s.loopEnd = loopEnd;

    // 3. bake a smooth cross-fade into the end of the loop (equal-power), so the seam does not click
    s.playLoop = s.play;
    const int Lr = loopEnd - loopStart;
    const int X = juce::jmin (juce::jmin (Lr / 3, (int) (0.08 * hostRate)), loopStart);
    // Equal-power fade for unrelated material, equal-gain fade for matching material (no loudness bump on tones).
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (int c = 0; c < ch; ++c)
    {
        const float* d = s.play.getReadPointer (c);
        for (int i = 0; i < X; ++i)
        {
            const double a = d[loopEnd - X + i], b = d[loopEnd - X + i - Lr];
            ab += a * b; aa += a * a; bb += b * b;
        }
    }
    const float corr = juce::jlimit (0.0f, 1.0f, (float) (ab / std::sqrt (aa * bb + 1.0e-20)));
    const float e = 1.0f + corr;
    for (int c = 0; c < ch; ++c)
    {
        const float* d = s.play.getReadPointer (c);
        float* o = s.playLoop.getWritePointer (c);
        for (int i = 0; i < X; ++i)
        {
            const int p = loopEnd - X + i;
            const float w = ((float) i + 0.5f) / (float) X;
            o[p] = std::pow (std::cos (w * juce::MathConstants<float>::halfPi), e) * d[p]
                 + std::pow (std::sin (w * juce::MathConstants<float>::halfPi), e) * d[p - Lr];
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
    const auto& d = p.loop ? sample->playLoop : sample->play;
    const int n = d.getNumSamples();
    const float* sl = d.getReadPointer (0);
    const float* sr_ = d.getNumChannels() > 1 ? d.getReadPointer (1) : sl;
    const int ls = sample->loopStart, le = sample->loopEnd;
    for (int i = 0; i < count; ++i)
    {
        int pos = (int) v.pos;
        if (p.loop && le > ls)
            while (pos >= le)
                pos -= le - ls;
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
    v.stretched = ! v.spectral && index < kStretchVoices && p.keepLength > 0.02f;
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
        const double speed = std::pow (R, 1.0 - (double) p.keepLength);
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
    const auto& d = p.loop ? sample->playLoop : sample->play;
    const int n = d.getNumSamples();
    const int ls = p.loop ? sample->loopStart : 0, le = p.loop ? sample->loopEnd : 0;
    const double ratio = std::pow (2.0, ((double) (v.note - 60) + (double) p.pitchSemis) / 12.0);
    const float cut = juce::jlimit (0.25f, 1.0f, (float) (1.0 / ratio));
    const float gain = p.level * p.level * v.velocity;
    const float* srcL = d.getReadPointer (0);
    const float* srcR = d.getNumChannels() > 1 ? d.getReadPointer (1) : srcL;
    float* oL = out.getWritePointer (0);
    float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    for (int i = 0; i < num; ++i)
    {
        if (p.loop && le > ls)
            while (v.pos >= (double) le)
                v.pos -= (double) (le - ls);
        else if (v.pos >= (double) n)
        {
            v.active = false;
            return;
        }
        const float env = v.adsr.getNextSample() * gain;
        oL[start + i] += sincRead (srcL, n, v.pos, cut, ls, le) * env;
        if (oR != nullptr)
            oR[start + i] += sincRead (srcR, n, v.pos, cut, ls, le) * env;
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
    const double speed = std::pow (R, 1.0 - (double) p.keepLength);
    st.setTransposeFactor ((float) R); // the pitch always follows the key; "keep length" only sets how fast we read
    const float gain = p.level * p.level * v.velocity;
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
        const bool ended = ! p.loop && v.pos >= (double) (n + 2 * st.blockSamples());
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
    const double factor = stretchOn (p.stretch) ? stretchFactor (p.stretch) : 1.0;
    const double advance = (1.0 - (double) p.freeze) / factor;
    const float gain = p.level * p.level * v.velocity;
    const float mix = v.modelMix;
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
                morphs[(size_t) index].process (*sample->model, *pm, advance, R, p.morph, p.modelNoise, addL.data(), addR.data(), chunk);
        }
        else
            {
                additives[(size_t) index].setShape (bodyShapeOn ? &bodyShape : nullptr);
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
    const double factor = stretchOn (p.stretch) ? stretchFactor (p.stretch) : 1.0;
    const float gain = p.level * p.level * v.velocity;
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

    // Chromophonic: gain per twelfth of an octave that turns this sound's body into the partner's (at most +-12 dB)
    bodyShapeOn = false;
    if (p.chromo > 0.001f && sample != nullptr && bodyPartner != nullptr && sample->model != nullptr && bodyPartner->model != nullptr
        && sample->model->body.size() == (size_t) SineModel::kBodyBins && bodyPartner->model->body.size() == (size_t) SineModel::kBodyBins)
    {
        double eIn = 0.0, eOut = 0.0; // keep the loudness: the shaped sound has the same energy as the original
        for (int b = 0; b < SineModel::kBodyBins; ++b)
        {
            const float g = juce::jlimit (0.25f, 4.0f, std::sqrt (bodyPartner->model->body[(size_t) b] / sample->model->body[(size_t) b]));
            bodyRatio[(size_t) b] = std::pow (g, p.chromo);
            const double e = (double) sample->model->body[(size_t) b];
            eIn += e;
            eOut += e * (double) bodyRatio[(size_t) b] * (double) bodyRatio[(size_t) b];
        }
        const float norm = eOut > 0.0 ? (float) std::sqrt (eIn / eOut) : 1.0f;
        for (auto& r : bodyRatio) r *= norm;
        bodyShape.ratio = bodyRatio.data();
        bodyShape.wave = 1.6f * p.chromoMove;
        bodyShapeOn = true;
    }

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
}
