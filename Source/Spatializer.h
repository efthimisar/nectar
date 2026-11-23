#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// Binaural placement of one mono source around the listener's head (for headphones), after the structure of
// Brown & Duda (1998, IEEE Trans. Speech Audio Process., "A structural model for binaural sound synthesis"):
//   - interaural time difference: Woodworth's spherical-head formula ITD = (a/c)(lat + sin lat); its use as a good
//     approximation is supported by Duda & Martens (1998, JASA, "Range dependence of the response of a spherical head model")
//   - head shadow: one pole / one zero filter per ear, H(s) = (1 + alpha(phi) s/(2 w0)) / (1 + s/(2 w0)), w0 = c/a,
//     alpha(phi) = 1 + amin/2 + (1 - amin/2) cos(phi/phimin * 180 deg), amin = 0.1, phimin = 150 deg, ears at +-100 deg.
//     (Eqs. 2-5 and alpha_min = 0.1, phimin = 150 deg checked against the paper's text, 15 Nov 2026)
// Our own additions (no published source): darker sound behind the listener, distance = level + high-frequency loss,
// and the speaker mode (plain pan from the azimuth, no time or head-shadow cues, which would only colour the sound on speakers;
// distance there = level, high-frequency loss and a narrower sound; behind = darker, as on headphones).
class Spatializer
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset();
    }
    void reset()
    {
        for (auto& v : line) v = 0.0f;
        wpos = 0;
        for (auto& e : ear) e = Ear();
        backSh = Ear();
        lpState = 0.0f; lpSide = 0.0f;
        prevDelay = { 0.0f, 0.0f };
        prevAz = -1000.0f;
        prevEarG = { 1.0f, 1.0f }; prevSideK = -1.0f; prevCoef = false;
    }

    struct Params
    {
        float azDeg = 0.0f;  // -180..180, 0 = in front, +90 = right
        float dist = 0.3f;   // 0..1  (0.5 m .. 8 m)
        float far = 0.5f;    // how strongly distance lowers level and brightness
        float back = 0.7f;   // how dark the sound gets behind the listener
        bool speakers = false;
    };

    // in: mono, n samples. outL / outR: the placed signal (overwritten).
    // side: (left - right) / 2 of a stereo sound, or nullptr. It is added around the placed point (our own addition), so a wide
    // sound keeps its width; it gets the same distance / back treatment, but no direction cues.
    void process (const float* in, const float* side, float* outL, float* outR, int n, const Params& p)
    {
        constexpr float pi = juce::MathConstants<float>::pi;
        constexpr float a = 0.0875f, c = 343.0f;
        const float th = juce::degreesToRadians (p.azDeg);
        const float lat = std::asin (juce::jlimit (-1.0f, 1.0f, std::sin (th))); // -90..90: front and back give the same lateral angle
        const float cosTh = std::cos (th);

        // shared (before the ears): distance and back darkening
        const float r = 0.5f * std::pow (16.0f, juce::jlimit (0.0f, 1.0f, p.dist));
        const float gDist = std::pow (0.5f / r, juce::jlimit (0.0f, 1.0f, p.far));
        const float backAmt = juce::jlimit (0.0f, 1.0f, -cosTh) * juce::jlimit (0.0f, 1.0f, p.back);
        const float fcDist = 20000.0f * std::pow (2500.0f / 20000.0f, juce::jlimit (0.0f, 1.0f, p.dist) * p.far);
        // behind: darker (the head model above alone makes a sound behind a little brighter than in front, since the ears sit
        // slightly behind the middle; the outer ear, which it leaves out, does the opposite, so the darkening has to win)
        const float fcBack = 16000.0f * std::pow (2500.0f / 16000.0f, backAmt);
        const float fc = juce::jmin (fcDist, fcBack, 0.45f * (float) sr);
        const float lpA = 1.0f - std::exp (-2.0f * pi * fc / (float) sr);
        const float gBack = std::pow (10.0f, -2.5f * backAmt / 20.0f);
        // a wide (stereo) sound placed to one side gets narrower, so it is heard on that side and not still all around
        // (our own choice: its width falls to a quarter at the far left or right)
        const float sideK = 1.0f - 0.75f * std::abs (std::sin (th));
        const float g0 = gDist * gBack * kMakeup;

        // behind: a high shelf of the same shape as the head shadow (above about 1 kHz) takes the highs down,
        // since on headphones that is the range where the head model leaves a sound behind brighter than one in front
        // (on speakers the same shelf, so behind sounds alike in both)
        Ear backTo;
        {
            const float beta = (float) sr / (c / a);
            const float ab = 1.0f - 0.75f * backAmt;
            const float b0 = 1.0f + beta * ab, b1 = 1.0f - beta * ab, a0 = 1.0f + beta, a1 = 1.0f - beta;
            backTo.b0 = b0 / a0; backTo.b1 = b1 / a0; backTo.a1 = a1 / a0;
        }
        // the filters' coefficients slide from the last block's to this one's (a step in a coefficient is a click when the sound moves fast)
        const Ear backFrom = prevCoef ? prevBack : backTo;
        const float g0From = prevCoef ? prevG0 : g0, lpFrom = prevCoef ? prevLpA : lpA, invN = 1.0f / (float) juce::jmax (1, n);
        if (p.speakers)
        {
            const float pan = std::sin (th); // -1 left .. 1 right
            const float tp = (pan + 1.0f) * 0.25f * pi;
            const float gl = std::sqrt (2.0f) * std::cos (tp), gr = std::sqrt (2.0f) * std::sin (tp);
            // on speakers a far sound is also narrower (its own width shrinks to a third at the far edge; our own choice)
            const float sk0 = prevSideK < 0.0f ? sideK : prevSideK, invS = 1.0f / (float) juce::jmax (1, n);
            prevSideK = sideK;
            const float narrow = 1.0f - 0.67f * juce::jlimit (0.0f, 1.0f, p.dist) * juce::jlimit (0.0f, 1.0f, p.far);
            for (int i = 0; i < n; ++i)
            {
                const float t = (float) (i + 1) * invN;
                const float lpI = lpFrom + (lpA - lpFrom) * t;
                lpState += lpI * (in[i] - lpState);
                const float yb = (backFrom.b0 + (backTo.b0 - backFrom.b0) * t) * lpState + backSh.z;
                backSh.z = (backFrom.b1 + (backTo.b1 - backFrom.b1) * t) * lpState - (backFrom.a1 + (backTo.a1 - backFrom.a1) * t) * yb;
                const float y = yb * (g0From + (g0 - g0From) * t); // (the same level trim as on headphones, so switching does not change the loudness)
                float sd = 0.0f;
                if (side != nullptr) { lpSide += lpA * (side[i] - lpSide); sd = lpSide * (g0From + (g0 - g0From) * t) * narrow * (sk0 + (sideK - sk0) * (float) (i + 1) * invS); }
                outL[i] = y * gl + sd;
                outR[i] = y * gr - sd;
            }
            prevAz = p.azDeg; prevBack = backTo; prevLpA = lpA; prevG0 = g0; prevCoef = true;
            return;
        }

        // ITD in samples: the ear on the far side is delayed
        const float itd = (a / c) * (std::abs (lat) + std::sin (std::abs (lat))) * (float) sr;
        const std::array<float, 2> dTarget { lat > 0.0f ? itd : 0.0f, lat > 0.0f ? 0.0f : itd }; // left ear delayed when the source is right

        // head shadow coefficients per ear
        const float w0 = c / a, beta = 2.0f * (float) sr / (2.0f * w0);
        Ear earTo[2];
        for (int e = 0; e < 2; ++e)
        {
            const float earDeg = e == 0 ? -100.0f : 100.0f;
            float phi = std::abs (juce::degreesToRadians (p.azDeg - earDeg));
            phi = std::fmod (phi, 2.0f * pi);
            if (phi > pi) phi = 2.0f * pi - phi;
            constexpr float amin = 0.1f;
            const float alpha = 1.0f + amin * 0.5f + (1.0f - amin * 0.5f) * std::cos (phi / juce::degreesToRadians (150.0f) * pi);
            const float b0 = 1.0f + beta * alpha, b1 = 1.0f - beta * alpha, a0 = 1.0f + beta, a1 = 1.0f - beta;
            earTo[e].b0 = b0 / a0;
            earTo[e].b1 = b1 / a0;
            earTo[e].a1 = a1 / a0;
        }
        const Ear earFrom[2] = { prevCoef ? prevEar[0] : earTo[0], prevCoef ? prevEar[1] : earTo[1] };

        // the ear away from the sound is also a little quieter across the whole range (the head model above only shades
        // the highs; our own addition, about 4 dB less at the far side, 2 dB more at the near one), so a sound to one side is heard there clearly
        const float farG = 1.0f - 0.35f * std::abs (std::sin (lat)), nearG = std::sqrt (2.0f - farG * farG); // (the two ears together keep their power)
        const std::array<float, 2> earG { lat > 0.0f ? farG : nearG, lat > 0.0f ? nearG : farG };
        const float inv = 1.0f / (float) juce::jmax (1, n);
        const std::array<float, 2> eg0 = prevAz < -999.0f ? earG : prevEarG;
        const float sk0 = prevSideK < 0.0f ? sideK : prevSideK;
        const std::array<float, 2> d0 = prevAz < -999.0f ? dTarget : prevDelay;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) * inv;
            const float lpI = lpFrom + (lpA - lpFrom) * t;
            lpState += lpI * (in[i] - lpState);
            const float yb = (backFrom.b0 + (backTo.b0 - backFrom.b0) * t) * lpState + backSh.z;
            backSh.z = (backFrom.b1 + (backTo.b1 - backFrom.b1) * t) * lpState - (backFrom.a1 + (backTo.a1 - backFrom.a1) * t) * yb;
            line[(size_t) wpos] = yb * (g0From + (g0 - g0From) * t);
            for (int e = 0; e < 2; ++e)
            {
                const float d = d0[(size_t) e] + (dTarget[(size_t) e] - d0[(size_t) e]) * (float) (i + 1) * inv;
                float rp = (float) wpos - d;
                if (rp < 0.0f) rp += (float) kLine;
                const int i0 = (int) rp, i1 = (i0 + 1) % kLine;
                const float f = rp - (float) i0;
                const float x = line[(size_t) i0] * (1.0f - f) + line[(size_t) i1] * f;
                auto& s = ear[(size_t) e];
                const float eb0 = earFrom[e].b0 + (earTo[e].b0 - earFrom[e].b0) * t, eb1 = earFrom[e].b1 + (earTo[e].b1 - earFrom[e].b1) * t, ea1 = earFrom[e].a1 + (earTo[e].a1 - earFrom[e].a1) * t;
                const float y = eb0 * x + s.z; // transposed direct form II, one pole / one zero
                s.z = eb1 * x - ea1 * y;
                const float gE = eg0[(size_t) e] + (earG[(size_t) e] - eg0[(size_t) e]) * (float) (i + 1) * inv;
                (e == 0 ? outL : outR)[i] = y * gE;
            }
            if (side != nullptr)
            {
                lpSide += lpA * (side[i] - lpSide);
                const float sk = sk0 + (sideK - sk0) * (float) (i + 1) * inv;
                outL[i] += lpSide * (g0From + (g0 - g0From) * t) * sk;
                outR[i] -= lpSide * (g0From + (g0 - g0From) * t) * sk;
            }
            wpos = (wpos + 1) % kLine;
        }
        prevDelay = dTarget;
        prevEarG = earG; prevSideK = sideK;
        prevAz = p.azDeg; prevBack = backTo; prevLpA = lpA; prevG0 = g0; prevEar[0] = earTo[0]; prevEar[1] = earTo[1]; prevCoef = true;
    }

private:
    static constexpr int kLine = 128;
    static constexpr float kMakeup = 1.4f; // level trim: noise straight ahead -2 dB, near ear at 90 deg +8 dB (measured), the output limiter catches peaks
    struct Ear { float b0 = 1, b1 = 0, a1 = 0, z = 0; };
    double sr = 44100.0;
    std::array<float, kLine> line {};
    int wpos = 0;
    std::array<Ear, 2> ear;
    Ear backSh;
    float lpState = 0.0f, lpSide = 0.0f;
    std::array<float, 2> prevDelay { 0.0f, 0.0f };
    float prevAz = -1000.0f;
    std::array<float, 2> prevEarG { 1.0f, 1.0f };
    float prevSideK = -1.0f;
    Ear prevEar[2], prevBack;
    float prevLpA = 0.0f, prevG0 = 1.0f;
    bool prevCoef = false;
};
