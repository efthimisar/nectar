#pragma once
#include <juce_graphics/juce_graphics.h>
#include <cmath>

// Shapes of movement for the joysticks and the Space dots. Shapes 0..15 are the Orb shapes of Flow (same maths,
// Source/OrbShapes.h of efthimisar/Flow, by the same author); 16.. are new. All return a point inside the unit circle
// (x right, y DOWN, as on screen), for u = 0..1 along one round. Our own designs, no published source.
namespace motionshapes
{
    constexpr int count = 28;
    constexpr int speedSteps = 5;
    inline double periodSeconds (int speed) // seconds per round, as the Orb in Flow
    {
        static const double p[speedSteps] { 16.0, 8.0, 4.0, 2.0, 1.0 };
        return p[juce::jlimit (0, speedSteps - 1, speed)];
    }
    // The round joystick pad and the square of its four corners: the elliptical grid mapping (Fong, "Analytical methods for
    // squaring the disc", 2015, arXiv:1509.06344 - not checked here). The rim of the disc goes to the edges of the square, so the
    // four points at 45 degrees reach the four corners. Both in -1..1, y up.
    inline juce::Point<float> discToSquare (float u, float v)
    {
        const float d = u * u + v * v;
        if (d > 1.0f) { const float s = 1.0f / std::sqrt (d); u *= s; v *= s; }
        const float r2 = 2.0f * std::sqrt (2.0f);
        const float a = 2.0f + u * u - v * v, b = 2.0f - u * u + v * v;
        const float x = 0.5f * std::sqrt (juce::jmax (0.0f, a + r2 * u)) - 0.5f * std::sqrt (juce::jmax (0.0f, a - r2 * u));
        const float y = 0.5f * std::sqrt (juce::jmax (0.0f, b + r2 * v)) - 0.5f * std::sqrt (juce::jmax (0.0f, b - r2 * v));
        return { juce::jlimit (-1.0f, 1.0f, x), juce::jlimit (-1.0f, 1.0f, y) };
    }
    inline juce::Point<float> squareToDisc (float x, float y)
    {
        return { x * std::sqrt (juce::jmax (0.0f, 1.0f - y * y * 0.5f)), y * std::sqrt (juce::jmax (0.0f, 1.0f - x * x * 0.5f)) };
    }
    inline float tri (float x) { x -= std::floor (x); return 4.0f * std::abs (x - 0.5f) - 1.0f; }
    inline juce::Point<float> poly (const juce::Point<float>* v, int n, float u)
    {
        const float s = u * (float) n; const int i = ((int) s) % n, j = (i + 1) % n; const float f = s - std::floor (s);
        return { v[i].x + (v[j].x - v[i].x) * f, v[i].y + (v[j].y - v[i].y) * f };
    }

    // ph: 4 random phases (wander), tg: 4 random targets (jump)
    inline juce::Point<float> point (int pat, float u, const float* ph, const juce::Point<float>* tg)
    {
        using juce::Point;
        const float pi = juce::MathConstants<float>::pi, T = 2.0f * pi * u, a = -0.5f * pi;
        switch (pat)
        {
            case 0:  return { 0.55f * std::cos (T + a), 0.55f * std::sin (T + a) };                                   // circle
            case 1:  return { 0.4f * std::sin (2.0f * T), 0.65f * std::sin (T) };                                     // figure eight, upright
            case 2:  return { 0.0f, 0.85f * std::sin (T) };                                                           // sweep up - down
            case 3:  { const float r = 0.15f + 0.7f * (0.5f - 0.5f * std::cos (T)); return { r * std::cos (2.0f * T + a), r * std::sin (2.0f * T + a) }; } // spiral
            case 4:  { const float r = 0.45f + 0.4f * std::sin (2.0f * T); return { 0.0f, -r }; }                     // pulse
            case 5:  return { 0.6f * std::sin (T + ph[0]) + 0.2f * std::sin (3.0f * T + ph[1]),
                              0.6f * std::sin (T + ph[2] + 1.3f) + 0.2f * std::sin (2.0f * T + ph[3]) };              // wander
            case 6:  { const int st = (int) (u * 4.0f) % 4; return st % 2 ? Point<float> (0.0f, 0.0f) : tg[st]; }     // jump and return
            case 7:  { const Point<float> v[3] { { 0.0f, -0.85f }, { 0.8f, 0.6f }, { -0.8f, 0.6f } }; return poly (v, 3, u); }               // triangle
            case 8:  { const Point<float> v[4] { { 0.0f, -0.85f }, { 0.85f, 0.0f }, { 0.0f, 0.85f }, { -0.85f, 0.0f } }; return poly (v, 4, u); } // diamond
            case 9:  return { 0.85f * std::sin (3.0f * T), 0.85f * std::sin (2.0f * T) };                             // Lissajous 3:2
            case 10: { const float r = 0.85f * std::cos (3.0f * T); return { r * std::cos (T), r * std::sin (T) }; }  // flower
            case 11: { const float s = 0.9f * std::sin (T); return { 0.7f * s, s }; }                                  // pendulum
            case 12: return { 0.75f * std::sin (T + 0.3f) + 0.15f * std::sin (5.0f * T), 0.6f * std::sin (2.0f * T + 1.0f) }; // drift
            case 13: return { 0.85f * tri (u * 2.0f), 0.85f * tri (u * 3.0f + 0.2f) };                                 // bounce
            case 14: { const float s1 = std::max (0.0f, std::sin (2.0f * T)), s2 = std::max (0.0f, std::sin (2.0f * T - 1.2f));
                       return { 0.0f, -(0.8f * s1 * s1 + 0.45f * s2 * s2) }; }                                         // heartbeat
            case 15: { const float f = u * 2.0f - std::floor (u * 2.0f); return { 0.0f, -0.9f * std::exp (-5.0f * f) }; } // drop
            // ---- new for Nektar
            case 16: return { 0.85f * std::sin (T), 0.0f };                                                           // sweep left - right
            case 17: { const Point<float> v[8] { { 0, 0 }, { 0, -0.85f }, { 0, 0 }, { 0.85f, 0 }, { 0, 0 }, { 0, 0.85f }, { 0, 0 }, { -0.85f, 0 } }; return poly (v, 8, u); } // cross
            case 18: { const float d = 0.62f; const Point<float> v[8] { { 0, 0 }, { -d, -d }, { 0, 0 }, { d, -d }, { 0, 0 }, { d, d }, { 0, 0 }, { -d, d } }; return poly (v, 8, u); } // X
            case 19: { const float d = 0.6f; const Point<float> v[4] { { -d, -d }, { d, -d }, { d, d }, { -d, d } }; return poly (v, 4, u); }  // square
            case 20: { Point<float> v[10];                                                                              // star
                       for (int i = 0; i < 10; ++i) { const float r = i % 2 ? 0.35f : 0.85f, an = a + (float) i * pi / 5.0f; v[i] = { r * std::cos (an), r * std::sin (an) }; }
                       return poly (v, 10, u); }
            case 21: return { 0.85f * std::sin (T), 0.4f * std::sin (2.0f * T) };                                     // infinity, lying
            case 22: { const float x = -0.85f + 1.7f * (0.5f - 0.5f * std::cos (T)); return { x, 0.5f * tri (u * 4.0f) }; } // zigzag
            case 23: { const float x = -0.85f + 1.7f * (0.5f - 0.5f * std::cos (T)); return { x, 0.45f * std::sin (6.0f * T) }; } // wave
            case 24: { const float an = pi * (0.5f - 0.5f * std::cos (T)); return { -0.8f * std::cos (an), -0.8f * std::sin (an) + 0.3f }; } // arc (rainbow)
            case 25: { const Point<float> v[4] { { -0.6f, -0.75f }, { 0.6f, -0.75f }, { -0.6f, 0.75f }, { 0.6f, 0.75f } }; return poly (v, 4, u); } // hourglass
            case 26: return { 0.85f * std::cos (T + a), 0.35f * std::sin (T + a) };                                   // wide ellipse
            case 27: { const float r = 0.85f * u; return { r * std::cos (3.0f * T + a), r * std::sin (3.0f * T + a) }; }     // spiral outwards, then back to the middle
            default: return { 0.0f, 0.0f };
        }
    }

    // the path of the shape, drawn as its symbol (no words)
    inline void drawSymbol (juce::Graphics& g, int pat, juce::Point<float> c, float r, juce::Colour col)
    {
        static const float ph[4] { 0.3f, 1.1f, 2.0f, 0.7f };
        static const juce::Point<float> tg[4] { { 0.7f, -0.5f }, { 0.0f, 0.0f }, { -0.6f, 0.6f }, { 0.0f, 0.0f } };
        juce::Path p;
        // shapes that only go up and down (pulse, heartbeat, drop) are drawn against time, so they can be told apart
        const bool overTime = pat == 4 || pat == 14 || pat == 15;
        for (int i = 0; i <= 120; ++i)
        {
            const auto q = point (pat, (float) i / 120.0f, ph, tg);
            const float x = overTime ? c.x + (-0.85f + 1.7f * (float) i / 120.0f) * r : c.x + q.x * r;
            const float y = overTime ? c.y + (q.y + 0.45f) * r : c.y + q.y * r;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.1f, r * 0.11f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
