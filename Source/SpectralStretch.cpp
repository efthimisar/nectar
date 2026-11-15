#include "SpectralStretch.h"
#include <cmath>

void SpectralStretch::prepare (double sampleRate)
{
    rate = sampleRate;
    order = sampleRate > 60000.0 ? 15 : 14; // about 0.37 s of sound per frame
    N = 1 << order;
    hop = N / 2;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i) // sine window: two frames shifted by half a frame add up to a constant
        window[(size_t) i] = std::sin (juce::MathConstants<float>::pi * ((float) i + 0.5f) / (float) N);
    work.assign ((size_t) N * 2, 0.0f);
    mag.assign ((size_t) N / 2 + 1, 0.0f);
    shifted.assign ((size_t) N / 2 + 1, 0.0f);
    smooth.assign ((size_t) N / 2 + 1, 0.0f);
    prefix.assign ((size_t) N / 2 + 2, 0.0f);
    workB.assign ((size_t) N * 2, 0.0f);
    magB.assign ((size_t) N / 2 + 1, 0.0f);
    envA.assign ((size_t) N / 2 + 1, 0.0f);
    envB.assign ((size_t) N / 2 + 1, 0.0f);
    envTmp.assign ((size_t) N / 2 + 1, 0.0f);
    srcSpec.assign ((size_t) N / 2 + 1, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        tail[c].assign ((size_t) hop, 0.0f);
        hopOut[c].assign ((size_t) hop, 0.0f);
    }
    readIdx = hop;
}

void SpectralStretch::start (const juce::AudioBuffer<float>& src, double startPos, double ratio,
                             const juce::AudioBuffer<float>* partner, float morph, Body body)
{
    const int n = src.getNumSamples();
    pos = n > 0 ? std::fmod (juce::jmax (0.0, startPos), (double) n) : 0.0;
    posB = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        std::fill (tail[c].begin(), tail[c].end(), 0.0f);
        std::fill (hopOut[c].begin(), hopOut[c].end(), 0.0f);
    }
    // One frame in advance, so that the very first output is already at full level (no slow fade-in).
    makeFrame (src, 1.0, ratio, 0.0f, 0.0f, false, partner, morph, body);
    readIdx = hop; // the next call to process() makes the second frame
}

void SpectralStretch::envelope (const float* in, float* out)
{
    const int bins = N / 2;
    const int hwMin = juce::jmax (2, (int) std::lround (150.0 * (double) N / rate)); // never narrower than about +-150 Hz
    std::copy (in, in + bins + 1, envTmp.begin());
    for (int pass = 0; pass < 2; ++pass)
    {
        prefix[0] = 0.0f;
        for (int k = 0; k <= bins; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + envTmp[(size_t) k];
        for (int k = 0; k <= bins; ++k)
        {
            const int hw = juce::jmax (hwMin, (int) (0.15 * (double) k)); // wider at high frequencies, like hearing
            const int a = juce::jmax (0, k - hw), b = juce::jmin (bins, k + hw);
            out[k] = (prefix[(size_t) b + 1] - prefix[(size_t) a]) / (float) (b - a + 1);
        }
        std::copy (out, out + bins + 1, envTmp.begin());
    }
}

void SpectralStretch::applyBody (const Body& body)
{
    const int bins = N / 2;
    envelope (shifted.data(), envA.data());
    float maxE = 0.0f;
    double before = 0.0;
    for (int k = 0; k <= bins; ++k)
    {
        maxE = juce::jmax (maxE, envA[(size_t) k]);
        before += (double) shifted[(size_t) k] * shifted[(size_t) k];
    }
    if (maxE < 1.0e-12f)
        return;
    const float fl = 1.0e-3f * maxE;
    // source = the fine structure with the envelope taken out
    for (int k = 0; k <= bins; ++k)
        srcSpec[(size_t) k] = shifted[(size_t) k] / (envA[(size_t) k] + fl);

    // body: Size moves the envelope along the frequency axis (+-0.75 octave), Focus changes its contrast, Tone tilts it
    const double sh = std::pow (2.0, (double) (body.size - 0.5f) * 1.5); // size 0 -> 0.6 (envelope moves up), size 1 -> 1.7 (moves down)
    const float expo = std::pow (2.0f, (body.focus - 0.5f) * 2.0f);     // 0.5 .. 2
    const float tiltDbPerOct = (body.tone - 0.5f) * 2.0f * 4.0f;          // +-4 dB per octave, only between 150 Hz and 8 kHz (no boost of rumble or hiss)
    const double binHz = rate / (double) N;
    double after = 0.0;
    for (int k = 0; k <= bins; ++k)
    {
        // envelope moved: new envelope at k = old envelope at k * sh (size small -> sh < 1 -> envelope moves up)
        const double q = (double) k * sh;
        float e;
        if (q >= (double) bins) e = envA[(size_t) bins];
        else
        {
            const int i0 = (int) q;
            const float f = (float) (q - (double) i0);
            e = envA[(size_t) i0] * (1.0f - f) + envA[(size_t) juce::jmin (bins, i0 + 1)] * f;
        }
        e = maxE * std::pow ((e + fl) / (maxE + fl), expo);
        const double hz = juce::jlimit (150.0, 8000.0, (double) k * binHz);
        e *= std::pow (10.0f, tiltDbPerOct * (float) std::log2 (hz / 1000.0) / 20.0f);
        shifted[(size_t) k] = srcSpec[(size_t) k] * e;
        after += (double) shifted[(size_t) k] * shifted[(size_t) k];
    }
    const float fix = (float) std::sqrt (before / juce::jmax (1.0e-20, after)); // loudness stays
    for (int k = 0; k <= bins; ++k) shifted[(size_t) k] *= fix;
}

void SpectralStretch::applyMorph (const juce::AudioBuffer<float>& partner, int channel, float morph)
{
    const int nb = partner.getNumSamples();
    const int bins = N / 2;
    const float* s = partner.getReadPointer (juce::jmin (channel, partner.getNumChannels() - 1));
    const int base = (int) posB;
    for (int i = 0; i < N; ++i)
    {
        int idx = base + i;
        if (idx >= nb) idx %= nb;
        workB[(size_t) i] = s[idx] * window[(size_t) i];
    }
    std::fill (workB.begin() + N, workB.end(), 0.0f);
    fft->performRealOnlyForwardTransform (workB.data(), true);
    for (int k = 0; k <= bins; ++k)
        magB[(size_t) k] = std::hypot (workB[(size_t) (2 * k)], workB[(size_t) (2 * k + 1)]);

    envelope (shifted.data(), envA.data());
    envelope (magB.data(), envB.data());

    float maxA = 0.0f, maxB = 0.0f;
    double before = 0.0;
    for (int k = 0; k <= bins; ++k)
    {
        maxA = juce::jmax (maxA, envA[(size_t) k]);
        maxB = juce::jmax (maxB, envB[(size_t) k]);
        before += (double) shifted[(size_t) k] * shifted[(size_t) k];
    }
    if (maxA < 1.0e-12f || maxB < 1.0e-12f)
        return; // one of the two is silent here: nothing to morph
    const float flA = 1.0e-3f * maxA, flB = 1.0e-3f * maxB; // floors, so that empty regions do not explode
    double after = 0.0;
    for (int k = 0; k <= bins; ++k)
    {
        const float ratio = (envB[(size_t) k] + flB) / (envA[(size_t) k] + flA);
        const float g = juce::jlimit (0.01f, 100.0f, std::pow (ratio, morph));
        shifted[(size_t) k] *= g;
        after += (double) shifted[(size_t) k] * shifted[(size_t) k];
    }
    const float fix = (float) std::sqrt (before / juce::jmax (1.0e-20, after)); // the loudness stays that of this sound
    for (int k = 0; k <= bins; ++k) shifted[(size_t) k] *= fix;
}

void SpectralStretch::makeFrame (const juce::AudioBuffer<float>& src, double stretch, double ratio, float blur, float freeze, bool advance,
                                 const juce::AudioBuffer<float>* partner, float morph, const Body& body)
{
    const int n = src.getNumSamples();
    const int chs = src.getNumChannels();
    if (n < 2)
        return;

    for (int c = 0; c < 2; ++c)
    {
        const float* s = src.getReadPointer (juce::jmin (c, chs - 1));
        const int base = (int) pos;
        for (int i = 0; i < N; ++i)
        {
            int idx = base + i;
            if (idx >= n) idx %= n;
            work[(size_t) i] = s[idx] * window[(size_t) i];
        }
        std::fill (work.begin() + N, work.end(), 0.0f);
        fft->performRealOnlyForwardTransform (work.data(), true);

        const int bins = N / 2;
        for (int k = 0; k <= bins; ++k)
            mag[(size_t) k] = std::hypot (work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)]);

        // pitch: bin k takes the magnitude found at k / ratio (frequencies above the top are dropped)
        const double inv = 1.0 / juce::jmax (0.01, ratio);
        for (int k = 0; k <= bins; ++k)
        {
            const double q = (double) k * inv;
            const int i0 = (int) q;
            if (i0 >= bins) { shifted[(size_t) k] = 0.0f; continue; }
            const float f = (float) (q - (double) i0);
            shifted[(size_t) k] = mag[(size_t) i0] * (1.0f - f) + mag[(size_t) (i0 + 1)] * f;
        }

        if (partner != nullptr && morph > 0.01f && partner->getNumSamples() >= 2)
            applyMorph (*partner, c, morph);

        if (body.shapeActive())
            applyBody (body);

        if (body.rough > 0.01f)
        {
            // Rough: every bin gets a random level change per ~60 Hz patch (about 10 dB spread at 1, in patches of ~60 Hz), the loudness is kept
            const float amt = body.rough * 1.2f;
            const int spacing = juce::jmax (2, (int) std::lround (60.0 / ((double) rate / (double) N))); // a random level every ~60 Hz
            double before = 0.0, after = 0.0;
            float g0 = std::exp (amt * (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 1.5f) * 2.0f), g1 = g0;
            for (int k = 0; k <= bins; ++k)
            {
                if (k % spacing == 0)
                {
                    g0 = g1;
                    g1 = std::exp (amt * (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 1.5f) * 2.0f);
                }
                const float t = (float) (k % spacing) / (float) spacing;
                before += (double) shifted[(size_t) k] * shifted[(size_t) k];
                shifted[(size_t) k] *= g0 + (g1 - g0) * t;
                after += (double) shifted[(size_t) k] * shifted[(size_t) k];
            }
            if (after > 1e-20)
            {
                const float g = (float) std::sqrt (before / after);
                for (int k = 0; k <= bins; ++k) shifted[(size_t) k] *= g;
            }
        }

        if (blur > 0.01f)
        {
            // smooth the spectrum with a triangle (two box filters); the loudness is kept
            const int hw = juce::jmax (1, (int) std::lround ((1.0 + 79.0 * (double) blur * blur) * (double) N / 16384.0));
            double before = 0.0;
            for (int k = 0; k <= bins; ++k) before += (double) shifted[(size_t) k] * shifted[(size_t) k];
            for (int pass = 0; pass < 2; ++pass)
            {
                prefix[0] = 0.0f;
                for (int k = 0; k <= bins; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + shifted[(size_t) k];
                for (int k = 0; k <= bins; ++k)
                {
                    const int a = juce::jmax (0, k - hw), b = juce::jmin (bins, k + hw);
                    smooth[(size_t) k] = (prefix[(size_t) b + 1] - prefix[(size_t) a]) / (float) (b - a + 1);
                }
                shifted.swap (smooth);
            }
            double after = 0.0;
            for (int k = 0; k <= bins; ++k) after += (double) shifted[(size_t) k] * shifted[(size_t) k];
            const float fix = (float) std::sqrt (before / juce::jmax (1.0e-20, after));
            for (int k = 0; k <= bins; ++k) shifted[(size_t) k] *= fix;
        }

        for (int k = 0; k <= bins; ++k)
        {
            const float ph = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const bool edge = (k == 0 || k == bins); // DC and the top bin must stay real
            work[(size_t) (2 * k)] = shifted[(size_t) k] * (edge ? 1.0f : std::cos (ph));
            work[(size_t) (2 * k + 1)] = edge ? 0.0f : shifted[(size_t) k] * std::sin (ph);
        }
        fft->performRealOnlyInverseTransform (work.data());

        // overlap-add: first half joins the stored tail, second half is kept for the next frame
        const float gain = kGain / (float) std::sqrt (juce::jmax (0.05, ratio)); // moving bins up/down changes the energy by about the ratio
        for (int i = 0; i < hop; ++i)
            hopOut[c][(size_t) i] = tail[c][(size_t) i] + work[(size_t) i] * window[(size_t) i] * gain;
        for (int i = 0; i < hop; ++i)
            tail[c][(size_t) i] = work[(size_t) (hop + i)] * window[(size_t) (hop + i)] * gain;
    }

    if (advance)
    {
        pos += (double) hop / juce::jmax (1.0, stretch) * (1.0 - (double) juce::jlimit (0.0f, 1.0f, freeze));
        pos = std::fmod (pos, (double) n);
        if (partner != nullptr && partner->getNumSamples() >= 2)
        {
            posB += (double) hop / juce::jmax (1.0, stretch) * (1.0 - (double) juce::jlimit (0.0f, 1.0f, freeze));
            posB = std::fmod (posB, (double) partner->getNumSamples());
        }
    }
}

void SpectralStretch::process (const juce::AudioBuffer<float>& src, double stretch, double ratio, float blur, float freeze,
                               float* outL, float* outR, int num,
                               const juce::AudioBuffer<float>* partner, float morph, Body body)
{
    for (int i = 0; i < num; ++i)
    {
        if (readIdx >= hop)
        {
            makeFrame (src, stretch, ratio, blur, freeze, true, partner, morph, body);
            readIdx = 0;
        }
        outL[i] = hopOut[0][(size_t) readIdx];
        outR[i] = hopOut[1][(size_t) readIdx];
        ++readIdx;
    }
}
