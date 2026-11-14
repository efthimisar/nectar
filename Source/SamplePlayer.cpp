#include "SamplePlayer.h"
#include <cmath>
#include <array>

namespace
{
// Windowed-sinc kernel (Blackman window), 8 zero crossings each side, sampled finely.
// Reading the sample faster than the output rate (high notes) uses a stretched kernel = low-pass,
// so frequencies above the output's Nyquist are removed instead of folding back as metallic aliasing.
constexpr int kHalf = 8;
constexpr int kRes = 256;

const float* kernelTable()
{
    static const std::array<float, kHalf * kRes + 2> table = []
    {
        std::array<float, kHalf * kRes + 2> t {};
        for (size_t j = 0; j < t.size(); ++j)
        {
            const double u = (double) j / kRes;
            const double s = u < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * u) / (juce::MathConstants<double>::pi * u);
            const double x = juce::jmin (1.0, u / kHalf);
            const double w = 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * x)
                                  + 0.08 * std::cos (2.0 * juce::MathConstants<double>::pi * x);
            t[j] = (float) (s * w);
        }
        return t;
    }();
    return table.data();
}
} // namespace

void SamplePlayer::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& v : voices)
    {
        v.active = false;
        v.adsr.setSampleRate (sr);
    }
}

void SamplePlayer::setSample (std::shared_ptr<const Sample> s)
{
    sample = std::move (s);
    for (auto& v : voices)
        v.active = false;
}

void SamplePlayer::noteOn (int note, float velocity, const Params& p)
{
    if (sample == nullptr || sample->data.getNumSamples() < 2)
        return;

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }
    if (target == nullptr) // steal the oldest voice
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.startedAt < target->startedAt) target = &v;
    }

    target->active = true;
    target->note = note;
    target->pos = 0.0;
    target->velocity = velocity;
    target->startedAt = ++counter;
    target->adsr.setSampleRate (sr);
    target->adsr.setParameters ({ kAttackSec, 0.0f, 1.0f, p.releaseSec });
    target->adsr.noteOn();
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

void SamplePlayer::renderVoice (Voice& v, juce::AudioBuffer<float>& out, int start, int num, const Params& p)
{
    if (sample == nullptr)
    {
        v.active = false;
        return;
    }

    const auto& d = sample->data;
    const int n = d.getNumSamples();
    const int srcCh = d.getNumChannels();
    const double ratio = (sample->rate / sr) * std::pow (2.0, ((double) (v.note - 60) + (double) p.pitchSemis) / 12.0);
    const float gain = p.level * p.level * v.velocity;

    float* outL = out.getWritePointer (0);
    float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
    const float* srcL = d.getReadPointer (0);
    const float* srcR = srcCh > 1 ? d.getReadPointer (1) : srcL;

    for (int i = 0; i < num; ++i)
    {
        if (v.pos >= (double) n)
        {
            if (p.loop)
                v.pos = std::fmod (v.pos, (double) n);
            else
            {
                v.active = false;
                return;
            }
        }

        const int i0 = (int) v.pos;
        const float* T = kernelTable();
        const float cut = juce::jlimit (0.25f, 1.0f, (float) (1.0 / ratio)); // low-pass when reading fast
        const int hw = (int) std::ceil ((float) kHalf / cut);

        float accL = 0.0f, accR = 0.0f, wsum = 0.0f;
        for (int k = 1 - hw; k <= hw; ++k)
        {
            const int idx = i0 + k;
            const float u = std::abs ((float) ((double) idx - v.pos)) * cut;
            if (u >= (float) kHalf)
                continue;
            const float fi = u * (float) kRes;
            const int ti = (int) fi;
            const float w = cut * (T[ti] + (T[ti + 1] - T[ti]) * (fi - (float) ti));
            wsum += w; // counted even outside the sample (silence), so the ends fade instead of jumping
            int ii = idx;
            if (ii < 0 || ii >= n)
            {
                if (! p.loop)
                    continue;
                ii = ((ii % n) + n) % n;
            }
            accL += w * srcL[ii];
            accR += w * srcR[ii];
        }
        const float norm = wsum > 1.0e-6f ? 1.0f / wsum : 0.0f;
        const float l = accL * norm;
        const float r = accR * norm;
        const float env = v.adsr.getNextSample() * gain;

        outL[start + i] += l * env;
        if (outR != nullptr)
            outR[start + i] += r * env;

        v.pos += ratio;

        if (! v.adsr.isActive())
        {
            v.active = false;
            return;
        }
    }
}

void SamplePlayer::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
{
    out.clear();
    const int total = out.getNumSamples();
    int done = 0;

    auto renderTo = [&] (int upTo)
    {
        const int num = upTo - done;
        if (num <= 0)
            return;
        for (auto& v : voices)
            if (v.active)
                renderVoice (v, out, done, num, p);
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
}
