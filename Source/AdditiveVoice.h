#pragma once
#include "SineModel.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <array>

// Plays a SineModel: one sine oscillator per track, with amplitude and frequency following the analysis.
// The position in the sound moves by `advance` source samples per output sample (stretch, freeze), the pitch is
// scaled by `ratio` independently of that. Phases are the voice's own (random at the start), so it is not
// the original waveform, but the same partials. No allocation while playing: call prepare() first.
class AdditiveVoice
{
public:
    void prepare (bool needSines = true)
    {
        for (auto& p : phase)
            p.assign (needSines ? (size_t) SineModel::kMaxTracks : 0, 0.0);
        sinesReady = needSines;
    }

    void start (juce::uint32 seed)
    {
        pos = npos = (double) SineModel::kFirstCentre;
        primed = false;
        nrng.setSeed ((juce::int64) seed * 7 + 1);
        juce::Random rng ((juce::int64) seed);
        for (auto& p : phase)
            for (auto& v : p) v = (double) rng.nextFloat();
    }

    // Chromophonic: the partials of this sound are given the body of another (see SineModel::body).
    // ratio[b] = linear gain per twelfth of an octave from kBodyLow; wave = depth (natural log) of a slow filter-like
    // sweep that travels up the spectrum (about 6 s a turn), a little later in the right channel.
    struct BodyShape { const float* ratio = nullptr; float wave = 0.0f; float rate = 0.16f; }; // rate = turns of the sweep per second
    void setShape (const BodyShape* shape) noexcept { shape_ = shape; }

    // Adds to outL / outR (n samples). Returns nothing; silent if the model has no tracks.
    // `noiseAmount` (0..1) is the level of the residual (shaped noise), 1 = as strong as in the original.
    void process (const SineModel& m, double advance, double ratio, float* outL, float* outR, int n, float noiseAmount = 0.0f)
    {
        if (m.ch.empty() || m.ch[0].numFrames < 2)
            return;
        shapeOn = shape_ != nullptr && shape_->ratio != nullptr;
        if (shapeOn)
            for (int b = 0; b <= SineModel::kBands; ++b)
                bandGain[(size_t) b] = bodyGain (SineModel::kBandLow * std::pow (m.bandRatio, (double) b + 0.5));
        renderNoise (m, advance, ratio, outL, outR, n, noiseAmount);
        if (! sinesReady)
            return;
        constexpr int H = SineModel::kHop;
        const int frames = m.ch[0].numFrames;
        const double invSr = 1.0 / m.rate;
        const double fMax = 0.46 * m.rate;
        int done = 0;
        while (done < n)
        {
            double rel = (pos - (double) SineModel::kFirstCentre) / (double) H;
            rel = juce::jlimit (0.0, (double) (frames - 1) - 1.0e-6, rel); // at the end the last frame is held
            const int seg = (int) rel;
            const double u0 = rel - (double) seg;
            int nb = juce::jmin (kBlock, n - done);
            if (advance > 1.0e-9)
            {
                const double left = (1.0 - u0) * (double) H / advance;
                nb = juce::jmin (nb, juce::jmax (1, (int) std::ceil (left)));
            }
            const double u1 = juce::jmin (1.0, u0 + (double) nb * advance / (double) H);
            const double um = 0.5 * (u0 + u1);
            const float inv = 1.0f / (float) nb;

            for (int c = 0; c < 2; ++c)
            {
                const auto& chn = m.ch[(size_t) juce::jmin (c, (int) m.ch.size() - 1)];
                float* out = c == 0 ? outL : outR;
                if (out == nullptr || seg >= chn.numFrames)
                    continue;
                auto& ph = phase[(size_t) c];
                for (int s = chn.segStart[(size_t) seg]; s < chn.segStart[(size_t) seg + 1]; ++s)
                {
                    const auto& q = chn.segs[(size_t) s];
                    const double f = ((double) q.fa + ((double) q.fb - (double) q.fa) * um) * ratio;
                    if (f >= fMax)
                        continue;
                    float a0 = q.aa + (q.ab - q.aa) * (float) u0;
                    float a1 = q.aa + (q.ab - q.aa) * (float) u1;
                    if (shapeOn)
                    {
                        const double fm = (double) q.fa + ((double) q.fb - (double) q.fa) * um;
                        const float gb = bodyGain (fm);
                        float w0 = 1.0f, w1 = 1.0f;
                        if (shape_->wave > 0.0f)
                        {
                            const double t0 = (pos - (double) SineModel::kFirstCentre) / m.rate;
                            const double t1 = t0 + (double) nb * advance / m.rate;
                            const double oct = 0.55 * std::log2 (std::max (fm, 60.0) / 200.0);
                            const double ph = 0.25 * (double) c;
                            w0 = std::exp (shape_->wave * (float) std::sin (6.2831853 * ((double) shape_->rate * t0 + ph - oct)));
                            w1 = std::exp (shape_->wave * (float) std::sin (6.2831853 * ((double) shape_->rate * t1 + ph - oct)));
                        }
                        a0 *= gb * w0;
                        a1 *= gb * w1;
                    }
                    double p = ph[(size_t) q.track];
                    const double w = f * invSr;
                    const float twoPi = 6.2831853f;
                    float zr = std::cos (twoPi * (float) p), zi = std::sin (twoPi * (float) p);
                    const float rr = std::cos (twoPi * (float) w), ri = std::sin (twoPi * (float) w);
                    float a = a0;
                    const float da = (a1 - a0) * inv;
                    for (int i = 0; i < nb; ++i)
                    {
                        out[done + i] += a * zr;
                        const float nr = zr * rr - zi * ri;
                        zi = zr * ri + zi * rr;
                        zr = nr;
                        a += da;
                    }
                    p += w * (double) nb;
                    ph[(size_t) q.track] = p - std::floor (p);
                }
            }
            pos += advance * (double) nb;
            done += nb;
        }
    }

private:
    float bodyGain (double f) const noexcept
    {
        const int b = juce::jlimit (0, SineModel::kBodyBins - 1, (int) std::floor (12.0 * std::log2 (std::max (f, SineModel::kBodyLow) / SineModel::kBodyLow)));
        return shape_->ratio[b];
    }
    const BodyShape* shape_ = nullptr;
    bool shapeOn = false;
    std::array<float, SineModel::kBands + 1> bandGain {};
    static constexpr int kBlock = 64;
    static constexpr int kM = 1024, kMHop = 256; // residual: synthesis frame and hop

    // The residual is made frame by frame: shaped, random-phase spectrum -> inverse FFT -> Hann window -> overlap-add.
    void synthHop (const SineModel& m, double ratio, float amount)
    {
        const double rel = juce::jlimit (0.0, (double) (m.ch[0].numFrames - 1) - 1.0e-6,
                                         (npos - (double) SineModel::kFirstCentre) / (double) SineModel::kHop);
        const int f0 = (int) rel;
        const float u = (float) (rel - f0);
        constexpr int B = SineModel::kBands;
        if (ratio != tableRatio)
            buildBinTable (m, ratio);
        for (int c = 0; c < 2; ++c)
        {
            const auto& chn = m.ch[(size_t) juce::jmin (c, (int) m.ch.size() - 1)];
            auto& ring = ola[(size_t) c];
            if (chn.noise.empty() || amount <= 0.0f)
            {
                for (int i = 0; i < kMHop; ++i) hopOut[(size_t) c][(size_t) i] = ring[(size_t) ((base + i) & (kM - 1))], ring[(size_t) ((base + i) & (kM - 1))] = 0.0f;
                continue;
            }
            const float* na = chn.noise.data() + (size_t) f0 * B;
            const float* nb = na + B;
            for (int k = 0; k < kM * 2; ++k) fbuf[(size_t) k] = 0.0f;
            for (int k = 1; k < kM / 2; ++k)
            {
                const int b = binBand[(size_t) k];
                float amp;
                if (b < 0)
                    amp = 0.0f; // above the top band
                else
                {
                    const float t = binFrac[(size_t) k];
                    amp = (na[b] * (1.0f - u) + nb[b] * u) * (1.0f - t) + (na[b + 1] * (1.0f - u) + nb[b + 1] * u) * t;
                    if (shapeOn)
                        amp *= bandGain[(size_t) b] * (1.0f - t) + bandGain[(size_t) b + 1] * t;
                }
                const int pi = (int) (nrng.nextInt() & (kPhases - 1));
                fbuf[(size_t) (2 * k)] = amp * phaseCos[(size_t) pi];
                fbuf[(size_t) (2 * k + 1)] = amp * phaseSin[(size_t) pi];
            }
            fft.performRealOnlyInverseTransform (fbuf.data());
            for (int i = 0; i < kM; ++i)
                ring[(size_t) ((base + i) & (kM - 1))] += fbuf[(size_t) i] * hann[(size_t) i];
            for (int i = 0; i < kMHop; ++i)
            {
                hopOut[(size_t) c][(size_t) i] = ring[(size_t) ((base + i) & (kM - 1))];
                ring[(size_t) ((base + i) & (kM - 1))] = 0.0f;
            }
        }
        base = (base + kMHop) & (kM - 1);
    }

    // For every bin of the synthesis frame: which pair of bands it lies between (b, b + 1) and where.
    void buildBinTable (const SineModel& m, double ratio)
    {
        constexpr int B = SineModel::kBands;
        const double lr = std::log (m.bandRatio);
        binBand.assign ((size_t) kM / 2, -1);
        binFrac.assign ((size_t) kM / 2, 0.0f);
        for (int k = 1; k < kM / 2; ++k)
        {
            const double f = (double) k * m.rate / kM / ratio; // the sound's own frequency that lands on this bin
            const double bp = f <= SineModel::kBandLow ? -0.5 : std::log (f / SineModel::kBandLow) / lr - 0.5; // band centres sit at b + 0.5
            if (bp > B - 0.5)
                continue;
            const double q = juce::jlimit (0.0, (double) (B - 1) - 1.0e-9, bp);
            binBand[(size_t) k] = (int) q;
            binFrac[(size_t) k] = (float) (q - (int) q);
        }
        tableRatio = ratio;
    }

    void renderNoise (const SineModel& m, double advance, double ratio, float* outL, float* outR, int n, float amount)
    {
        if (amount <= 0.0005f)
        {
            // keep the position moving and the frame clock running without making any noise
            npos += advance * (double) n;
            primed = false;
            return;
        }
        if (! primed)
        {
            for (auto& r : ola) std::fill (r.begin(), r.end(), 0.0f);
            base = 0;
            for (int i = 0; i < kM / kMHop - 1; ++i) synthHop (m, ratio, amount); // warm up the overlap
            hopIdx = 0;
            primed = true;
            synthHop (m, ratio, amount);
        }
        for (int i = 0; i < n; ++i)
        {
            if (hopIdx >= kMHop)
            {
                synthHop (m, ratio, amount);
                hopIdx = 0;
            }
            outL[i] += hopOut[0][(size_t) hopIdx] * amount;
            if (outR != nullptr)
                outR[i] += hopOut[1][(size_t) hopIdx] * amount;
            ++hopIdx;
            npos += advance;
        }
    }
    static constexpr float kNoiseScale = 25.4f; // calibrated in a test: white-noise strength 1 over 0..0.46 fs -> standard deviation 0.96
    juce::dsp::FFT fft { 10 };
    std::vector<float> fbuf = std::vector<float> ((size_t) kM * 2, 0.0f);
    std::array<std::vector<float>, 2> ola { std::vector<float> ((size_t) kM, 0.0f), std::vector<float> ((size_t) kM, 0.0f) };
    std::array<std::array<float, kMHop>, 2> hopOut {};
    std::vector<float> hann = [] { std::vector<float> w (kM); for (int i = 0; i < kM; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) kM); return w; }();
    juce::Random nrng;
    static constexpr int kPhases = 4096;
    std::vector<float> phaseCos = [] { std::vector<float> v (kPhases); for (int i = 0; i < kPhases; ++i) v[(size_t) i] = std::cos (6.2831853f * (float) i / (float) kPhases) * kNoiseScale; return v; }();
    std::vector<float> phaseSin = [] { std::vector<float> v (kPhases); for (int i = 0; i < kPhases; ++i) v[(size_t) i] = std::sin (6.2831853f * (float) i / (float) kPhases) * kNoiseScale; return v; }();
    std::vector<int> binBand;
    std::vector<float> binFrac;
    double tableRatio = -1.0;
    double npos = (double) SineModel::kFirstCentre;
    int base = 0, hopIdx = 0;
    bool primed = false;
    bool sinesReady = true;
    std::vector<double> phase[2];
    double pos = (double) SineModel::kFirstCentre;
};
