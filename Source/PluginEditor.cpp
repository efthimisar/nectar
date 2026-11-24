#include "PluginEditor.h"
#include "BinaryData.h"

using juce::Colour;
using juce::ColourGradient;
using juce::Graphics;

namespace
{
const Colour kLight (0xffc1c494), kMuted (0xffa5a38a), kAcc (0xfffbe6a0), kDark (0xff2a2826);
const Colour kWada[] = { Colour (0xfffdc57e), Colour (0xffb5b1d8), Colour (0xff78cdd0),
                         Colour (0xfffbe6a0), Colour (0xffc1c494) };
constexpr float kDW = 1040.0f, kDH = 665.0f;   // design space of the editor (wide)
constexpr float kCx = 500.0f, kCy = 410.0f, kRingR = 255.0f, kScreenR = 134.0f; // the library bowl is designed at this size...
constexpr float kLibR = 138.0f;                   // ...and drawn smaller: library radius 138 (room below for the second voice)
constexpr float kRingScale = kLibR / 255.0f;
constexpr float kLibX = 520.0f, kLibY = 216.0f;
constexpr float kPadCx[2] = { 180.0f, 860.0f };
constexpr float kPadCy = 214.0f;
constexpr float kCharR = 84.0f; // the characters round each joystick
// ---- the main screen (design units): four panels
const juce::Rectangle<float> kPanelL (10.0f, 68.0f, 340.0f, 392.0f), kPanelC (358.0f, 68.0f, 324.0f, 392.0f),
                             kPanelR (690.0f, 68.0f, 340.0f, 392.0f), kPanelB (10.0f, 468.0f, 1020.0f, 190.0f);
// centre: the library categories, Random sounds and the relation of the pairs; the Space circle with its oval on the rim
// the shared voice (pair 2 as a second voice of pair 1): a small pad (across: the interval, up and down: near - far) and its Intensity bar
const juce::Rectangle<float> kVoicePad (410.0f, 366.0f, 220.0f, 60.0f), kVoiceBar (410.0f, 434.0f, 220.0f, 12.0f); // both centred on the panel (x 520)
inline juce::Rectangle<float> fxSlot (int i) { return { 355.0f + (float) i * 170.0f, 15.0f, 160.0f, 30.0f }; } // the two effects, top bar, centred on the window
const juce::Point<float> kPresetAt (384.0f, 93.0f), kSaveAt (656.0f, 93.0f);   // the two corners of the library card: Presets (left), Save to library (right)
//   // Presets: in the corner of the library card
const juce::Rectangle<float> kSpaceOval (472.0f, 271.0f, 96.0f, 30.0f);      // sits on the bottom rim of the Space circle
// bottom: the river of the four sounds; the three arcs; Keep, Make, Back; Harmony and Key; the message
const juce::Rectangle<float> kRiver (26.0f, 480.0f, 988.0f, 92.0f);
const juce::Point<float> kArcs (54.0f, 615.0f);          // the whole sound: Motion, Weight, Light
const juce::Rectangle<float> kMkOval (340.0f, 599.0f, 70.0f, 32.0f);   // which sounds Distill, Rec and Resample use: 1, 1+2, 1-3, 1-4 (each press, the next)
const juce::Point<float> kMkArcs (375.0f, 615.0f);       // the chosen sound: what Make may change (rhythm, colour, texture)
constexpr float kArcR[3] = { 16.0f, 22.0f, 28.0f };  // Motion (inside), Weight, Light (outside)
// one row, the same gap between all its parts: rings, Rec oval, rings, Keep, Make, Back, Harmony, Key
const juce::Rectangle<float> kMakeBtn (430.0f, 593.0f, 180.0f, 44.0f), kBackBtn (832.0f, 14.0f, 56.0f, 32.0f);
inline juce::Point<float> keepAt (int pair) { return { pair == 0 ? 330.0f : 1010.0f, 93.0f }; }   // Keep: top right of each pair's card
inline juce::Rectangle<float> harmChip (int i) { return { 734.0f + (float) i * 56.0f, 599.0f, 50.0f, 32.0f }; }
const juce::Rectangle<float> kKeyBtn (920.0f, 599.0f, 94.0f, 32.0f);
const juce::Rectangle<float> kMsgLine (320.0f, 640.0f, 440.0f, 16.0f);
// a pair panel: the movement and Gentle - Wild on top, the joystick with the 13 characters round it, the two sound cards
inline juce::Point<float> charAt (int pair, int i)
{
    const float a = juce::MathConstants<float>::pi * (-0.5f + 2.0f * (float) i / 13.0f);
    return { kPadCx[pair] + kCharR * std::cos (a), kPadCy + kCharR * std::sin (a) };
}
inline juce::Rectangle<float> charBtn (int pair) { return { (pair == 0 ? 180.0f : 860.0f) - 139.0f, 198.0f, 32.0f, 32.0f }; }
inline juce::Rectangle<float> kCharPanelR() { return { 10.0f, 468.0f, 1020.0f, 190.0f }; } // the place of the bottom panel
// the bench: four stages (Material, Timbre, Rhythm, Motion) of three controls each
inline juce::Rectangle<float> benchStage (int i) { return { 58.0f + (float) i * 241.0f, 502.0f, 235.0f, 150.0f }; }
inline juce::Point<float> benchKnobAt (int idx)
{   // Timbre has five (Light, Body, Air, Shift, Ring: 3, 4, 5, 12, 13), the other stages three
    if (idx >= 12) { const auto r = benchStage (1); return { r.getX() + 26.0f + (float) (idx - 12 + 3) * 45.0f, r.getY() + 112.0f }; }
    if (idx >= 3 && idx <= 5) { const auto r = benchStage (1); return { r.getX() + 26.0f + (float) (idx - 3) * 45.0f, r.getY() + 112.0f }; }
    const auto r = benchStage (idx / 3); return { r.getX() + 40.0f + (float) (idx % 3) * 78.0f, r.getY() + 112.0f };
}
inline juce::Rectangle<float> benchSwellChip() { const auto r = benchStage (3); return { r.getX() + r.getWidth() - 56.0f, r.getY() + 38.0f, 48.0f, 22.0f }; }
inline juce::Rectangle<float> benchWave() { const auto r = benchStage (0); return { r.getX() + 8.0f, r.getY() + 25.0f, r.getWidth() - 16.0f, 52.0f }; }
inline juce::Rectangle<float> benchStepAt (int j) { const auto r = benchStage (2); return { r.getX() + 8.0f + (float) j * 14.4f, r.getY() + 32.0f, 12.0f, 24.0f }; }
struct BenchCtl { const char* name; const char* id; int kind; bool grains; }; // kind: 0 knob, 1 switch, 2 five steps, 3 chip (free / steps)
static const BenchCtl kBench[14] = {
    { "Speed", "grscan", 0, true }, { "Grains", "grsize", 0, true }, { "Reverse", "grrev", 1, true },
    { "Light", "blight", 0, false }, { "Body", "bbody", 0, false }, { "Air", "bair", 0, false },
    { "Cut", "grgate", 0, true }, { "Density", "grdens", 0, true }, { "Pattern", "grmode", 3, true },
    { "Move", "spmove", 2, false }, { "How fast", "spspeed", 0, false }, { "Scatter", "grspray", 0, true },
    { "Shift", "fshift", 0, false }, { "Ring", "ring", 0, false } };
inline juce::Point<float> benchChipAt (int i) { return { 150.0f + (float) i * 38.0f, 487.0f }; }
static const int kBenchFlavours[7] = { 0, 3, 10, 8, 7, 2, 12 }; // Cloud, Air, Shimmer, Dust, Glitch, Metal, Arp
static const char* const kBenchStage[4] = { "Material", "Timbre", "Rhythm", "Motion" };
inline juce::Point<float> shapeAt (int pair) { return { pair == 0 ? 34.0f : 714.0f, 92.0f }; }
inline juce::Rectangle<float> speedDots (int pair) { const auto c = shapeAt (pair); return { c.x + 20.0f, c.y - 6.0f, 40.0f, 12.0f }; }
inline juce::Rectangle<float> cardRect (int slot) { return { (slot < 2 ? 20.0f : 700.0f) + (float) (slot % 2) * 166.0f, 322.0f, 154.0f, 128.0f }; }
inline juce::Rectangle<float> cardWave (int slot) { const auto r = cardRect (slot); return { r.getX() + 8.0f, r.getY() + 24.0f, r.getWidth() - 16.0f, 44.0f }; }
inline juce::Point<float> lockAt (int slot) { const auto r = cardRect (slot); return { r.getRight() - 15.0f, r.getBottom() - 15.0f }; }
inline juce::Point<float> soloAt (int slot) { const auto r = cardRect (slot); return { r.getRight() - 38.0f, r.getBottom() - 15.0f }; }
const juce::Rectangle<float> kRecOval (100.0f, 15.0f, 160.0f, 30.0f);  // Rec, where it goes, Resample: in the top bar, between the mark and the effects
const juce::Point<float> kRecAt (kRecOval.getX() + 17.0f, kRecOval.getCentreY()), kResampleAt (kRecOval.getRight() - 17.0f, kRecOval.getCentreY());
inline juce::Point<float> blendAt (int pair) { return { cardRect (pair * 2).getRight() + 6.0f, cardRect (pair * 2).getY() + 46.0f }; }
inline juce::Rectangle<float> keyLabel (int slot) { const auto r = cardRect (slot); return { r.getX() + 10.0f, r.getY() + 92.0f, r.getWidth() - 20.0f, 14.0f }; }
constexpr float kSpaceX = 520.0f, kSpaceY = 216.0f, kSpaceR = 69.0f; // the placement pad (top view of the listener)

juce::Font font (const juce::Typeface::Ptr& tf, float h)
{
    return juce::Font (juce::FontOptions (tf).withHeight (h));
}

void shadowCircle (Graphics& g, float cx, float cy, float r, float alpha, int radius, juce::Point<int> off)
{
    juce::Path p;
    p.addEllipse (cx - r, cy - r, 2 * r, 2 * r);
    juce::DropShadow (juce::Colours::black.withAlpha (alpha), radius, off).drawForPath (g, p);
}

void shadowRect (Graphics& g, juce::Rectangle<float> r, float corner, float alpha, int radius, juce::Point<int> off)
{
    juce::Path p;
    p.addRoundedRectangle (r, corner);
    juce::DropShadow (juce::Colours::black.withAlpha (alpha), radius, off).drawForPath (g, p);
}

// Heights of 40 bars that draw the sound (0..1), or nothing for an empty slot.
juce::Array<float> peaksOf (const Sample* smp, int bars = 40)
{
    juce::Array<float> peaks;
    if (smp == nullptr)
        return peaks;
    const int n = smp->data.getNumSamples();
    float mx = 1.0e-6f;
    for (int b = 0; b < bars; ++b)
    {
        const int a = (int) ((juce::int64) n * b / bars), e = juce::jmax (a + 1, (int) ((juce::int64) n * (b + 1) / bars));
        float m = 0.0f;
        for (int c = 0; c < smp->data.getNumChannels(); ++c)
            m = juce::jmax (m, smp->data.findMinMax (c, a, e - a).getLength() * 0.5f);
        peaks.add (m);
        mx = juce::jmax (mx, m);
    }
    for (auto& v : peaks) v /= mx;
    return peaks;
}

} // namespace

namespace nektar
{
void drawKnob (Graphics& g, const juce::Typeface::Ptr& tf, float cx, float cy, float r, float p,
               const juce::String& label, bool dim, float fs)
{
    const float a = dim ? 0.35f : 1.0f;
    g.setColour (Colour (0xff1e1d1b));
    g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path c;
        c.addEllipse (cx - r, cy - r, 2 * r, 2 * r);
        g.reduceClipRegion (c);
        g.setColour (kAcc.withMultipliedAlpha (a));
        g.fillRect (cx - r, cy + r - 2 * r * p, 2 * r, 2 * r * p);
    }
    g.setColour (kAcc.withMultipliedAlpha (a));
    g.drawEllipse (cx - r, cy - r, 2 * r, 2 * r, 1.5f);
    g.setColour (kMuted.withMultipliedAlpha (dim ? 0.5f : 1.0f));
    g.setFont (font (tf, fs));
    g.drawText (label, juce::Rectangle<float> (cx - 45, cy + r + 3, 90, fs + 4), juce::Justification::centred);
}
} // namespace nektar

// ------------------------------------------------------------------ KnobComponent
KnobComponent::KnobComponent (juce::Typeface::Ptr tf, float r, juce::String label, bool dim, float fixedValue,
                              juce::RangedAudioParameter* param, bool isToggle)
    : typeface (std::move (tf)), radius (r), text (std::move (label)), dimmed (dim), fixed (fixedValue),
      parameter (param), toggle (isToggle)
{
    if (parameter != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
}

void KnobComponent::paint (Graphics& g)
{
    const float s = (float) getWidth() / kWidth;
    g.addTransform (juce::AffineTransform::scale (s));
    const float p = parameter != nullptr ? parameter->getValue() : fixed;
    const bool multi = toggle && parameter != nullptr && parameter->getNumSteps() > 2;
    nektar::drawKnob (g, typeface, kWidth / 2.0f, radius, radius, p, multi ? parameter->getCurrentValueAsText() : text, dimmed,
                      juce::jlimit (9.0f, 12.0f, radius * 0.95f));
}

void KnobComponent::mouseDown (const juce::MouseEvent& e)
{
    if (parameter == nullptr)
        return;
    if (e.mods.isPopupMenu())
        return;
    parameter->beginChangeGesture();
    if (toggle)
    {
        const int n = parameter->getNumSteps();
        if (n > 2) // a button that cycles through its choices (Joystick Play: Off, 1shot, Repeat, Free)
            parameter->setValueNotifyingHost ((float) ((juce::roundToInt (parameter->getValue() * (float) (n - 1)) + 1) % n) / (float) (n - 1));
        else
            parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    dragStart = parameter->getValue();
}

void KnobComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (parameter == nullptr || toggle)
        return;
    if (e.mods.isPopupMenu())
        return;
    const float s = (float) getWidth() / kWidth;
    const float delta = -(float) e.getDistanceFromDragStartY() / (150.0f * s);
    parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStart + delta));
}

void KnobComponent::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (parameter != nullptr)
        parameter->endChangeGesture();
}

void KnobComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (parameter == nullptr || toggle)
        return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->getDefaultValue());
    parameter->endChangeGesture();
}

// ------------------------------------------------------------------ Editor
namespace
{
const char* kKeys = "awsedftgyhujkolp;"; // computer keyboard = one chromatic octave and a bit, from C
}

// ------------------------------------------------------------------ PairPad
void PairPad::paint (Graphics& g)
{   // a round pad: the disc is spread onto the square of the four corners (MotionShapes.h)
    const float w = (float) getWidth(), c = w * 0.5f, r = c - 1.5f, rr = r - 12.0f;
    const bool on = proc.vectorOn (pair);
    const auto tint = kWada[pair == 0 ? 1 : 2];
    g.setColour (Colour (0xff1b1a19));
    g.fillEllipse (c - r, c - r, 2 * r, 2 * r);
    g.setColour (on ? tint.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (c - r, c - r, 2 * r, 2 * r, on ? 2.0f : 1.2f);
    if (! on)
    {
        g.setColour (kMuted);
        g.setFont (juce::Font (juce::FontOptions (w * 0.09f)));
        g.drawFittedText ("Distill", getLocalBounds(), juce::Justification::centred, 1);
        trail.clear();
        return;
    }
    auto toPix = [&] (float x01, float y01)
    {
        const auto d = motionshapes::squareToDisc (2.0f * x01 - 1.0f, 2.0f * y01 - 1.0f);
        return juce::Point<float> (c + d.x * rr, c - d.y * rr);
    };
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (c - rr * 0.5f, c - rr * 0.5f, rr, rr, 1.0f);
    for (int i = 0; i < 4; ++i)
    {   // the four corners (four versions of the pair) on the rim: the more a corner is heard now, the bigger and brighter
        const auto q = toPix ((i & 1) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f);
        const float vx = proc.vectorX (pair), vy = proc.vectorY (pair);
        const float wgt = ((i & 1) ? vx : 1.0f - vx) * ((i & 2) ? vy : 1.0f - vy);
        const float rad = 3.0f + 6.0f * wgt;
        g.setColour (tint.withAlpha (0.25f + 0.75f * wgt));
        g.fillEllipse (q.x - rad, q.y - rad, 2 * rad, 2 * rad);
    }
    const auto now = toPix (proc.vectorX (pair), proc.vectorY (pair));
    trail.push_back (now);
    if (trail.size() > 48) trail.erase (trail.begin());
    for (size_t i = 0; i + 1 < trail.size(); ++i)
    {   // the trail of the stick
        const float a = (float) i / (float) trail.size();
        g.setColour (tint.withAlpha (0.5f * a));
        g.fillEllipse (trail[i].x - 1.0f - 2.5f * a, trail[i].y - 1.0f - 2.5f * a, 2.0f + 5.0f * a, 2.0f + 5.0f * a);
    }
    g.setColour (tint.withAlpha (0.25f));
    g.fillEllipse (now.x - 13.0f, now.y - 13.0f, 26.0f, 26.0f);
    g.setColour (tint);
    g.fillEllipse (now.x - 8.0f, now.y - 8.0f, 16.0f, 16.0f);
}

void PairPad::setFromMouse (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float w = (float) getWidth(), c = w * 0.5f, rr = c - 1.5f - 12.0f;
    const auto sq = motionshapes::discToSquare ((e.position.x - c) / rr, (c - e.position.y) / rr);
    if (auto* p = proc.apvts.getParameter (pair == 0 ? "vec1x" : "vec2x")) p->setValueNotifyingHost (0.5f + 0.5f * sq.x);
    if (auto* p = proc.apvts.getParameter (pair == 0 ? "vec1y" : "vec2y")) p->setValueNotifyingHost (0.5f + 0.5f * sq.y);
    repaint();
}

void PairPad::mouseDown (const juce::MouseEvent& e)
{
    if (! proc.vectorOn (pair)) return;
    {   // only inside the round pad
        const float c = (float) getWidth() * 0.5f;
        if (e.position.getDistanceFrom ({ c, c }) > c) return;
    }
    dragging = true;
    for (const char* id : { pair == 0 ? "vec1x" : "vec2x", pair == 0 ? "vec1y" : "vec2y" })
        if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture();
    setFromMouse (e);
}

void PairPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    for (const char* id : { pair == 0 ? "vec1x" : "vec2x", pair == 0 ? "vec1y" : "vec2y" })
        if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture();
}

// ------------------------------------------------------------------ LibraryMap
void LibraryMap::refresh()
{
    if (proc.libIndex.version() == seenVersion && proc.libCategory == seenCategory) return;
    seenVersion = proc.libIndex.version();
    seenCategory = proc.libCategory;
    dots = proc.libIndex.layout (proc.libCategory);
    hover = -1;
}
juce::Point<float> LibraryMap::toPix (float x, float y) const
{
    const float R = (float) getWidth() * 0.5f;
    return { R + x * R * 0.94f, R + y * R * 0.94f };
}
juce::Point<float> LibraryMap::toUnit (juce::Point<float> p) const
{
    const float R = (float) getWidth() * 0.5f;
    return { (p.x - R) / (R * 0.94f), (p.y - R) / (R * 0.94f) };
}
bool LibraryMap::hitTest (int x, int y)
{   // only the ring around the Space pad belongs to the map
    const float d = toUnit ({ (float) x, (float) y }).getDistanceFromOrigin();
    return d >= 0.52f && d <= 1.06f;
}
int LibraryMap::dotAt (juce::Point<float> p) const
{
    int best = -1; float bd = 7.0f;
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    for (int i = 0; i < (int) dots.size(); ++i)
    {
        const float dx = 0.012f * (float) std::sin (t * (0.31 + 0.07 * (i % 7)) + i), dy = 0.012f * (float) std::cos (t * (0.27 + 0.05 * (i % 5)) + 1.7 * i);
        const float d = toPix (dots[(size_t) i].x + dx, dots[(size_t) i].y + dy).getDistanceFrom (p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
void LibraryMap::paint (juce::Graphics& g)
{
    refresh();
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto cur = proc.libCursor;
    const float R = (float) getWidth() * 0.5f;
    if (dots.empty())
    {
        g.setColour (kMuted.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (proc.libIndex.busy() ? "Reading the library..." : "Put sounds in Music / Nektar Library",
                    juce::Rectangle<float> (0, R * 0.12f, (float) getWidth(), 16), juce::Justification::centred);
        return;
    }
    // the group the cursor holds: its nearest quarter (at least four), as Random sounds will pick
    std::vector<float> dist (dots.size());
    for (size_t i = 0; i < dots.size(); ++i) dist[i] = juce::Point<float> (dots[i].x, dots[i].y).getDistanceFrom (cur);
    auto sorted = dist;
    std::sort (sorted.begin(), sorted.end());
    const size_t keep = juce::jmin (dots.size(), (size_t) juce::jmax (4, (int) dots.size() / 4));
    const float lim = sorted[keep - 1];
    const float sz = juce::jlimit (2.2f, 4.6f, 4.6f - 0.012f * (float) dots.size());
    for (int i = 0; i < (int) dots.size(); ++i)
    {   // molecules: each drifts a little around its place
        const float dx = 0.012f * (float) std::sin (t * (0.31 + 0.07 * (i % 7)) + i), dy = 0.012f * (float) std::cos (t * (0.27 + 0.05 * (i % 5)) + 1.7 * i);
        const auto p = toPix (dots[(size_t) i].x + dx, dots[(size_t) i].y + dy);
        const bool in = dist[(size_t) i] <= lim;
        const auto col = kWada[(size_t) (i % 5)];
        g.setColour (col.withAlpha (in ? 0.95f : 0.35f));
        g.fillEllipse (p.x - sz, p.y - sz, 2 * sz, 2 * sz);
        if (i == hover) { g.setColour (kAcc); g.drawEllipse (p.x - sz - 3, p.y - sz - 3, 2 * sz + 6, 2 * sz + 6, 1.2f); }
    }
    // the cursor and the reach of its group
    const auto c = toPix (cur.x, cur.y);
    g.setColour (kAcc.withAlpha (0.16f));
    g.fillEllipse (c.x - 13.0f, c.y - 13.0f, 26, 26);
    g.setColour (kAcc);
    g.drawEllipse (c.x - 8.5f, c.y - 8.5f, 17, 17, 1.4f);
    g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7, 7);
    juce::ignoreUnused (R);
}
void LibraryMap::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }
void LibraryMap::mouseDrag (const juce::MouseEvent& e)
{
    auto u = toUnit (e.position);
    const float d = u.getDistanceFromOrigin();
    if (d > 1.0f) u /= d;
    if (d < 0.55f && d > 1.0e-4f) u *= 0.55f / d;
    proc.libCursor = u;
    repaint();
}
void LibraryMap::mouseMove (const juce::MouseEvent& e)
{
    const int h = dotAt (e.position);
    if (h != hover)
    {
        hover = h;
        if (onHover) onHover (h >= 0 ? dots[(size_t) h].file.getFileNameWithoutExtension() : juce::String());
        repaint();
    }
}
void LibraryMap::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = dotAt (e.position);
    if (h >= 0 && onLoad) onLoad (dots[(size_t) h].file);
}

// ------------------------------------------------------------------ SpacePad
namespace
{
juce::Point<float> spaceDotPos (float azDeg, float dist, float R)
{
    const float a = juce::degreesToRadians (azDeg), r = (0.12f + 0.88f * dist) * R * 0.92f;
    return { R + std::sin (a) * r, R - std::cos (a) * r }; // up = in front of the listener
}
}

void SpacePad::paint (Graphics& g)
{
    const float R = (float) getWidth() * 0.5f;
    g.setColour (Colour (0xff1e1d1b));
    g.fillEllipse (1.0f, 1.0f, 2 * R - 2.0f, 2 * R - 2.0f);
    g.setColour (Colour (0xff7ec8e3).withAlpha (0.8f));
    g.drawEllipse (1.0f, 1.0f, 2 * R - 2.0f, 2 * R - 2.0f, 1.6f);
    // the listener: a head seen from above, nose up (front); click it for a new placement of the sounds.
    // The headphones on it: lit = listening on headphones, dark = on speakers (click them to switch)
    g.setColour (overHead ? kLight.brighter (0.3f) : kLight);
    g.fillEllipse (R - 9, R - 9, 18, 18);
    juce::Path nose;
    nose.addTriangle (R - 4, R - 8, R + 4, R - 8, R, R - 15);
    g.fillPath (nose);
    {
        const bool phones = proc.apvts.getRawParameterValue ("spspk")->load() < 0.5f;
        const auto pc = phones ? kAcc : kMuted.withAlpha (overPhones ? 0.9f : 0.45f);
        juce::Path band;
        band.addCentredArc (R, R, 15.0f, 15.0f, 0.0f, -1.9f, 1.9f, true);
        g.setColour (pc);
        g.strokePath (band, juce::PathStrokeType (overPhones ? 3.2f : 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle (R - 17.5f, R - 2.0f, 6.0f, 11.0f, 2.0f);
        g.fillRoundedRectangle (R + 11.5f, R - 2.0f, 6.0f, 11.0f, 2.0f);
    }
    for (int s = 0; s < 4; ++s)
    {
        const auto az = proc.apvts.getRawParameterValue (s == 0 ? "spaz" : "spaz" + juce::String (s + 1))->load();
        const auto di = proc.apvts.getRawParameterValue (s == 0 ? "spdist" : "spdist" + juce::String (s + 1))->load();
        const auto home = spaceDotPos (az, di, R);
        const bool moving = juce::roundToInt (proc.apvts.getRawParameterValue (s == 0 ? "spmove" : "spmove" + juce::String (s + 1))->load()) > 0;
        const auto p = moving && s != dragging ? spaceDotPos (proc.spaceAzNow (s), proc.spaceDistNow (s), R) : home;
        if (moving)
        {   // where it was put: a small ring; the dot itself travels
            g.setColour (kWada[s % 5].withAlpha (0.5f));
            g.drawEllipse (home.x - 5.0f, home.y - 5.0f, 10.0f, 10.0f, 1.2f);
        }
        if (s == chosen)
        {
            g.setColour (kAcc);
            g.drawEllipse (p.x - 13.0f, p.y - 13.0f, 26.0f, 26.0f, 1.6f);
        }
        g.setColour (kWada[s % 5].withAlpha (s == dragging ? 1.0f : 0.92f));
        g.fillEllipse (p.x - 9.5f, p.y - 9.5f, 19, 19);
        g.setColour (kDark);
        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        g.drawText (juce::String (s + 1), juce::Rectangle<float> (p.x - 9.5f, p.y - 9.5f, 19, 19), juce::Justification::centred);
    }
}

void SpacePad::setDot (int s, juce::Point<float> pos)
{
    const float R = (float) getWidth() * 0.5f;
    float dx = (pos.x - R) / (R * 0.92f), dy = (R - pos.y) / (R * 0.92f);
    const float len = std::sqrt (dx * dx + dy * dy);
    const float azDeg = juce::radiansToDegrees (std::atan2 (dx, dy));
    const float dist = juce::jlimit (0.0f, 1.0f, (juce::jmin (len, 1.0f) - 0.12f) / 0.88f);
    const juce::String sfx = s == 0 ? juce::String() : juce::String (s + 1);
    if (auto* pa = proc.apvts.getParameter ("spaz" + sfx)) pa->setValueNotifyingHost (pa->convertTo0to1 (azDeg));
    if (auto* pd = proc.apvts.getParameter ("spdist" + sfx)) pd->setValueNotifyingHost (pd->convertTo0to1 (dist));
}

void SpacePad::mouseDown (const juce::MouseEvent& e)
{
    {
        const float R = (float) getWidth() * 0.5f;
        const float d = e.position.getDistanceFrom ({ R, R });
        bool onDot = false; // a sound dot right next to the head wins over the head and the headphones
        for (int s = 0; s < 4 && ! onDot; ++s)
        {
            const auto sx = s == 0 ? juce::String() : juce::String (s + 1);
            const bool mv = juce::roundToInt (proc.apvts.getRawParameterValue ("spmove" + sx)->load()) > 0;
            const auto dp = mv ? spaceDotPos (proc.spaceAzNow (s), proc.spaceDistNow (s), R)
                               : spaceDotPos (proc.apvts.getRawParameterValue ("spaz" + sx)->load(), proc.apvts.getRawParameterValue ("spdist" + sx)->load(), R);
            onDot = e.position.getDistanceFrom (dp) <= 9.5f;
        }
        if (! onDot && d >= 12.0f && d <= 21.0f && e.position.y <= R + 10.0f)
        {   // the headphones: on = headphones, off = speakers
            if (auto* pa = proc.apvts.getParameter ("spspk")) pa->setValueNotifyingHost (pa->getValue() > 0.5f ? 0.0f : 1.0f);
            if (onSay) onSay (proc.apvts.getRawParameterValue ("spspk")->load() < 0.5f ? "Headphones: the sounds are placed for listening on headphones."
                                                                                     : "Speakers: the sounds are placed for listening on speakers.");
            repaint();
            return;
        }
        if (! onDot && d < 12.0f)
        {   // the head: a new placement and movement for every sound
            proc.newSpace();
            if (onSay) onSay ("A new place and movement for each sound.");
            repaint();
            return;
        }
    }
    const float R = (float) getWidth() * 0.5f;
    dragging = -1;
    float best = 0.3f * R;
    for (int s = 0; s < 4; ++s)
    {
        const auto az = proc.apvts.getRawParameterValue (s == 0 ? "spaz" : "spaz" + juce::String (s + 1))->load();
        const auto di = proc.apvts.getRawParameterValue (s == 0 ? "spdist" : "spdist" + juce::String (s + 1))->load();
        const bool moving = juce::roundToInt (proc.apvts.getRawParameterValue (s == 0 ? "spmove" : "spmove" + juce::String (s + 1))->load()) > 0;
        const float d = juce::jmin (e.position.getDistanceFrom (spaceDotPos (az, di, R)),
                                    moving ? e.position.getDistanceFrom (spaceDotPos (proc.spaceAzNow (s), proc.spaceDistNow (s), R)) : 1.0e9f);
        if (d < best) { best = d; dragging = s; }
    }
    if (dragging >= 0)
    {
        chosen = dragging;
        if (onPick) onPick (dragging);
        const juce::String sfx = dragging == 0 ? juce::String() : juce::String (dragging + 1);
        if (auto* pa = proc.apvts.getParameter ("spaz" + sfx)) pa->beginChangeGesture();
        if (auto* pd = proc.apvts.getParameter ("spdist" + sfx)) pd->beginChangeGesture();
        setDot (dragging, e.position);
    }
}
void SpacePad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0) setDot (dragging, e.position);
    repaint();
}
void SpacePad::mouseMove (const juce::MouseEvent& e)
{
    const float R = (float) getWidth() * 0.5f, d = e.position.getDistanceFrom ({ R, R });
    const bool h = d < 12.0f, ph = d >= 12.0f && d <= 21.0f && e.position.y <= R + 10.0f;
    if (h != overHead || ph != overPhones) { overHead = h; overPhones = ph; repaint(); }
}

void SpacePad::mouseExit (const juce::MouseEvent&) { overHead = overPhones = false; repaint(); }

void SpacePad::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        const juce::String sfx = dragging == 0 ? juce::String() : juce::String (dragging + 1);
        if (auto* pa = proc.apvts.getParameter ("spaz" + sfx)) pa->endChangeGesture();
        if (auto* pd = proc.apvts.getParameter ("spdist" + sfx)) pd->endChangeGesture();
    }
    dragging = -1;
    repaint();
}

NektarEditor::NektarEditor (NektarProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    proc.fxRack.beforeChange = [this] (int i) { fxWindows[(size_t) juce::jlimit (0, 1, i)] = nullptr; };
    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::CarlitoRegular_ttf, BinaryData::CarlitoRegular_ttfSize);
    bold    = juce::Typeface::createSystemTypefaceFor (BinaryData::CarlitoBold_ttf, BinaryData::CarlitoBold_ttfSize);

    {
        libMap = std::make_unique<LibraryMap> (proc);
        libMap->onLoad = [this] (const juce::File& f) { loadFile (f, selectedSlot); say ("Loaded into circle " + juce::String (selectedSlot + 1) + "."); };
        libMap->onHover = [this] (const juce::String& n) { if (n.isNotEmpty()) say (n); };
        addAndMakeVisible (*libMap);
        proc.rescanLibrary();
        spacePad = std::make_unique<SpacePad> (proc);
        spacePad->onPick = [this] (int s)
        {
            if (s == spaceSlot) return;
            spaceSlot = s;
            rebuildPanel();
            repaint();
        };
        addAndMakeVisible (*spacePad);
        spacePad->onSay = [this] (const juce::String& m) { say (m); repaint(); };
        spaceOval = std::make_unique<Overlay>();
        spaceOval->draw = [this] (juce::Graphics& g) { drawSpaceOval (g, spaceOval->getLocalBounds().toFloat()); };
        spaceOval->down = [this] (const juce::MouseEvent& e) { spaceOvalClick (e); };
        addAndMakeVisible (*spaceOval);
    }

    addKnob (250, 615, 13, "Out", false, 0.0f, "master"); // Master out, -24..+6 dB (default 0 dB)
    for (int i = 0; i < 2; ++i)
    {
        pairPads[i] = std::make_unique<PairPad> (proc, i);
        addAndMakeVisible (*pairPads[i]);
    }
    fixedKnobs = knobs.size();
    rebuildPanel();

    constrainer.setFixedAspectRatio ((double) (kDW / kDH));
    constrainer.setMinimumSize (620, (int) (620 * kDH / kDW));
    constrainer.setMaximumSize (2600, (int) (2600 * kDH / kDW));
    setConstrainer (&constrainer);
    setResizable (true, true);
    setWantsKeyboardFocus (true);
    proc.addChangeListener (this);
    startTimerHz (20);
    setSize ((int) kDW, (int) kDH);
    grabKeyboardFocus();
}

NektarEditor::~NektarEditor()
{
    stopTimer();
    proc.fxRack.beforeChange = nullptr;
    for (auto& w : fxWindows) w = nullptr;
    proc.removeChangeListener (this);
    releaseAllKeys();
}

void NektarEditor::addKnob (float cx, float cy, float r, const juce::String& label, bool dim, float fixed,
                            const char* paramId, bool toggle)
{
    juce::RangedAudioParameter* param = paramId != nullptr ? proc.apvts.getParameter (paramId) : nullptr;
    auto* k = knobs.add (new KnobComponent (regular, r, label, dim, fixed, param, toggle));
    knobPos.add ({ cx, cy });
    addAndMakeVisible (k);
}

void NektarEditor::resized()
{
    const float s = (float) getWidth() / kDW;
    for (int i = 0; i < knobs.size(); ++i)
    {
        auto* k = knobs[i];
        const auto c = knobPos[i];
        const float r = (k->height() - 20.0f) / 2.0f;
        k->setBounds (juce::Rectangle<float> ((c.x - KnobComponent::kWidth / 2.0f) * s, (c.y - r) * s,
                                              KnobComponent::kWidth * s, k->height() * s).toNearestInt());
    }
    if (libMap != nullptr)
        libMap->setBounds (juce::Rectangle<float> ((kLibX - kLibR) * s, (kLibY - kLibR) * s, 2.0f * kLibR * s, 2.0f * kLibR * s).toNearestInt());
    if (spaceOval != nullptr)
    {
        spaceOval->setBounds (juce::Rectangle<float> (kSpaceOval.getX() * s, kSpaceOval.getY() * s, kSpaceOval.getWidth() * s, kSpaceOval.getHeight() * s).toNearestInt());
    }
    if (spacePad != nullptr)
        spacePad->setBounds (juce::Rectangle<float> ((kSpaceX - kSpaceR) * s, (kSpaceY - kSpaceR) * s,
                                                     2 * kSpaceR * s, 2 * kSpaceR * s).toNearestInt());
    for (int i = 0; i < 2; ++i)
        if (pairPads[i] != nullptr)
            pairPads[i]->setBounds (juce::Rectangle<float> ((kPadCx[i] - PairPad::kHalf) * s, (kPadCy - PairPad::kHalf) * s,
                                                            2 * PairPad::kHalf * s, 2 * PairPad::kHalf * s).toNearestInt());
}

void NektarEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    cache = juce::Image();
    repaint();
}

void NektarEditor::paint (Graphics& g)
{
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int pw = juce::roundToInt ((float) getWidth() * ps);
    const int ph = juce::roundToInt ((float) getHeight() * ps);

    if (cache.getWidth() != pw || cache.getHeight() != ph)
    {
        cache = juce::Image (juce::Image::ARGB, pw, ph, true);
        Graphics ig (cache);
        ig.addTransform (juce::AffineTransform::scale ((float) pw / kDW, (float) ph / kDH));
        renderDesign (ig);
        addGrain (cache);
    }
    g.drawImage (cache, getLocalBounds().toFloat());
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / kDW));
    drawFace (g);
    if (charPanel >= 0) drawCharPanel (g);
}

void NektarEditor::addGrain (juce::Image& img)
{
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    juce::Random rnd (7);
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
        {
            const int n = rnd.nextInt (11) - 5;
            const auto c = bd.getPixelColour (x, y);
            bd.setPixelColour (x, y, Colour ((juce::uint8) juce::jlimit (0, 255, (int) c.getRed() + n),
                                             (juce::uint8) juce::jlimit (0, 255, (int) c.getGreen() + n),
                                             (juce::uint8) juce::jlimit (0, 255, (int) c.getBlue() + n)));
        }
}

// ---- loading sounds
void NektarEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position / ((float) getWidth() / kDW);
    if (topBarClick (p)) return;
    if (charPanelClick (p)) return;
    faceClick (e, p);
}

// the bar at the top
bool NektarEditor::topBarClick (juce::Point<float> p)
{
    if (fxSlot (0).contains (p)) fxMenu (0);
    else if (fxSlot (1).contains (p)) fxMenu (1);
    else return false;
    return true;
}

void NektarEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{   // the wheel on a joystick's movement button: its speed
    const auto p = e.position / ((float) getWidth() / kDW);
    for (int pair = 0; pair < 2; ++pair)
        if (p.getDistanceFrom (shapeAt (pair)) <= 16.0f)
        {
            const char* id = pair == 0 ? "vec1speed" : "vec2speed";
            const int cur = juce::roundToInt (proc.apvts.getRawParameterValue (id)->load());
            if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) juce::jlimit (0, 4, cur + (w.deltaY > 0 ? 1 : -1))));
            repaint();
            return;
        }
}

void NektarEditor::pressKeep (int pair)
{
    say (proc.savePreset ("Keep " + juce::String (pair + 1) + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S")));
}

int NektarEditor::slotAt (juce::Point<float> p) const
{
    for (int i = 0; i < 4; ++i)
        if (cardRect (i).contains (p))
            return i;
    return -1;
}

void NektarEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.position / ((float) getWidth() / kDW));
    if (slot >= 0)
    {
        selectedSlot = slot;
        recAll = false; proc.makeMask = 1 << slot;
        repaint();
        chooseFile (slot);
    }
}

void NektarEditor::selectSlot (int slot)
{
    if (slot == selectedSlot)
        return;
    selectedSlot = slot;
    recAll = false; proc.makeMask = 1 << slot;
    repaint();
}

void NektarEditor::say (const juce::String& text)
{   // the message line is drawn live (drawFace): the cached background stays as it is
    message = text;
    repaint();
}

void NektarEditor::toggleRecording()
{
    if (! proc.canRecord())
    {
        say ("Recording works in the standalone app.");
        return;
    }
    if (proc.isRecording())
    {
        say (proc.stopRecording());
        return;
    }
    if (message.isNotEmpty())
        say ({});
    proc.startRecording (recAll ? NektarProcessor::kToLibrary : selectedSlot);
}

void NektarEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Save preset...");
    menu.addSeparator();
    menu.addItem (7, "Auto level (equal loudness of the sounds)", true, proc.apvts.getRawParameterValue ("autolevel")->load() > 0.5f);
    menu.addItem (3, "Store snapshot A");
    menu.addItem (4, "Store snapshot B");
    menu.addItem (5, "Recall A", proc.hasSnapshot (0));
    menu.addItem (6, "Recall B", proc.hasSnapshot (1));
    menu.addSeparator();
    const auto presets = NektarProcessor::listPresets();
    for (int i = 0; i < presets.size(); ++i)
        menu.addItem (100 + i, presets[i].getFileNameWithoutExtension());
    if (presets.isEmpty())
        menu.addItem (2, "(no presets yet)", false);
    menu.addSeparator();
    {
        juce::PopupMenu factory, rename, remove;
        const auto fn = NektarProcessor::factoryNames();
        for (int i = 0; i < fn.size(); ++i)
            factory.addItem (200 + i, fn[i]);
        for (int i = 0; i < presets.size(); ++i)
        {
            rename.addItem (300 + i, presets[i].getFileNameWithoutExtension());
            remove.addItem (400 + i, presets[i].getFileNameWithoutExtension());
        }
        menu.addSubMenu ("Factory presets", factory);
        menu.addSubMenu ("Rename preset", rename, ! presets.isEmpty());
        menu.addSubMenu ("Delete preset (to Trash)", remove, ! presets.isEmpty());
    }
    juce::Component::SafePointer<NektarEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (
                            juce::Rectangle<int> ((int) (316 * getWidth() / kDW), (int) (45 * getWidth() / kDW), 64, 4))),
                        [safe, presets] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result >= 200 && result < 300)
        {
            safe->say (safe->proc.loadFactory (result - 200));
            return;
        }
        if (result >= 300 && result < 400 && result - 300 < presets.size())
        {
            const auto file = presets[result - 300];
            auto* w = new juce::AlertWindow ("Rename preset", "New name:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", file.getFileNameWithoutExtension(), "");
            w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, file] (int r)
            {
                if (r == 1 && safe != nullptr)
                    safe->say (safe->proc.renamePreset (file, w->getTextEditorContents ("name")));
                delete w;
            }));
            return;
        }
        if (result >= 400 && result < 500 && result - 400 < presets.size())
        {
            const auto file = presets[result - 400];
            auto* w = new juce::AlertWindow ("Delete preset", "Move \"" + file.getFileNameWithoutExtension() + "\" to the Trash?", juce::MessageBoxIconType::NoIcon);
            w->addButton ("Move to Trash", 1);
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, file] (int r)
            {
                if (r == 1 && safe != nullptr)
                    safe->say (safe->proc.deletePreset (file));
                delete w;
            }));
            return;
        }
        if (result >= 100 && result - 100 < presets.size())
        {
            safe->say (safe->proc.loadPreset (presets[result - 100]));
            return;
        }
        if (result == 7)
        {
            auto* a = safe->proc.apvts.getParameter ("autolevel");
            const bool on = a->getValue() < 0.5f;
            a->setValueNotifyingHost (on ? 1.0f : 0.0f);
            safe->say (on ? "Auto level on." : "Auto level off.");
            return;
        }
        if (result == 3 || result == 4)
        {
            safe->proc.storeSnapshot (result - 3);
            safe->say (juce::String ("Stored snapshot ") + (result == 3 ? "A" : "B") + ".");
            return;
        }
        if (result == 5 || result == 6)
        {
            safe->proc.recallSnapshot (result - 5);
            safe->say (juce::String ("Recalled snapshot ") + (result == 5 ? "A" : "B") + ".");
            return;
        }
        if (result == 1)
        {
            auto* w = new juce::AlertWindow ("Save preset", "Name of the preset:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "", "");
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
            {
                if (r == 1 && safe != nullptr)
                    safe->say (safe->proc.savePreset (w->getTextEditorContents ("name")));
                delete w;
            }), false);
        }
    });
}

void NektarEditor::toggleResample()
{
    if (proc.isResampling())
    {
        say (proc.stopRecording());
        return;
    }
    if (proc.isRecording())
        return; // a microphone recording is running
    if (recAll)
    {
        say ("Resampling all of Nectar into a new sound in the library. Play, then click again.");
        proc.startResample (NektarProcessor::kToLibrary);
        return;
    }
    if (proc.getSample (selectedSlot) == nullptr)
    {
        say ("Sound " + juce::String (selectedSlot + 1) + " is empty: nothing to resample.");
        return;
    }
    say ("Resampling sound " + juce::String (selectedSlot + 1) + " with its changes. Play, then click again.");
    proc.startResample (selectedSlot);
}

void NektarEditor::timerCallback()
{
    if (const auto m = proc.takeMakeMessage(); m.isNotEmpty()) { say (m); repaint(); }
    if (proc.baking() && proc.judgeSteps.load() > 0 && proc.judgeStep.load() <= proc.judgeSteps.load() && message.startsWith ("Listening"))
    {
        const juce::String m = "Listening to version " + juce::String (juce::jmax (1, proc.judgeStep.load())) + " of " + juce::String (proc.judgeSteps.load()) + "...";
        if (m != message) { say (m); repaint(); }
    }
    if (proc.fxRack.scanning())
    {   // which plug-in is being looked at (if one makes Nektar crash, it is skipped the next time)
        const auto f = juce::File (proc.fxRack.scanName).getFileNameWithoutExtension();
        const juce::String m = "Looking for effects " + juce::String (juce::roundToInt (100.0f * proc.fxRack.scanProgress)) + "%" + (f.isNotEmpty() ? ": " + f : juce::String());
        if (m != message) { say (m); repaint(); }
    }
    if (proc.recordingFinishedByLimit())
        say (proc.stopRecording());
    if (spacePad != nullptr)
        spacePad->repaint();
    if (libMap != nullptr)
        libMap->repaint();
    {   // the main screen's movement: grains read, levels, the river, the joystick trails
        auto& row = river[(size_t) riverAt];
        for (int slot = 0; slot < 4; ++slot)
        {
            levelShown[(size_t) slot] += (proc.slotLevel[(size_t) slot].load() - levelShown[(size_t) slot]) * 0.35f;
            row[(size_t) slot] = levelShown[(size_t) slot];
            riverPan[(size_t) riverAt][(size_t) slot] = -std::sin (juce::degreesToRadians (proc.spaceAzNow (slot)));
            const int c = proc.tapCount[(size_t) slot].load();
            const int fresh = juce::jmin (8, c - sparkSeen[(size_t) slot]);
            for (int k = 0; k < fresh; ++k) // y holds the slot and the height within its card
                sparks.push_back ({ proc.tapPos[(size_t) slot][(size_t) ((c - 1 - k) & 15)].load(), (float) slot + 0.15f + 0.7f * juce::Random::getSystemRandom().nextFloat(), 1.0f });
            sparkSeen[(size_t) slot] = c;
        }
        {
            const float pk = proc.outPeak.exchange (0.0f);
            outShown = juce::jmax (outShown * 0.85f, pk);
            clipHold = pk > 0.9f ? 30 : juce::jmax (0, clipHold - 1);
        }
        const float bt = proc.beatNow.load();
        riverBeat[(size_t) riverAt] = proc.beatOn.load() && bt < lastBeat;
        lastBeat = bt;
        riverAt = (riverAt + 1) % (int) river.size();
        for (auto& sp : sparks) sp.life -= 0.07f;
        sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& sp) { return sp.life <= 0.0f; }), sparks.end());
        if (sparks.size() > 400) sparks.erase (sparks.begin(), sparks.begin() + (long) (sparks.size() - 400));
        for (auto& pp : pairPads) if (pp != nullptr) pp->repaint();
        if (spaceOval != nullptr) spaceOval->repaint();
        repaint();
    }
}

void NektarEditor::rebuildPanel()
{
    while (knobs.size() > fixedKnobs)
    {
        knobs.removeLast();
        knobPos.removeLast();
    }
    spaceSpeedId = slotId ("spspeed", spaceSlot); // the speed of the chosen sound's movement in the space, on the oval
    addKnob (kSpaceOval.getRight() - 17.0f, kSpaceOval.getCentreY(), 10.0f, "", false, 0.0f, spaceSpeedId.toRawUTF8());
    resized();
}

// Live parts of the panel (drawn in design units on top of the cached picture).
namespace
{
// symbols instead of words: the first five Characters and the Space movements
void characterSymbol (juce::Graphics& g, int i, juce::Point<float> c, float r, juce::Colour col)
{
    g.setColour (col);
    const float w = juce::jmax (1.2f, r * 0.13f);
    juce::Path p;
    switch (i)
    {
        case 0: // Cloud: a few soft dots
        {
            const float d[6][3] { { -0.55f, 0.15f, 0.28f }, { -0.15f, -0.2f, 0.36f }, { 0.3f, -0.05f, 0.32f }, { 0.65f, 0.25f, 0.22f }, { 0.05f, 0.35f, 0.25f }, { -0.35f, 0.45f, 0.16f } };
            for (auto& q : d) g.fillEllipse (c.x + q[0] * r - q[2] * r, c.y + q[1] * r - q[2] * r, 2 * q[2] * r, 2 * q[2] * r);
            return;
        }
        case 1: // Rhythm: bars of different heights
        {
            const float h[5] { 0.9f, 0.4f, 0.65f, 0.3f, 0.8f };
            for (int k = 0; k < 5; ++k) g.fillRoundedRectangle (c.x + (-0.8f + 0.4f * (float) k) * r - 0.11f * r, c.y + r * 0.6f - h[k] * 1.2f * r, 0.22f * r, h[k] * 1.2f * r, 0.08f * r);
            return;
        }
        case 2: // Metal: a struck triangle
            p.addTriangle (c.x, c.y - 0.8f * r, c.x + 0.75f * r, c.y + 0.6f * r, c.x - 0.75f * r, c.y + 0.6f * r);
            g.strokePath (p, juce::PathStrokeType (w));
            g.drawLine (c.x + 0.45f * r, c.y - 0.9f * r, c.x + 0.95f * r, c.y - 0.4f * r, w);
            return;
        case 3: // Air: three waves
            for (int k = 0; k < 3; ++k)
            {
                juce::Path wv;
                const float y0 = c.y + (-0.5f + 0.5f * (float) k) * r;
                for (int t = 0; t <= 24; ++t)
                {
                    const float x = c.x - 0.85f * r + 1.7f * r * (float) t / 24.0f, y = y0 + 0.14f * r * std::sin ((float) t / 24.0f * 6.283f * 1.5f + (float) k);
                    if (t == 0) wv.startNewSubPath (x, y); else wv.lineTo (x, y);
                }
                g.strokePath (wv, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            return;
        default: // Reverse: an arrow going back
            p.startNewSubPath (c.x + 0.8f * r, c.y);
            p.lineTo (c.x - 0.7f * r, c.y);
            g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            {
                juce::Path h; h.addTriangle (c.x - 0.85f * r, c.y, c.x - 0.35f * r, c.y - 0.4f * r, c.x - 0.35f * r, c.y + 0.4f * r);
                g.fillPath (h);
            }
            g.drawLine (c.x + 0.8f * r, c.y - 0.5f * r, c.x + 0.8f * r, c.y + 0.5f * r, w);
            return;
    }
}
// Space movement i: 0 still, 1 round the listener, 2 left - right, 3 front - back (those three go round / through the listener's
// space: drawn with the head in the middle), 4.. the shapes around the dot's own place
void spaceSymbol (juce::Graphics& g, int i, juce::Point<float> c, float r, juce::Colour col)
{
    const float w = juce::jmax (1.1f, r * 0.11f);
    g.setColour (col);
    if (i == 0) { g.fillEllipse (c.x - 0.25f * r, c.y - 0.25f * r, 0.5f * r, 0.5f * r); return; }
    if (i >= 4) { motionshapes::drawSymbol (g, i - 4, c, r, col); return; }
    g.fillEllipse (c.x - 0.18f * r, c.y - 0.18f * r, 0.36f * r, 0.36f * r); // the listener
    if (i == 1) { g.drawEllipse (c.x - 0.8f * r, c.y - 0.8f * r, 1.6f * r, 1.6f * r, w); return; }
    const bool h = i == 2;
    const float L = 0.85f * r;
    auto arrow = [&] (float sx, float sy)
    {
        juce::Path a; const float tx = c.x + sx * L, ty = c.y + sy * L;
        if (h) a.addTriangle (tx, ty, tx - sx * 0.35f * r, ty - 0.25f * r, tx - sx * 0.35f * r, ty + 0.25f * r);
        else   a.addTriangle (tx, ty, tx - 0.25f * r, ty - sy * 0.35f * r, tx + 0.25f * r, ty - sy * 0.35f * r);
        g.fillPath (a);
    };
    if (h) { g.drawLine (c.x - L, c.y, c.x - 0.3f * r, c.y, w); g.drawLine (c.x + 0.3f * r, c.y, c.x + L, c.y, w); arrow (-1, 0); arrow (1, 0); }
    else   { g.drawLine (c.x, c.y - L, c.x, c.y - 0.3f * r, w); g.drawLine (c.x, c.y + 0.3f * r, c.x, c.y + L, w); arrow (0, -1); arrow (0, 1); }
}
}

void NektarEditor::chooseFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sound for slot " + juce::String (slot + 1), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<NektarEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, slot] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (safe != nullptr && f.existsAsFile())
                                  safe->loadFile (f, slot);
                          });
}

void NektarEditor::loadFile (const juce::File& f, int slot)
{
    say (proc.loadSample (f, slot));
    if (auto* pk = proc.apvts.getParameter (slotId ("skey", slot))) pk->setValueNotifyingHost (0.0f); // a new sound: its key is found again
    if (auto* pt = proc.apvts.getParameter (slotId ("stempo", slot))) pt->setValueNotifyingHost (0.0f); // ...and its tempo
}

bool NektarEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac"))
            return true;
    return false;
}

void NektarEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    int slot = slotAt (juce::Point<float> ((float) x, (float) y) / ((float) getWidth() / kDW));
    if (slot < 0)
        slot = selectedSlot;
    selectSlot (slot);
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac"))
        {
            loadFile (juce::File (f), slot);
            return;
        }
}

// ---- computer keyboard as a piano (A W S E D F T G Y H U J K O L P ;), Z/X = octave down/up
void NektarEditor::releaseAllKeys()
{
    for (int n : heldNotes)
        proc.keyboardState.noteOff (1, n, 0.0f);
    heldNotes.clear();
}

bool NektarEditor::keyPressed (const juce::KeyPress& k)
{
    const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown())
        return false;
    if (c == 'z' || c == 'x')
    {
        releaseAllKeys();
        octaveShift = juce::jlimit (-3, 3, octaveShift + (c == 'x' ? 1 : -1));
        return true;
    }
    return juce::String (kKeys).containsChar (c);
}

bool NektarEditor::keyStateChanged (bool)
{
    bool changed = false;
    for (int i = 0; kKeys[i] != 0; ++i)
    {
        const int note = juce::jlimit (0, 127, 60 + 12 * octaveShift + i);
        const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) kKeys[i]);
        const bool held = heldNotes.count (note) > 0;
        if (down && ! held)
        {
            heldNotes.insert (note);
            proc.keyboardState.noteOn (1, note, 0.8f);
            changed = true;
        }
        else if (! down && held)
        {
            heldNotes.erase (note);
            proc.keyboardState.noteOff (1, note, 0.0f);
            changed = true;
        }
    }
    return changed;
}

void NektarEditor::renderDesign (Graphics& g)
{
    const auto& reg = regular;
    const auto& bld = bold;

    // ---- background: taupe, light from the top-left
    {
        ColourGradient bg (Colour (0xff9a8a73), 280, 140, Colour (0xff4e4237), 1100, 1100, true);
        bg.addColour (0.45, Colour (0xff7b6b5b));
        g.setGradientFill (bg);
        g.fillAll();
        g.setGradientFill (ColourGradient (Colour (0xfffff4dc).withAlpha (0.14f), 260, 150,
                                           Colour (0xfffff4dc).withAlpha (0.0f), 260, 480, true));
        g.fillEllipse (-160, -150, 840, 600);
    }

    // ---- top bar
    shadowRect (g, { 0, 0, kDW, 60 }, 0, 0.5f, 14, { 0, 6 });
    g.setGradientFill (ColourGradient (Colour (0xff353331), 0, 0, Colour (0xff272523), 0, 60, false));
    g.fillRect (0.0f, 0.0f, kDW, 60.0f);

    g.setColour (kAcc);
    g.setFont (font (bld, 26));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xcf\x80/-")), juce::Rectangle<float> (22, 12, 70, 36), juce::Justification::centredLeft);

    g.setColour (kLight);
    g.setFont (font (reg, 32));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xce\xbd\xce\xad\xce\xba\xcf\x84\xce\xb1\xcf\x81")),
                juce::Rectangle<float> (kDW - 150.0f, 8, 130, 44), juce::Justification::centredRight);

    // ---- ring (library map): recessed bowl; the ring and the check screen are drawn smaller (kRingScale)
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (kRingScale, kRingScale, kCx, kCy).translated (kLibX - kCx, kLibY - kCy));
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillEllipse (kCx - kRingR + 4, kCy - kRingR + 5, 2 * kRingR, 2 * kRingR);
    g.setGradientFill (ColourGradient (Colour (0xff141312), kCx - 120, kCy - 150, Colour (0xff2a2826), kCx + 200, kCy + 230, true));
    g.fillEllipse (kCx - kRingR, kCy - kRingR, 2 * kRingR, 2 * kRingR);

    // ---- centre screen
    g.setGradientFill (ColourGradient (kAcc.withAlpha (0.10f), kCx, kCy, kAcc.withAlpha (0.0f), kCx + 150, kCy, true));
    g.fillEllipse (kCx - 150, kCy - 150, 300, 300);
    shadowCircle (g, kCx, kCy, kScreenR, 0.6f, 26, { 9, 14 });
    g.setGradientFill (ColourGradient (Colour (0xff3a3836), kCx - 70, kCy - 80, Colour (0xff222120), kCx + 80, kCy + 100, false));
    g.fillEllipse (kCx - kScreenR, kCy - kScreenR, 2 * kScreenR, 2 * kScreenR);
    g.setGradientFill (ColourGradient (Colour (0xff181716), kCx - 40, kCy - 60, Colour (0xff201f1d), kCx + 60, kCy + 70, false));
    g.fillEllipse (kCx - 122, kCy - 122, 244, 244);

    g.restoreState();

    // ---- four plates (flat, no shadow under them): the two pairs, the centre, the whole sound (the face itself is drawn live, drawFace)
    for (auto plate : { kPanelL, kPanelC, kPanelR, kPanelB })
    {
        g.setGradientFill (ColourGradient (Colour (0xff6b6454).withAlpha (0.55f), 0, plate.getY(),
                                           Colour (0xff4d4739).withAlpha (0.55f), 0, plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 22.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawLine (plate.getX() + 22.0f, plate.getY() + 1.0f, plate.getRight() - 22.0f, plate.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (plate.reduced (0.5f), 22.0f, 1.2f);
    }
}

// ================================================================== the main screen (one panel)
namespace
{
// symbols, no words: the 13 characters, the 5 relations and the 6 Space movements
void charSymbol (juce::Graphics& g, int i, juce::Point<float> c, float r, juce::Colour col)
{
    if (i < 5) { characterSymbol (g, i, c, r, col); return; }
    g.setColour (col);
    const float w = juce::jmax (1.2f, r * 0.13f);
    juce::Path p;
    switch (i)
    {
        case 5: // Drone: a low steady line over the ground
            g.fillRoundedRectangle (c.x - 0.85f * r, c.y - 0.1f * r, 1.7f * r, 0.32f * r, 0.12f * r);
            for (int k = 0; k < 4; ++k) g.drawLine (c.x + (-0.6f + 0.4f * (float) k) * r, c.y + 0.45f * r, c.x + (-0.6f + 0.4f * (float) k) * r - 0.2f * r, c.y + 0.7f * r, w * 0.8f);
            return;
        case 6: // Swell: grows and fades
            p.startNewSubPath (c.x - 0.85f * r, c.y);
            p.lineTo (c.x, c.y - 0.55f * r); p.lineTo (c.x + 0.85f * r, c.y); p.lineTo (c.x, c.y + 0.55f * r); p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (w));
            return;
        case 7: // Glitch: broken pieces
            g.fillRect (c.x - 0.8f * r, c.y - 0.5f * r, 0.45f * r, 0.35f * r);
            g.fillRect (c.x - 0.2f * r, c.y - 0.1f * r, 0.5f * r, 0.3f * r);
            g.fillRect (c.x + 0.4f * r, c.y - 0.6f * r, 0.35f * r, 0.25f * r);
            g.fillRect (c.x - 0.5f * r, c.y + 0.35f * r, 0.6f * r, 0.22f * r);
            return;
        case 8: // Dust: specks
        {
            const float d[7][2] { { -0.7f, -0.4f }, { -0.2f, 0.5f }, { 0.1f, -0.6f }, { 0.6f, 0.1f }, { -0.5f, 0.2f }, { 0.3f, 0.6f }, { 0.75f, -0.5f } };
            for (auto& q : d) g.fillEllipse (c.x + q[0] * r - 0.09f * r, c.y + q[1] * r - 0.09f * r, 0.18f * r, 0.18f * r);
            return;
        }
        case 9: // Bells: a bell
            p.startNewSubPath (c.x - 0.7f * r, c.y + 0.45f * r);
            p.quadraticTo (c.x - 0.55f * r, c.y - 0.75f * r, c.x, c.y - 0.75f * r);
            p.quadraticTo (c.x + 0.55f * r, c.y - 0.75f * r, c.x + 0.7f * r, c.y + 0.45f * r);
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (w));
            g.fillEllipse (c.x - 0.15f * r, c.y + 0.5f * r, 0.3f * r, 0.3f * r);
            return;
        case 10: // Shimmer: a sparkle
            for (int k = 0; k < 4; ++k)
            {
                const float a = juce::MathConstants<float>::halfPi * (float) k;
                p.startNewSubPath (c.x, c.y);
                p.lineTo (c.x + std::cos (a) * 0.85f * r, c.y + std::sin (a) * 0.85f * r);
            }
            g.strokePath (p, juce::PathStrokeType (w));
            g.fillEllipse (c.x - 0.2f * r, c.y - 0.2f * r, 0.4f * r, 0.4f * r);
            return;
        case 11: // Melt: two sounds running into each other
            g.drawEllipse (c.x - 0.75f * r, c.y - 0.45f * r, 0.9f * r, 0.9f * r, w);
            g.drawEllipse (c.x - 0.15f * r, c.y - 0.45f * r, 0.9f * r, 0.9f * r, w);
            g.fillEllipse (c.x - 0.12f * r, c.y + 0.45f * r, 0.24f * r, 0.35f * r);
            return;
        default: // Arp: steps going up
            for (int k = 0; k < 5; ++k) g.fillEllipse (c.x + (-0.75f + 0.37f * (float) k) * r - 0.12f * r, c.y + (0.55f - 0.27f * (float) k) * r - 0.12f * r, 0.24f * r, 0.24f * r);
            return;
    }
}
void relationSymbol (juce::Graphics& g, int i, juce::Point<float> c, float r, juce::Colour col)
{
    g.setColour (col);
    const float w = juce::jmax (1.1f, r * 0.12f);
    switch (i)
    {
        case 0: g.fillEllipse (c.x - 0.2f * r, c.y - 0.2f * r, 0.4f * r, 0.4f * r); return;
        case 1: // Contrast: half and half
        {
            g.drawEllipse (c.x - 0.7f * r, c.y - 0.7f * r, 1.4f * r, 1.4f * r, w);
            juce::Path p; p.addPieSegment (c.x - 0.7f * r, c.y - 0.7f * r, 1.4f * r, 1.4f * r, 0.0f, juce::MathConstants<float>::pi, 0.0f);
            g.fillPath (p);
            return;
        }
        case 2: // Shadow: one follows the other
            g.fillEllipse (c.x - 0.75f * r, c.y - 0.45f * r, 0.8f * r, 0.8f * r);
            g.drawEllipse (c.x - 0.05f * r, c.y - 0.35f * r, 0.8f * r, 0.8f * r, w);
            return;
        case 3: // Call and response: two turns
        {
            juce::Path a, b;
            a.addTriangle (c.x + 0.8f * r, c.y - 0.35f * r, c.x + 0.35f * r, c.y - 0.6f * r, c.x + 0.35f * r, c.y - 0.1f * r);
            b.addTriangle (c.x - 0.8f * r, c.y + 0.35f * r, c.x - 0.35f * r, c.y + 0.1f * r, c.x - 0.35f * r, c.y + 0.6f * r);
            g.drawLine (c.x - 0.7f * r, c.y - 0.35f * r, c.x + 0.4f * r, c.y - 0.35f * r, w);
            g.drawLine (c.x - 0.4f * r, c.y + 0.35f * r, c.x + 0.7f * r, c.y + 0.35f * r, w);
            g.fillPath (a); g.fillPath (b);
            return;
        }
        default: // Depth: near and far
            g.fillEllipse (c.x - 0.25f * r, c.y - 0.25f * r, 0.5f * r, 0.5f * r);
            g.drawEllipse (c.x - 0.75f * r, c.y - 0.75f * r, 1.5f * r, 1.5f * r, w);
            return;
    }
}
void moveSymbol (juce::Graphics& g, int i, juce::Point<float> c, float r, juce::Colour col)
{   // 0 still, 1 left - right, 2 front - back, 3 arc in front, 4 arc behind, 5 circle; the listener is the small dot
    const float w = juce::jmax (1.1f, r * 0.11f);
    g.setColour (col);
    if (i == 0) { g.fillEllipse (c.x - 0.28f * r, c.y - 0.28f * r, 0.56f * r, 0.56f * r); return; }
    if (i == 1) { spaceSymbol (g, 2, c, r, col); return; }
    if (i == 2) { spaceSymbol (g, 3, c, r, col); return; }
    if (i == 5) { spaceSymbol (g, 1, c, r, col); return; }
    g.fillEllipse (c.x - 0.15f * r, c.y - 0.15f * r, 0.3f * r, 0.3f * r);
    juce::Path p;
    if (i == 3) p.addCentredArc (c.x, c.y, 0.8f * r, 0.8f * r, 0.0f, -1.4f, 1.4f, true);
    else        p.addCentredArc (c.x, c.y, 0.8f * r, 0.8f * r, 0.0f, juce::MathConstants<float>::pi - 1.4f, juce::MathConstants<float>::pi + 1.4f, true);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}


namespace
{
void harmonySymbol (juce::Graphics& g, int i, juce::Point<float> c, juce::Colour col)
{   // the four sounds as dots: 0 scattered (as is), 1 on one line (one key), 2 one on another (chord)
    g.setColour (col);
    if (i == 0) { const float d[4][2] { { -10, -5 }, { -3, 5 }, { 4, -3 }, { 11, 6 } }; for (auto& q : d) g.fillEllipse (c.x + q[0] - 2.6f, c.y + q[1] - 2.6f, 5.2f, 5.2f); return; }
    if (i == 1) { g.drawLine (c.x - 15, c.y, c.x + 15, c.y, 0.8f); for (int k = 0; k < 4; ++k) g.fillEllipse (c.x - 11.0f + 7.3f * (float) k - 2.6f, c.y - 2.6f, 5.2f, 5.2f); return; }
    for (int k = 0; k < 3; ++k) g.fillEllipse (c.x - 2.8f, c.y - 9.0f + 6.5f * (float) k - 2.6f, 5.6f, 5.2f);
}
void keepSymbol (juce::Graphics& g, juce::Point<float> c, juce::Colour col)
{   // a drop falling into a circle
    g.setColour (col);
    g.drawEllipse (c.x - 7.0f, c.y - 2.0f, 14.0f, 14.0f, 1.6f);
    juce::Path d;
    d.startNewSubPath (c.x, c.y - 13.0f);
    d.quadraticTo (c.x + 4.0f, c.y - 7.0f, c.x, c.y - 4.0f);
    d.quadraticTo (c.x - 4.0f, c.y - 7.0f, c.x, c.y - 13.0f);
    g.fillPath (d);
}
void backSymbol (juce::Graphics& g, juce::Point<float> c, juce::Colour col)
{   // a dot going back the way it came, its trail behind it
    g.setColour (col);
    g.fillEllipse (c.x - 11.0f, c.y - 4.0f, 8.0f, 8.0f);
    const float tr[4][2] { { 2.0f, 2.6f }, { 8.0f, 2.0f }, { 13.0f, 1.5f }, { 17.0f, 1.1f } };
    for (int k = 0; k < 4; ++k) { g.setColour (col.withAlpha (0.75f - 0.17f * (float) k)); g.fillEllipse (c.x + tr[k][0] - tr[k][1], c.y - tr[k][1], 2 * tr[k][1], 2 * tr[k][1]); }
}
void arcIcon (juce::Graphics& g, int i, juce::Point<float> c, juce::Colour col)
{   // 0 motion (a dot with a trail), 1 weight (a weight), 2 light (a sun)
    g.setColour (col);
    if (i == 0) { g.fillEllipse (c.x + 1.0f, c.y - 2.5f, 5.0f, 5.0f); g.setColour (col.withAlpha (0.5f)); g.fillEllipse (c.x - 4.0f, c.y - 1.5f, 3.0f, 3.0f); return; }
    if (i == 1) { juce::Path p; p.addTriangle (c.x - 4.0f, c.y + 4.0f, c.x + 4.0f, c.y + 4.0f, c.x, c.y - 3.0f); g.fillPath (p); g.drawEllipse (c.x - 1.6f, c.y - 6.0f, 3.2f, 3.2f, 1.0f); return; }
    g.fillEllipse (c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
    for (int k = 0; k < 6; ++k) { const float a = juce::MathConstants<float>::twoPi * (float) k / 6.0f; g.drawLine (c.x + std::cos (a) * 4.0f, c.y + std::sin (a) * 4.0f, c.x + std::cos (a) * 6.0f, c.y + std::sin (a) * 6.0f, 1.0f); }
}
const char* const kCharNames[13] = { "Cloud", "Rhythm", "Metal", "Air", "Reverse", "Drone", "Swell", "Glitch", "Dust", "Bells", "Shimmer", "Melt", "Arp" };
// families round the joystick, each with its colour: soft, rhythmic, metallic, changing
const int kCharOrder[13] = { 0, 3, 5, 6, 10, 1, 7, 12, 8, 2, 9, 4, 11 };
inline juce::Colour familyColour (int ch)
{
    switch (ch) { case 0: case 3: case 5: case 6: case 10: return kWada[2]; case 1: case 7: case 12: case 8: return kWada[3]; case 2: case 9: return kWada[0]; default: return kWada[1]; }
}
const char* const kArcNames[3] = { "Motion: slower - faster", "Weight: thinner - heavier", "Light: duller - brighter" };
const char* const kArcIds[3] = { "mspeed", "mweight", "mlight" };
}

void NektarEditor::drawFace (Graphics& g)
{
    for (int i = 0; i < 2; ++i)
    {   // the two effects at the end (VST3): name when one is there, lit when it is on
        const auto r = fxSlot (i);
        const auto nm = proc.fxRack.name (i);
        const bool on = nm.isNotEmpty() && proc.fxRack.isOn (i);
        g.setColour (Colour (0xff1e1d1b));
        g.fillRoundedRectangle (r, 15.0f);
        g.setColour (on ? kAcc : kMuted.withAlpha (nm.isEmpty() ? 0.5f : 0.8f));
        g.drawRoundedRectangle (r.reduced (0.75f), 15.0f, 1.5f);
        g.fillEllipse (r.getX() + 11.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setColour (nm.isEmpty() ? kMuted : (on ? kLight : kMuted));
        g.setFont (font (regular, 13));
        g.drawFittedText (nm.isEmpty() ? juce::String ("+ FX ") + juce::String (i + 1) : nm, r.withTrimmedLeft (26.0f).withTrimmedRight (10.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }
    auto button = [&] (juce::Rectangle<float> r, bool usable, bool strong)
    {
        g.setColour (strong && usable ? kAcc : Colour (0xff2f2d2b));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    };
    auto disc = [&] (juce::Point<float> c, float r, bool on, juce::Colour tint)
    {
        g.setColour (on ? tint : Colour (0xff2f2d2b));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    };
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // ================= the pair panels
    for (int pr = 0; pr < 2; ++pr)
    {
        const auto panel = pr == 0 ? kPanelL : kPanelR;
        g.setColour (kDark.withAlpha (0.55f));
        g.setFont (font (regular, 12));
        g.drawText (pr == 0 ? "1 + 2" : "3 + 4", juce::Rectangle<float> (panel.getCentreX() - 30.0f, 84.0f, 60.0f, 16.0f), juce::Justification::centred);
        // the movement (click = next shape, wheel = speed) and its five speed dots
        const int shape = juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "vec1shape" : "vec2shape")->load());
        const auto tint = familyColour (juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "character1" : "character2")->load()));
        const auto sc = shapeAt (pr);
        disc (sc, 14.0f, shape > 0, tint);
        static const int pick[4] = { 1, 3, 9, 10 };
        if (shape > 0) motionshapes::drawSymbol (g, pick[juce::jlimit (0, 3, shape - 1)], sc, 9.0f, kDark);
        else { g.setColour (kLight); g.fillEllipse (sc.x - 3.0f, sc.y - 3.0f, 6.0f, 6.0f); }
        if (shape > 0)
        {
            const int speed = juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "vec1speed" : "vec2speed")->load());
            const auto r = speedDots (pr);
            for (int k = 0; k < 5; ++k)
            {
                g.setColour (k <= speed ? kAcc : kDark.withAlpha (0.35f));
                g.fillEllipse (r.getX() + 4.0f + 8.0f * (float) k - 2.4f, r.getCentreY() - 2.4f, 4.8f, 4.8f);
            }
        }
        {   // the bowl behind the joystick and its characters, like the library and the space
            const float R = kCharR + 15.0f, cx = kPadCx[pr], cy = kPadCy;
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.fillEllipse (cx - R + 3.0f, cy - R + 4.0f, 2 * R, 2 * R);
            g.setGradientFill (ColourGradient (Colour (0xff35332f), cx - 60.0f, cy - 75.0f, Colour (0xff45423d), cx + 100.0f, cy + 115.0f, true));
            g.fillEllipse (cx - R, cy - R, 2 * R, 2 * R);
        }
        {   // Conversation, across: which sound leads the other's rhythm (left: the first sound, right: the second)
            g.setFont (11.0f);
            for (int c = 0; c < 2; ++c)
            {
                g.setColour (kWada[(pr * 2 + c) % 5].withAlpha (0.85f));
                const float x = kPadCx[pr] + (c == 0 ? -96.0f : 70.0f);
                g.drawText (c == 0 ? juce::String (pr * 2 + 1) + " leads" : juce::String (pr * 2 + 2) + " leads",
                            juce::Rectangle<float> (x, kPadCy - 7.0f, 26.0f, 14.0f).expanded (14.0f, 0.0f), juce::Justification::centred);
                const float sx = proc.stickX (pr);
                if (std::abs (sx - 0.5f) > 0.1f && (sx < 0.5f) == (c == 0) && proc.convDev (pr) < 0.06f)
                {   // this sound leads, but it plays evenly: there is no beat for the other to follow
                    g.setColour (kLight.withAlpha (0.55f)); g.setFont (9.5f);
                    g.drawText ("no beat to follow", juce::Rectangle<float> (x - 16.0f, kPadCy + 7.0f, 70.0f, 12.0f), juce::Justification::centred);
                    g.setFont (11.0f);
                }
            }
        }
        {   // Conversation, up and down: whose colour the other takes
            g.setFont (10.0f);
            g.setColour (kLight.withAlpha (0.5f));
            g.drawText (juce::String (pr * 2 + 1) + " takes the colour of " + juce::String (pr * 2 + 2), juce::Rectangle<float> (kPadCx[pr] - 70.0f, kPadCy - 84.0f, 140.0f, 14.0f), juce::Justification::centred);
            g.drawText (juce::String (pr * 2 + 2) + " takes the colour of " + juce::String (pr * 2 + 1), juce::Rectangle<float> (kPadCx[pr] - 70.0f, kPadCy + 74.0f, 140.0f, 14.0f), juce::Justification::centred);
        }
        // the main flavour of each sound of the pair, at its lower corner (sound 1 / 3 left, 2 / 4 right)
        for (int c = 0; c < 2; ++c)
        {
            const int ch = proc.mainFlavour (pr * 2 + c);
            const juce::Point<float> at { kPadCx[pr] + (c == 1 ? 64.0f : -64.0f), kPadCy + 64.0f };
            const auto fc = familyColour (ch);
            g.setColour (fc); g.fillEllipse (at.x - 11.0f, at.y - 11.0f, 22.0f, 22.0f);
            charSymbol (g, ch, at, 6.5f, kDark);
        }
        {   // the button that opens the characters' panel
            const auto b = charBtn (pr);
            g.setColour (charPanel == pr ? kLight : kLight.withAlpha (0.5f));
            g.drawRoundedRectangle (b, 6.0f, 1.4f);
            for (int k = 0; k < 3; ++k) g.fillRect (b.getX() + 9.0f + (float) k * 6.0f, b.getY() + 9.0f, 2.0f, 14.0f);
        }
    }

    // ================= the sound cards: each its own small screen
    for (int slot = 0; slot < 4; ++slot)
    {
        const auto r = cardRect (slot);
        const auto smp = proc.getSample (slot);
        const auto col = kWada[slot % 5];
        const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (levelShown[(size_t) slot]) * 2.2f);
        if (smp != nullptr && lv > 0.01f)
        {   // it breathes with its level
            g.setColour (col.withAlpha (0.10f + 0.35f * lv));
            g.drawRoundedRectangle (r.expanded (1.5f + 4.0f * lv), 13.0f + 4.0f * lv, 1.5f + 2.5f * lv);
        }
        g.setColour (Colour (0xff1c1b19));
        g.fillRoundedRectangle (r, 12.0f);
        const bool chosen = recAll || selectedSlot == slot;
        g.setColour (chosen ? kAcc.withAlpha (recAll ? 0.6f : 0.9f) : juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, chosen ? 1.6f : 1.0f);
        g.setColour (col);
        g.setFont (font (bold, 15));
        g.drawText (juce::String (slot + 1), juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 4.0f, 20.0f, 18.0f), juce::Justification::centredLeft);
        const auto wv = cardWave (slot);
        if (smp != nullptr)
        {
            const float pa = proc.apvts.getRawParameterValue (slotId ("grstart", slot))->load();
            const float pb = proc.apvts.getRawParameterValue (slotId ("grend", slot))->load();
            const float a0 = juce::jmin (pa, pb), b0 = juce::jmax (pa, pb);
            g.setColour (col.withAlpha (0.12f));
            g.fillRect (wv.getX() + a0 * wv.getWidth(), wv.getY(), (b0 - a0) * wv.getWidth(), wv.getHeight());
            const auto peaks = peaksOf (smp.get(), 60);
            const float cw = wv.getWidth() / (float) juce::jmax (1, (int) peaks.size());
            for (int i = 0; i < peaks.size(); ++i)
            {
                const float f = ((float) i + 0.5f) / (float) peaks.size(), h = juce::jmax (0.6f, peaks[i] * wv.getHeight() * 0.48f);
                g.setColour ((f >= a0 && f <= b0) ? col.withAlpha (0.9f) : kMuted.withAlpha (0.3f));
                g.fillRect (wv.getX() + (float) i * cw, wv.getCentreY() - h, juce::jmax (1.0f, cw * 0.6f), 2 * h);
            }
            const float ph = proc.playheadOf (slot);
            if (ph >= 0.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillRect (wv.getX() + (a0 + ph * (b0 - a0)) * wv.getWidth() - 0.6f, wv.getY() - 2.0f, 1.2f, wv.getHeight() + 4.0f);
            }
            const bool reverse = proc.knobNow01 (slotId ("grrev", slot)) > 0.3f;
            for (const auto& sp : sparks)
                if ((int) sp.y == slot)
                {   // the grains of this sound, as they are read
                    g.setColour (col.withAlpha (juce::jlimit (0.0f, 1.0f, sp.life)));
                    const float x = wv.getX() + (a0 + sp.x * (b0 - a0)) * wv.getWidth(), y = wv.getY() + (sp.y - (float) slot) * wv.getHeight();
                    const float rad = 1.2f + 1.6f * sp.life;
                    g.fillEllipse (x - rad, y - rad, 2 * rad, 2 * rad);
                    if (reverse)
                    {   // read backwards: a short tail behind it, to the right
                        g.setColour (col.withAlpha (0.4f * juce::jlimit (0.0f, 1.0f, sp.life)));
                        g.drawLine (x, y, x + 7.0f * sp.life, y, 1.0f);
                    }
                }
            g.setColour (kLight);
            g.setFont (font (regular, 12));
            g.drawFittedText (smp->name, juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 74.0f, r.getWidth() - 20.0f, 16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            auto k = proc.keyText (slot);
            k = k.replace (" minor", "m").replace (" major", "");
            if (k == "no key") k = "-";
            if (k.containsChar (' ')) // Harmony moves it: "Em -> +3"
                k = k.upToFirstOccurrenceOf (" ", false, false) + " -> " + k.fromFirstOccurrenceOf (" ", false, false);
            auto t = proc.tempoText (slot);
            t = t.endsWith (" bpm") ? t.upToFirstOccurrenceOf (" ", false, false) : (t == "..." ? t : "-");
            {   // the tempo lock reads it faster or slower: by how much
                const double hb = proc.hostBpmShown();
                const double ratio = proc.tempoRatioFor (slot, smp.get(), hb > 0.0 ? hb : 120.0, hb > 0.0);
                if (std::abs (ratio - 1.0) > 0.005) t << juce::String (juce::CharPointer_UTF8 (" \xc3\x97")) << juce::String (ratio, 2);
            }
            if (juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("groct", slot))->load()) > 0)
            {   // Shimmer: an octave up
                g.setColour (kAcc);
                g.setFont (font (regular, 11));
                g.drawText ("+1 oct", juce::Rectangle<float> (r.getRight() - 56.0f, r.getY() + 5.0f, 46.0f, 16.0f), juce::Justification::centredRight);
            }
            g.setColour (col);
            g.setFont (font (regular, 11));
            g.drawText (k + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + t, keyLabel (slot), juce::Justification::centredLeft);
            if (juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", slot))->load()) == 2)
            {   // its 16 steps, the one playing lit
                const int pat = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grpat", slot))->load());
                const int st = proc.tapStep[(size_t) slot].load();
                for (int s2 = 0; s2 < 16; ++s2)
                {
                    g.setColour (s2 == st ? kAcc : (((pat >> s2) & 1) != 0 ? col.withAlpha (0.55f) : kMuted.withAlpha (0.18f)));
                    g.fillRoundedRectangle (r.getX() + 10.0f + (float) s2 * 4.5f, r.getBottom() - 18.0f, 3.3f, 6.0f, 1.0f);
                }
                if (juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grarp", slot))->load()) > 0 && st >= 0)
                {   // Arp: the note of the step that plays now
                    int tonic = 0; bool minor = false;
                    if (proc.keyOf (slot, tonic, minor))
                    {
                        const int note = ((tonic + juce::roundToInt (proc.harmonyShiftOf (slot) + proc.arpSteps[(size_t) slot][(size_t) (st & 15)])) % 12 + 12) % 12;
                        g.setColour (kAcc);
                        g.setFont (font (bold, 11));
                        g.drawText (Tonality::noteName (note), juce::Rectangle<float> (r.getX() + 84.0f, r.getBottom() - 22.0f, 22.0f, 14.0f), juce::Justification::centred);
                    }
                }
            }
        }
        else
        {
            g.setColour (kMuted);
            g.setFont (font (regular, 12));
            g.drawText ("double-click", wv, juce::Justification::centred);
        }
    }
    auto badge = [&] (juce::Point<float> c, bool on, int kind)
    {
        g.setColour (on ? kAcc : Colour (0xff2f2d2b));
        g.fillEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f);
        g.setColour (on ? kDark : kMuted);
        if (kind == 0)
        {
            g.fillRoundedRectangle (c.x - 4.0f, c.y - 0.5f, 8.0f, 6.0f, 1.2f);
            juce::Path sh;
            sh.addCentredArc (c.x, c.y - 1.0f, 2.7f, 3.0f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (sh, juce::PathStrokeType (1.3f));
        }
        else if (kind == 1)
        {
            g.setFont (font (bold, 11));
            g.drawText ("S", juce::Rectangle<float> (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f), juce::Justification::centred);
        }
        else
        {
            g.drawEllipse (c.x - 5.5f, c.y - 2.8f, 6.5f, 5.6f, 1.2f);
            g.drawEllipse (c.x - 1.0f, c.y - 2.8f, 6.5f, 5.6f, 1.2f);
        }
    };
    const bool micRec = proc.isRecording() && ! proc.isResampling() && proc.recordingSlot() >= 0;
    auto clock = [] (double secs) { const int m = (int) (secs / 60.0); return juce::String::formatted ("%02d:%04.1f", m, secs - 60.0 * m); };
    for (int slot = 0; slot < 4; ++slot)
        if (micRec && proc.recordingSlot() == slot)
        {   // the microphone going into this sound
            const float lv = juce::jlimit (0.0f, 1.0f, proc.getInputLevel() * 1.5f);
            const auto wv = cardWave (slot);
            g.setColour (Colour (0xff1c1b19));
            g.fillRect (wv);
            g.setColour (Colour (0xffe0614f));
            g.fillRoundedRectangle (wv.getX(), wv.getCentreY() - 2.0f, juce::jmax (4.0f, wv.getWidth() * lv), 4.0f, 2.0f);
            g.setColour (kLight);
            g.setFont (font (regular, 12));
            g.drawText (clock (proc.getRecordedSeconds()), wv.withTrimmedBottom (wv.getHeight() * 0.5f), juce::Justification::centred);
        }
    for (int slot = 0; slot < 4; ++slot)
    {
        badge (lockAt (slot), proc.slotLocked (slot), 0);
        badge (soloAt (slot), proc.solo() == slot, 1);
    }
    for (int pr = 0; pr < 2; ++pr)
    {
        const auto c = blendAt (pr);
        badge (c, proc.blendLocked (pr), 2);
        if (proc.vectorOn (pr))
        {   // which of the two is heard more right now: a thin bar under the link, towards that sound's card
            const float x = proc.knobNow01 (pr == 0 ? "xmorph12" : "xmorph34");
            g.setColour (kMuted.withAlpha (0.3f));
            g.fillRoundedRectangle (c.x - 2.0f, c.y + 14.0f, 4.0f, 44.0f, 2.0f);
            g.setColour (kWada[(pr * 2 + (x > 0.5f ? 1 : 0)) % 5]);
            g.fillEllipse (c.x - 4.0f, c.y + 14.0f + 40.0f * x - 0.0f, 8.0f, 8.0f);
            if (juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "character1" : "character2")->load()) == 11)
            {   // Melt: how far the two have run into each other
                const float m = proc.knobNow01 (pr == 0 ? "morph12" : "morph34");
                g.setColour (kAcc.withAlpha (0.8f));
                g.drawEllipse (c.x - 10.0f - 3.0f * m, c.y - 10.0f - 3.0f * m, 20.0f + 6.0f * m, 20.0f + 6.0f * m, 1.0f + m);
            }
        }
    }

    // ================= the centre panel: the library circle (sounds are chosen by dragging), the shared voice below it
    {   // Save to library: an arrow into a tray, in the other corner
        const auto c = kSaveAt;
        g.setColour (kAcc);
        g.fillEllipse (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f);
        g.setColour (kDark);
        g.drawLine (c.x, c.y - 6.5f, c.x, c.y + 2.5f, 1.8f);
        juce::Path head; head.addTriangle (c.x - 4.5f, c.y - 1.5f, c.x + 4.5f, c.y - 1.5f, c.x, c.y + 4.0f);
        g.fillPath (head);
        g.drawLine (c.x - 6.0f, c.y + 7.0f, c.x + 6.0f, c.y + 7.0f, 1.8f);
    }
    {   // Presets: a round button in the corner of the library card (a list)
        const auto c = kPresetAt;
        g.setColour (Colour (0xff2f2d2b));
        g.fillEllipse (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f);
        g.setColour (kLight);
        for (int k = -1; k <= 1; ++k) g.drawLine (c.x - 6.0f, c.y + 4.5f * (float) k, c.x + 6.0f, c.y + 4.5f * (float) k, 1.6f);
    }
    {   // the shared voice
        const float vx = proc.apvts.getRawParameterValue ("vcx")->load(), vy = proc.apvts.getRawParameterValue ("vcy")->load(), vi = proc.apvts.getRawParameterValue ("vci")->load();
        const bool on = vi > 0.02f;
        g.setColour (Colour (0xff1c1b19));
        g.fillRoundedRectangle (kVoicePad, 8.0f);
        g.setColour (kMuted.withAlpha (on ? 0.35f : 0.18f));
        for (int k = 0; k < 7; ++k) g.drawLine (kVoicePad.getX() + 6.0f + (kVoicePad.getWidth() - 12.0f) * (float) k / 6.0f, kVoicePad.getY() + 6.0f, kVoicePad.getX() + 6.0f + (kVoicePad.getWidth() - 12.0f) * (float) k / 6.0f, kVoicePad.getBottom() - 6.0f, 1.0f);
        g.setColour (kMuted.withAlpha (on ? 0.8f : 0.4f));
        g.drawRoundedRectangle (kVoicePad.reduced (0.5f), 8.0f, 1.0f);
        const float dx = kVoicePad.getX() + 6.0f + (kVoicePad.getWidth() - 12.0f) * std::round (vx * 6.0f) / 6.0f, dy = kVoicePad.getY() + 6.0f + (kVoicePad.getHeight() - 12.0f) * vy;
        g.setColour (on ? kAcc : kMuted.withAlpha (0.5f));
        g.fillEllipse (dx - 5.0f, dy - 5.0f, 10.0f, 10.0f);
        g.setColour (Colour (0xff1c1b19));
        g.fillRoundedRectangle (kVoiceBar, 6.0f);
        const float fw = (kVoiceBar.getWidth() - 16.0f) * vi + 8.0f;
        g.setColour (on ? kAcc : kMuted.withAlpha (0.5f));
        g.fillRoundedRectangle (kVoiceBar.getX() + 2.0f, kVoiceBar.getY() + 2.0f, juce::jmax (2.0f, fw), kVoiceBar.getHeight() - 4.0f, 4.0f);
        const float hx = kVoiceBar.getX() + 8.0f + (kVoiceBar.getWidth() - 16.0f) * vi;   // the handle to drag
        g.setColour (on ? kAcc : kMuted.withAlpha (0.7f));
        g.fillEllipse (hx - 9.0f, kVoiceBar.getCentreY() - 9.0f, 18.0f, 18.0f);
        g.setColour (Colour (0xff1c1b19));
        g.drawEllipse (hx - 9.0f, kVoiceBar.getCentreY() - 9.0f, 18.0f, 18.0f, 1.5f);
        g.setColour (kMuted);
        g.setFont (font (regular, 10));
        g.drawText (on ? juce::String (NektarProcessor::voiceName (vx)) : juce::String ("2nd voice"), juce::Rectangle<float> (kVoicePad.getX(), kVoicePad.getY() + 2.0f, kVoicePad.getWidth(), 12.0f), juce::Justification::centred);
    }

    // ================= the bottom panel: the river of the four sounds
    {   // each sound a see-through ribbon: thick when it is loud, high when it is on the left in the space, low on the right;
        // where two sounds meet in the space, their ribbons meet too
        const auto r = kRiver;
        g.setColour (Colour (0xff1c1b19));
        g.fillRoundedRectangle (r, 10.0f);
        const int n = (int) river.size();
        const float left = r.getX() + 10.0f, w = r.getWidth() - 40.0f, dx = w / (float) (n - 1);
        const float mid = r.getCentreY(), hMax = r.getHeight() - 16.0f;
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        for (int i = 0; i < n; ++i)
            if (riverBeat[(size_t) ((riverAt + i) % n)])
                g.fillRect (left + (float) i * dx, r.getY() + 8.0f, 1.0f, r.getHeight() - 16.0f);
        for (int slot = 0; slot < 4; ++slot)
        {
            juce::Path band;
            std::array<float, 300> lo {};
            for (int i = 0; i < n; ++i)
            {
                const int at = (riverAt + i) % n;
                const float half = juce::jlimit (0.0f, hMax * 0.3f, std::sqrt (river[(size_t) at][(size_t) slot]) * hMax * 0.45f);
                const float c = mid - riverPan[(size_t) at][(size_t) slot] * hMax * 0.32f;
                const float x = left + (float) i * dx;
                if (i == 0) band.startNewSubPath (x, c - half); else band.lineTo (x, c - half);
                lo[(size_t) i] = c + half;
            }
            for (int i = n - 1; i >= 0; --i) band.lineTo (left + (float) i * dx, lo[(size_t) i]);
            band.closeSubPath();
            g.setColour (kWada[slot % 5].withAlpha (0.5f));
            g.fillPath (band);
        }
        g.setColour (kMuted.withAlpha (0.6f));
        g.setFont (font (regular, 10));
        g.drawText ("L", juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 4.0f, 12.0f, 12.0f), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (r.getX() + 4.0f, r.getBottom() - 16.0f, 12.0f, 12.0f), juce::Justification::centred);
        if (const double bpm = proc.hostBpmShown(); bpm > 0.0)
        {   // the host's tempo, with a dot on the beat
            const bool onBeat = proc.beatOn.load() && proc.beatNow.load() < 0.15f;
            g.setColour (onBeat ? kAcc : kAcc.withAlpha (0.25f));
            g.fillEllipse (r.getRight() - 96.0f, r.getY() + 9.0f, 7.0f, 7.0f);
            g.setColour (kLight.withAlpha (0.8f));
            g.setFont (font (regular, 11));
            g.drawText (juce::String (juce::roundToInt (bpm)) + " bpm", juce::Rectangle<float> (r.getRight() - 86.0f, r.getY() + 5.0f, 56.0f, 14.0f), juce::Justification::centredLeft);
        }
        {   // the output level; red at the top while the safety knee is working
            const auto m = juce::Rectangle<float> (r.getRight() - 18.0f, r.getY() + 8.0f, 7.0f, r.getHeight() - 16.0f);
            g.setColour (Colour (0xff2f2d2b));
            g.fillRoundedRectangle (m, 3.0f);
            const float db = juce::Decibels::gainToDecibels (outShown, -60.0f);
            const float f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            g.setColour (kWada[3].withAlpha (0.85f));
            g.fillRoundedRectangle (m.withTop (m.getBottom() - m.getHeight() * f), 3.0f);
            if (clipHold > 0)
            {
                g.setColour (Colour (0xffe0614f));
                g.fillRoundedRectangle (m.withHeight (8.0f), 3.0f);
            }
        }
    }

    {   // Rec and Resample in one oval; in the middle, where the take goes: one chosen sound, or a new sound in the library
        const bool rs = proc.isResampling();
        const bool busyRec = micRec || rs;
        g.setColour (Colour (0xff1e1d1b));   // the same as the two effects beside it
        g.fillRoundedRectangle (kRecOval, 15.0f);
        g.setColour (kMuted.withAlpha (0.8f));
        g.drawRoundedRectangle (kRecOval.reduced (0.75f), 15.0f, 1.5f);
        const int tgt = busyRec ? proc.recordingSlot() : (recAll ? NektarProcessor::kToLibrary : selectedSlot);
        g.setColour (busyRec ? kLight : (tgt >= 0 ? kWada[tgt % 5] : kLight.withAlpha (0.8f)));
        g.setFont (font (regular, busyRec ? 12 : 13));
        const juce::String where = tgt >= 0 ? "-> " + juce::String (tgt + 1) : juce::String ("-> library");
        g.drawText (busyRec ? clock (proc.getRecordedSeconds()) : where, kRecOval.reduced (32.0f, 0.0f), juce::Justification::centred);
        {   // Rec (the buttons are drawn at 0.8: the oval is as high as an effect's)
            const auto c = kRecAt;
            g.saveState(); g.addTransform (juce::AffineTransform::scale (0.8f, 0.8f, c.x, c.y));
            if (micRec)
            {
                const float lv = juce::jlimit (0.0f, 1.0f, proc.getInputLevel() * 1.5f);
                g.setColour (Colour (0xffe0614f).withAlpha (0.25f + 0.5f * lv));
                g.fillEllipse (c.x - 15.0f - 4.0f * lv, c.y - 15.0f - 4.0f * lv, 30.0f + 8.0f * lv, 30.0f + 8.0f * lv);
            }
            g.setColour (micRec ? Colour (0xffe0614f) : Colour (0xff3a3936));
            g.fillEllipse (c.x - 15.0f, c.y - 15.0f, 30.0f, 30.0f);
            g.setColour (micRec ? kDark : Colour (0xffe0614f).withAlpha (proc.canRecord() ? 1.0f : 0.4f));
            if (micRec) g.fillRect (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
            else g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
            g.restoreState();
        }
        {   // Resample
            const auto c = kResampleAt;
            g.saveState(); g.addTransform (juce::AffineTransform::scale (0.8f, 0.8f, c.x, c.y));
            if (rs)
            {
                g.setColour (kAcc.withAlpha (0.3f + 0.2f * (float) std::sin (now * 4.0)));
                g.fillEllipse (c.x - 19.0f, c.y - 19.0f, 38.0f, 38.0f);
            }
            g.setColour (rs ? kAcc : Colour (0xff3a3936));
            g.fillEllipse (c.x - 15.0f, c.y - 15.0f, 30.0f, 30.0f);
            g.setColour (rs ? kDark : kLight);
            if (rs) g.fillRoundedRectangle (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 2.0f);
            else
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, 7.0f, 7.0f, 0.0f, 0.6f, 5.6f, true);
                g.strokePath (arc, juce::PathStrokeType (1.7f));
                juce::Path head;
                head.addTriangle (c.x + 3.5f, c.y - 8.5f, c.x + 10.0f, c.y - 6.5f, c.x + 5.5f, c.y - 1.5f);
                g.fillPath (head);
            }
            g.restoreState();
        }
    }

    // ================= two sets of three rings: the whole sound (Motion, Weight, Light) and the chosen sound (rhythm, colour, texture)
    auto rings = [&] (juce::Point<float> at, const float* values, const juce::Colour* cols, int dragBase)
    {
        const float a0 = juce::MathConstants<float>::pi * 0.75f, span = juce::MathConstants<float>::pi * 1.5f;
        for (int i = 0; i < 3; ++i)
        {
            const float v = juce::jlimit (0.0f, 1.0f, values[i]);
            juce::Path track, fill;
            track.addCentredArc (at.x, at.y, kArcR[i], kArcR[i], 0.0f, a0 + juce::MathConstants<float>::halfPi, a0 + span + juce::MathConstants<float>::halfPi, true);
            fill.addCentredArc (at.x, at.y, kArcR[i], kArcR[i], 0.0f, a0 + juce::MathConstants<float>::halfPi, a0 + span * v + juce::MathConstants<float>::halfPi, true);
            g.setColour (Colour (0xff2a2927));
            g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (v > 0.004f)
            {
                g.setColour (cols[i].withAlpha (arcDrag == dragBase + i ? 1.0f : 0.85f));
                g.strokePath (fill, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    };
    // the symbol of the ring under the mouse (or being dragged) shows in the middle of its set
    const int shownRing = arcDrag >= 0 ? arcDrag : hoverRing;
    {
        g.setColour (Colour (0xff1c1b19));
        g.fillEllipse (kArcs.x - 11.0f, kArcs.y - 11.0f, 22.0f, 22.0f);
        const juce::Colour cols[3] = { kWada[1], kWada[2], kWada[3] };
        float v[3];
        for (int i = 0; i < 3; ++i) v[i] = proc.apvts.getRawParameterValue (kArcIds[i])->load();
        rings (kArcs, v, cols, 0);
        if (shownRing >= 0 && shownRing < 3) arcIcon (g, shownRing, kArcs, cols[shownRing]);
    }
    {   // the chosen sound: its colour and its number in the middle (all four: "1-4")
        const auto col = recAll ? kLight : kWada[selectedSlot % 5];
        const juce::Colour cols[3] = { col.interpolatedWith (kLight, 0.45f), col, col.withMultipliedBrightness (0.7f) };
        juce::ignoreUnused (cols); // (the rings for how much Distill changes are gone: it draws anew)
        g.setColour (col);
        g.fillRoundedRectangle (kMkOval, 16.0f);
        g.setColour (kDark);
        g.setFont (font (bold, 14));
        const juce::String lab = proc.makeMask == 1 ? "1" : proc.makeMask == 3 ? "1 + 2" : proc.makeMask == 7 ? "1 + 2 + 3" : proc.makeMask == 15 ? "1 - 4" : juce::String (selectedSlot + 1);
        g.drawText (lab, kMkOval, juce::Justification::centred);
    }

    bool anySound = false;
    for (int slot = 0; slot < 4; ++slot) anySound = anySound || proc.getSample (slot) != nullptr;
    // ================= Keep, Distill, Back
    const bool busy = proc.baking();
    for (int pr = 0; pr < 2; ++pr)
    {
        g.setColour (Colour (0xff2f2d2b));
        g.fillEllipse (keepAt (pr).x - 15.0f, keepAt (pr).y - 15.0f, 30.0f, 30.0f);
        keepSymbol (g, keepAt (pr).translated (0.0f, 2.0f), kLight);
    }
    {   // Distill breathes slowly while the corners are being prepared
        const float br = busy ? 0.55f + 0.45f * (0.5f + 0.5f * (float) std::sin (now * 2.4)) : 1.0f;
        g.setColour ((anySound ? kAcc : Colour (0xff2f2d2b)).withMultipliedAlpha (br));
        g.fillRoundedRectangle (kMakeBtn, kMakeBtn.getHeight() * 0.5f);
        g.setColour (anySound ? kDark : kMuted);
        g.setFont (font (bold, 21));
        g.drawText ("Distill", kMakeBtn, juce::Justification::centred);
    }
    button (kBackBtn, proc.vectorHistory (0) > 0 || proc.vectorHistory (1) > 0, false);
    backSymbol (g, kBackBtn.getCentre(), (proc.vectorHistory (0) > 0 || proc.vectorHistory (1) > 0) ? kLight : kMuted.withAlpha (0.45f));
    if (message.isNotEmpty())
    {
        g.setColour (kLight.withAlpha (0.75f));
        g.setFont (font (regular, 12));
        g.drawText (message, kMsgLine, juce::Justification::centred);
    }

    // ================= Harmony and Key
    const int hm = juce::roundToInt (proc.apvts.getRawParameterValue ("harmony")->load());
    for (int i = 0; i < 3; ++i)
    {
        const auto r = harmChip (i);
        g.setColour (i == hm ? kAcc : Colour (0xff2f2d2b));
        g.fillRoundedRectangle (r, 16);
        harmonySymbol (g, i, r.getCentre(), i == hm ? kDark : kLight);
    }
    {
        const auto r = kKeyBtn;
        g.setColour (Colour (0xff2f2d2b));
        g.fillRoundedRectangle (r, 16);
        const int key = juce::roundToInt (proc.apvts.getRawParameterValue ("key")->load());
        // a small keyboard of one octave with the key lit, and its name
        const float kx = r.getX() + 12.0f, ky = r.getY() + 7.0f, kw = 4.6f;
        static const int white[7] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int w = 0; w < 7; ++w)
        {
            g.setColour (white[w] == key && hm != 0 ? kAcc : kLight.withAlpha (hm != 0 ? 0.9f : 0.45f));
            g.fillRect (kx + (float) w * (kw + 0.8f), ky, kw, 18.0f);
        }
        static const int black[5] = { 1, 3, 6, 8, 10 };
        static const float bpos[5] = { 0.7f, 1.7f, 3.7f, 4.7f, 5.7f };
        for (int b2 = 0; b2 < 5; ++b2)
        {
            g.setColour (black[b2] == key && hm != 0 ? kAcc : Colour (0xff1c1b19));
            g.fillRect (kx + bpos[b2] * (kw + 0.8f), ky, kw * 0.75f, 11.0f);
        }
        g.setColour (hm != 0 ? kLight : kMuted.withAlpha (0.45f));
        g.setFont (font (bold, 15));
        g.drawText (Tonality::noteName (key), juce::Rectangle<float> (r.getRight() - 40.0f, r.getY(), 32.0f, r.getHeight()), juce::Justification::centred);
    }
}

int NektarEditor::panelSound() const
{   // the sound the flavours' panel shows: the chosen card, if it belongs to the pair whose panel is open
    if (! recAll && selectedSlot / 2 == charPanel) return selectedSlot;
    return juce::jlimit (0, 3, panelSnd >= 0 && panelSnd / 2 == charPanel ? panelSnd : charPanel * 2);
}

static int benchMode (int idx)
{   // the mode in which a control is heard: 1 free grains (Speed, Grains, Density), 2 steps (Cut), 0 either
    return idx == 0 || idx == 1 || idx == 7 ? 1 : idx == 6 ? 2 : 0;
}

float NektarEditor::benchGet (int idx, int snd) const
{
    const auto& c = kBench[idx];
    const juce::String id = slotId (c.id, snd);
    const float raw = proc.apvts.getRawParameterValue (id)->load();
    if (idx == 0) return juce::jlimit (0.0f, 1.0f, raw);              // speed: 0 frozen .. 1 as the sound goes
    if (c.kind == 1) return raw > 0.3f ? 1.0f : 0.0f;
    if (c.kind == 2) return raw / 5.0f;
    if (c.kind == 3) return raw > 1.5f ? 1.0f : 0.0f;
    if (auto* pa = proc.apvts.getParameter (id)) return pa->convertTo0to1 (raw);
    return 0.0f;
}

void NektarEditor::benchSet (int idx, int snd, float v)
{
    const auto& c = kBench[idx];
    auto setRaw = [this] (const juce::String& id, float raw) { if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 (raw)); };
    v = juce::jlimit (0.0f, 1.0f, v);
    if (c.grains && idx != 8)
    {   // the grains are what these controls shape: they come on, in the mode where the control is heard
        const int mode = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", snd))->load());
        const int want = benchMode (idx);
        if (want > 0 && mode != want) setRaw (slotId ("grmode", snd), (float) want);
        else if (mode == 0) setRaw (slotId ("grmode", snd), 1.0f);
    }
    const juce::String id = slotId (c.id, snd);
    if (idx == 0) setRaw (id, v);
    else if (c.kind == 1) setRaw (id, v > 0.5f ? 1.0f : 0.0f);
    else if (c.kind == 2) setRaw (id, (float) juce::roundToInt (v * 5.0f));
    else if (c.kind == 3) setRaw (id, v > 0.5f ? 2.0f : 1.0f);
    else if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (v);
}

void NektarEditor::drawCharPanel (juce::Graphics& g)
{   // the bench of one sound: its number on the left (click: the other sound of the pair), then four stages of three controls
    const int snd = panelSound();
    const auto R = kCharPanelR();
    const auto col = kWada[snd % 5];
    g.setColour (Colour (0xff2a2927)); g.fillRoundedRectangle (R, 10.0f);
    g.setColour (kLight.withAlpha (0.25f)); g.drawRoundedRectangle (R, 10.0f, 1.0f);
    {
        const auto tab = juce::Rectangle<float> (R.getX() + 6.0f, R.getY() + 6.0f, 36.0f, R.getHeight() - 12.0f);
        g.setColour (Colour (0xff333230)); g.fillRoundedRectangle (tab, 6.0f);
        g.setColour (col); g.setFont (22.0f);
        g.drawText (juce::String (snd + 1), tab.withHeight (40.0f), juce::Justification::centred);
        g.setColour (kLight.withAlpha (0.45f)); g.setFont (11.0f);
        g.drawText (juce::String ((snd % 2 == 0 ? snd + 2 : snd)), tab.withY (tab.getBottom() - 30.0f).withHeight (24.0f), juce::Justification::centred);
    }
    const auto smp = proc.getSample (snd);
    {   // the flavours: places to start from (the knobs below move there, and are there to be turned)
        g.setColour (kLight.withAlpha (0.55f)); g.setFont (11.0f);
        g.drawText ("Start from", juce::Rectangle<float> (58.0f, 477.0f, 80.0f, 20.0f), juce::Justification::centredLeft);
        const int mainF = proc.mainFlavour (snd);
        for (int i = 0; i < 7; ++i)
        {
            const int ch = kBenchFlavours[i];
            const auto at = benchChipAt (i);
            const auto fc = familyColour (ch);
            g.setColour (fc.withAlpha (ch == mainF ? 1.0f : 0.55f)); g.fillEllipse (at.x - 11.0f, at.y - 11.0f, 22.0f, 22.0f);
            if (ch == mainF) { g.setColour (kLight); g.drawEllipse (at.x - 13.5f, at.y - 13.5f, 27.0f, 27.0f, 1.4f); }
            charSymbol (g, ch, at, 6.5f, kDark);
        }
        const int mf = mainF;
        g.setColour (kLight.withAlpha (0.45f)); g.setFont (11.0f);
        g.drawText (kCharNames[mf], juce::Rectangle<float> (420.0f, 477.0f, 120.0f, 20.0f), juce::Justification::centredLeft);
    }
    for (int i = 0; i < 4; ++i)
    {
        const auto st = benchStage (i);
        g.setColour (Colour (0xff333230)); g.fillRoundedRectangle (st, 6.0f);
        g.setColour (kLight.withAlpha (0.18f)); g.drawRoundedRectangle (st, 6.0f, 1.0f);
        g.setColour (kLight.withAlpha (0.8f)); g.setFont (12.0f);
        g.drawText (kBenchStage[i], st.withHeight (22.0f), juce::Justification::centred);
    }
    {   // Material: the sound's wave, the part in use and the point inside it
        const auto wv = benchWave();
        g.setColour (Colour (0xff1c1b19)); g.fillRoundedRectangle (wv, 4.0f);
        if (smp != nullptr)
        {
            const float pa = proc.apvts.getRawParameterValue (slotId ("grstart", snd))->load(), pb = proc.apvts.getRawParameterValue (slotId ("grend", snd))->load();
            const float a0 = juce::jmin (pa, pb), b0 = juce::jmax (pa, pb);
            g.setColour (col.withAlpha (0.14f)); g.fillRect (wv.getX() + a0 * wv.getWidth(), wv.getY(), (b0 - a0) * wv.getWidth(), wv.getHeight());
            const auto peaks = peaksOf (smp.get(), 100);
            const float cw = wv.getWidth() / (float) juce::jmax (1, (int) peaks.size());
            for (int i = 0; i < peaks.size(); ++i)
            {
                const float f = ((float) i + 0.5f) / (float) peaks.size(), h = juce::jmax (0.6f, peaks[i] * wv.getHeight() * 0.48f);
                g.setColour ((f >= a0 && f <= b0) ? col.withAlpha (0.9f) : kMuted.withAlpha (0.3f));
                g.fillRect (wv.getX() + (float) i * cw, wv.getCentreY() - h, juce::jmax (1.0f, cw * 0.6f), 2 * h);
            }
            const float pos = proc.apvts.getRawParameterValue (slotId ("grpos", snd))->load();
            g.setColour (kLight); g.fillRect (wv.getX() + (a0 + pos * (b0 - a0)) * wv.getWidth() - 1.0f, wv.getY() - 3.0f, 2.0f, wv.getHeight() + 6.0f);
            const float ph = proc.playheadOf (snd);
            if (ph >= 0.0f) { g.setColour (juce::Colours::white.withAlpha (0.7f)); g.fillRect (wv.getX() + (a0 + ph * (b0 - a0)) * wv.getWidth() - 0.6f, wv.getY(), 1.2f, wv.getHeight()); }
        }
    }
    {   // Timbre: the curve of what the three knobs do to the spectrum, as it is now
        const auto cv = benchStage (1).withTrimmedTop (25.0f).withHeight (52.0f).reduced (8.0f, 0.0f);
        g.setColour (Colour (0xff1c1b19)); g.fillRoundedRectangle (cv, 4.0f);
        const float li = proc.apvts.getRawParameterValue (slotId ("blight", snd))->load(), bo = proc.apvts.getRawParameterValue (slotId ("bbody", snd))->load(),
                    ai = proc.apvts.getRawParameterValue (slotId ("bair", snd))->load();
        g.setColour (kLight.withAlpha (0.15f)); g.drawHorizontalLine ((int) cv.getCentreY(), cv.getX(), cv.getRight());
        juce::Path pth;
        for (int x = 0; x <= 60; ++x)
        {
            const float f = 30.0f * std::pow (18000.0f / 30.0f, (float) x / 60.0f);
            const auto sig = [] (float a, float b) { return 1.0f / (1.0f + std::pow (a / b, 2.0f)); }; // 0 under b, 1 over b
            const float hi = sig (f, 1000.0f), lo = 1.0f - hi;
            const float db = (li - 0.5f) * 18.0f * (hi - lo) + (bo - 0.5f) * 22.0f * (1.0f - sig (f, 220.0f)) + (ai - 0.5f) * 22.0f * sig (f, 8000.0f);
            const float px = cv.getX() + cv.getWidth() * (float) x / 60.0f, py = cv.getCentreY() - juce::jlimit (-1.0f, 1.0f, db / 22.0f) * cv.getHeight() * 0.45f;
            if (x == 0) pth.startNewSubPath (px, py); else pth.lineTo (px, py);
        }
        g.setColour (col); g.strokePath (pth, juce::PathStrokeType (2.0f));
    }
    {   // Rhythm: the 16 steps
        const int pat = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grpat", snd))->load());
        const bool steps = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", snd))->load()) == 2;
        for (int j = 0; j < 16; ++j)
        {
            const auto r = benchStepAt (j);
            const bool on = ((pat >> j) & 1) != 0;
            g.setColour (on ? col.withAlpha (steps ? 0.95f : 0.35f) : kLight.withAlpha (0.12f));
            g.fillRoundedRectangle (r, 2.5f);
        }
        g.setColour (kLight.withAlpha (0.45f)); g.setFont (10.0f);
        g.drawText (steps ? "16 steps, in time" : "the steps play when Pattern is on", benchStage (2).withY (benchStage (2).getY() + 60.0f).withHeight (14.0f), juce::Justification::centred);
    }
    {   // Motion: the movement in the room
        const int mv = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("spmove", snd))->load());
        const auto c = benchStage (3).getCentre().withY (benchStage (3).getY() + 50.0f);
        g.setColour (mv > 0 ? col : Colour (0xff3a3936)); g.fillEllipse (c.x - 22.0f, c.y - 22.0f, 44.0f, 44.0f);
        moveSymbol (g, mv, c, 14.0f, mv > 0 ? kDark : kLight);
    }
    {   // Swell: the level of the pair breathes (it belongs to the pair, not to one sound)
        const bool on = proc.apvts.getRawParameterValue (snd < 2 ? "swell1" : "swell2")->load() > 0.5f;
        const auto r = benchSwellChip();
        g.setColour (on ? col : kLight.withAlpha (0.25f)); on ? g.fillRoundedRectangle (r, 11.0f) : g.drawRoundedRectangle (r, 11.0f, 1.4f);
        g.setColour (on ? kDark : kLight.withAlpha (0.6f)); g.setFont (10.0f);
        g.drawText ("Swell", r, juce::Justification::centred);
    }
    for (int idx = 0; idx < 14; ++idx)
    {
        const auto& c = kBench[idx];
        const auto at = benchKnobAt (idx);
        const float v = benchGet (idx, snd);
        const int modeNow = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", snd))->load());
        const bool silent = c.grains && idx != 8 && benchMode (idx) > 0 && modeNow != benchMode (idx); // not heard in this mode: touch it and the mode changes
        const float al = silent ? 0.4f : 1.0f;
        if (c.kind == 3 || c.kind == 1)
        {
            const bool on = v > 0.5f;
            const auto r = juce::Rectangle<float> (at.x - 24.0f, at.y - 11.0f, 48.0f, 22.0f);
            g.setColour (on ? col : kLight.withAlpha (0.25f)); on ? g.fillRoundedRectangle (r, 11.0f) : g.drawRoundedRectangle (r, 11.0f, 1.4f);
            g.setColour (on ? kDark : kLight.withAlpha (0.6f)); g.setFont (10.0f);
            g.drawText (c.kind == 3 ? (on ? "Steps" : "Free") : (on ? "On" : "Off"), r, juce::Justification::centred);
        }
        else
        {
            g.setColour (kLight.withAlpha (0.2f * al)); g.drawEllipse (at.x - 13.0f, at.y - 13.0f, 26.0f, 26.0f, 2.5f);
            juce::Path a; a.addCentredArc (at.x, at.y, 13.0f, 13.0f, 0.0f, -2.4f, -2.4f + 4.8f * v, true);
            g.setColour (col.withAlpha (al)); g.strokePath (a, juce::PathStrokeType (2.8f));
        }
        g.setFont (10.5f); g.setColour (kLight.withAlpha (0.65f * al));
        g.drawText (c.name, juce::Rectangle<float> (at.x - 38.0f, at.y + 15.0f, 76.0f, 13.0f), juce::Justification::centred);
    }
}

bool NektarEditor::charPanelClick (juce::Point<float> p)
{
    for (int pr = 0; pr < 2; ++pr)
        if (charBtn (pr).contains (p))
        {
            setBenchOpen (charPanel == pr ? -1 : pr);
            return true;
        }
    if (charPanel < 0 || ! kCharPanelR().contains (p)) return false;
    const int snd = panelSound();
    if (p.x < kCharPanelR().getX() + 44.0f)
    {   // the other sound of the pair
        panelSnd = snd % 2 == 0 ? snd + 1 : snd - 1;
        recAll = true; proc.makeMask = 15; // (the panel follows its own choice, not the chosen card)
        say ("Bench of sound " + juce::String (panelSnd + 1) + ".");
        repaint(); return true;
    }
    for (int i = 0; i < 7; ++i)
        if (p.getDistanceFrom (benchChipAt (i)) <= 13.0f)
        {
            proc.startFromFlavour (snd, kBenchFlavours[i]);
            say ("Sound " + juce::String (snd + 1) + " starts from " + kCharNames[kBenchFlavours[i]] + ". Turn the knobs from there.");
            repaint(); return true;
        }
    if (proc.getSample (snd) != nullptr && benchWave().contains (p))
    {   // press at one end of the part and drag to the other; a click alone puts the point there
        const auto wv = benchWave();
        partFrom = juce::jlimit (0.0f, 1.0f, (p.x - wv.getX()) / wv.getWidth());
        partDrag = snd; partOnBench = true; partMoved = false;
        return true;
    }
    for (int j = 0; j < 16; ++j)
        if (benchStepAt (j).expanded (1.0f).contains (p))
        {
            const juce::String id = slotId ("grpat", snd);
            const int pat = juce::roundToInt (proc.apvts.getRawParameterValue (id)->load()) ^ (1 << j);
            if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) pat));
            if (juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", snd))->load()) != 2) benchSet (8, snd, 1.0f);
            repaint(); return true;
        }
    if (benchSwellChip().contains (p))
    {
        auto* pa = proc.apvts.getParameter (snd < 2 ? "swell1" : "swell2");
        const bool on = pa->getValue() > 0.5f;
        pa->setValueNotifyingHost (on ? 0.0f : 1.0f);
        say (juce::String ("Swell of sounds ") + (snd < 2 ? "1 and 2" : "3 and 4") + (on ? ": off." : ": on, the pair's level breathes."));
        repaint(); return true;
    }
    for (int idx = 0; idx < 14; ++idx)
    {
        const auto& c = kBench[idx];
        const auto at = benchKnobAt (idx);
        if (c.kind == 1 || c.kind == 3)
        {
            if (juce::Rectangle<float> (at.x - 26.0f, at.y - 13.0f, 52.0f, 26.0f).contains (p))
            {
                benchSet (idx, snd, benchGet (idx, snd) > 0.5f ? 0.0f : 1.0f);
                say (juce::String (c.name) + " of sound " + juce::String (snd + 1) + (benchGet (idx, snd) > 0.5f ? ": on." : ": off."));
                repaint(); return true;
            }
        }
        else if (p.getDistanceFrom (at) <= 16.0f)
        {
            chkDrag = 2000 + idx; chkY = p.y; chkFrom = benchGet (idx, snd);
            say (juce::String (c.name) + " of sound " + juce::String (snd + 1) + ": drag up or down.");
            return true;
        }
    }
    return true; // a click on the panel does not reach what is under it
}

void NektarEditor::setBenchOpen (int pair)
{   // opens the bench for the pair (-1: closes it); what sits under it steps aside
    charPanel = pair;
    panelSnd = -1;
    const float sc = (float) getWidth() / kDW;
    for (auto* c : getChildren())
        if (c != pairPads[0].get() && c != pairPads[1].get() && c->getBounds().toFloat().intersects (kCharPanelR() * sc))
            c->setVisible (charPanel < 0);
    repaint();
}

void NektarEditor::drawSpaceOval (Graphics& g, juce::Rectangle<float> r)
{   // the oval on the rim of the Space circle: the chosen sound, its movement, its speed (the speed knob sits on it)
    const float s = r.getWidth() / kSpaceOval.getWidth();
    g.setColour (Colour (0xff2a2927));
    g.fillRoundedRectangle (r.reduced (1.0f), r.getHeight() * 0.5f);
    g.setColour (Colour (0xff7ec8e3).withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (1.0f), r.getHeight() * 0.5f, 1.4f);
    const auto col = kWada[spaceSlot % 5];
    g.setColour (col);
    g.setFont (font (bold, 13.0f * s));
    g.drawText (juce::String (spaceSlot + 1), juce::Rectangle<float> (8.0f * s, 0.0f, 16.0f * s, r.getHeight()), juce::Justification::centred);
    const int mv = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("spmove", spaceSlot))->load());
    const juce::Point<float> c (38.0f * s, r.getCentreY());
    g.setColour (mv > 0 ? col : Colour (0xff3a3936));
    g.fillEllipse (c.x - 11.0f * s, c.y - 11.0f * s, 22.0f * s, 22.0f * s);
    moveSymbol (g, mv, c, 7.5f * s, mv > 0 ? kDark : kLight);
}

void NektarEditor::spaceOvalClick (const juce::MouseEvent& e)
{
    const float s = (float) spaceOval->getWidth() / kSpaceOval.getWidth();
    if (e.position.x < 54.0f * s)
    {   // the chosen sound's next movement
        const juce::String id = slotId ("spmove", spaceSlot);
        const int cur = juce::roundToInt (proc.apvts.getRawParameterValue (id)->load());
        if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) ((cur + (e.mods.isShiftDown() ? 5 : 1)) % 6)));
        const char* names[6] = { "Still", "Left - right", "Front - back", "An arc in front", "An arc behind", "A circle round you" };
        say ("Sound " + juce::String (spaceSlot + 1) + ": " + names[(cur + (e.mods.isShiftDown() ? 5 : 1)) % 6] + ".");
        spaceOval->repaint();
        repaint();
    }
}

void NektarEditor::voiceSet (juce::Point<float> p)
{
    auto set = [this] (const char* id, float v) { if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jlimit (0.0f, 1.0f, v))); };
    if (voiceDrag == 1)
    {
        const float vx = (p.x - kVoicePad.getX() - 6.0f) / (kVoicePad.getWidth() - 12.0f), vy = (p.y - kVoicePad.getY() - 6.0f) / (kVoicePad.getHeight() - 12.0f);
        const float stop = std::round (juce::jlimit (0.0f, 1.0f, vx) * 6.0f) / 6.0f;
        set ("vcx", stop); set ("vcy", vy);
        if (proc.apvts.getRawParameterValue ("vci")->load() <= 0.02f) set ("vci", 0.7f); // touching the pad switches the voice on
        say (juce::String ("Second voice: ") + NektarProcessor::voiceName (stop) + (vy < 0.4f ? ", near" : vy > 0.6f ? ", far" : ", at the distance of pair 2"));
    }
    else if (voiceDrag == 2)
    {
        const float v = (p.x - kVoiceBar.getX() - 8.0f) / (kVoiceBar.getWidth() - 16.0f);
        set ("vci", v < 0.04f ? 0.0f : v);
        say (v < 0.04f ? "Second voice: off" : "Second voice: " + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 100.0f)) + " %");
    }
    repaint();
}

void NektarEditor::faceClick (const juce::MouseEvent& e, juce::Point<float> p)
{
    auto setInt = [this] (const juce::String& id, float v) { if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 (v)); };
    auto intOf = [this] (const juce::String& id) { return juce::roundToInt (proc.apvts.getRawParameterValue (id)->load()); };
    if (kVoicePad.expanded (4.0f).contains (p)) { voiceDrag = 1; voiceSet (p); return; }
    if (kVoiceBar.expanded (10.0f, 6.0f).contains (p)) { voiceDrag = 2; voiceSet (p); return; }
    if (p.getDistanceFrom (kPresetAt) <= 15.0f) { showPresetMenu(); return; }
    if (p.getDistanceFrom (kSaveAt) <= 15.0f) { say (proc.saveToLibrary (selectedSlot)); repaint(); return; }
    if (kMakeBtn.contains (p)) { say (proc.makeAll (recAll ? -1 : selectedSlot)); repaint(); return; }
    if (kBackBtn.contains (p)) { if (proc.backAll()) say ("Back to what was there before the last Distill."); repaint(); return; }
    for (int pr = 0; pr < 2; ++pr) if (p.getDistanceFrom (keepAt (pr)) <= 15.0f) { pressKeep (pr); return; }
    if (kMkOval.contains (p))
    {
        const int order[4] = { 1, 3, 7, 15 };
        int i = 0; while (i < 4 && order[i] != proc.makeMask) ++i;
        proc.makeMask = order[(i + 1) % 4];
        recAll = (proc.makeMask != 1 && proc.makeMask != 2 && proc.makeMask != 4 && proc.makeMask != 8);
        if (! recAll) selectedSlot = proc.makeMask == 1 ? 0 : proc.makeMask == 2 ? 1 : proc.makeMask == 4 ? 2 : 3;
        say (proc.makeMask == 1 ? "Sound 1: Distill, Rec and Resample use it." : "Distill changes these sounds; Rec and Resample make a new sound in the library.");
        repaint(); return;
    }
    for (int pr = 0; pr < 2; ++pr)
    {
        if (p.getDistanceFrom (shapeAt (pr)) <= 15.0f)
        {   // the next movement of the stick (none, figure eight, spiral, Lissajous, flower)
            const juce::String id = pr == 0 ? "vec1shape" : "vec2shape";
            setInt (id, (float) ((intOf (id) + (e.mods.isShiftDown() ? 4 : 1)) % 5));
            repaint();
            return;
        }
        if (intOf (pr == 0 ? "vec1shape" : "vec2shape") > 0 && speedDots (pr).expanded (3.0f).contains (p))
        {
            const juce::String id = pr == 0 ? "vec1speed" : "vec2speed";
            setInt (id, (float) ((intOf (id) + 1) % 5));
            repaint();
            return;
        }
    }
    if (p.getDistanceFrom (kResampleAt) <= 13.0f) { toggleResample(); repaint(); return; }
    if (p.getDistanceFrom (kRecAt) <= 13.0f) { toggleRecording(); repaint(); return; }
    if (kRiver.contains (p)) { recAll = true; proc.makeMask = 15; say ("All four sounds: Rec and Resample make a new sound in the library."); repaint(); return; }
    for (int slot = 0; slot < 4; ++slot)
    {
        if (p.getDistanceFrom (lockAt (slot)) <= 10.0f)
        {
            proc.setSlotLock (slot, ! proc.slotLocked (slot));
            say (proc.slotLocked (slot) ? "Sound " + juce::String (slot + 1) + " is locked: Distill and Random sounds leave it as it is."
                                        : "Sound " + juce::String (slot + 1) + " is free again.");
            repaint();
            return;
        }
        if (p.getDistanceFrom (soloAt (slot)) <= 10.0f)
        {
            proc.setSolo (proc.solo() == slot ? -1 : slot);
            say (proc.solo() == slot ? "Only sound " + juce::String (slot + 1) + " is heard." : "All four sounds are heard.");
            repaint();
            return;
        }
    }
    for (int pr = 0; pr < 2; ++pr)
        if (p.getDistanceFrom (blendAt (pr)) <= 10.0f)
        {
            proc.setBlendLock (pr, ! proc.blendLocked (pr));
            say (juce::String ("How sounds ") + (pr == 0 ? "1 and 2" : "3 and 4") + (proc.blendLocked (pr) ? " blend is locked." : " blend is free again."));
            repaint();
            return;
        }
    for (int i = 0; i < 3; ++i)
        if (harmChip (i).contains (p))
        {
            setInt ("harmony", (float) i);
            const char* says[3] = { "As is: each sound plays as it was recorded.", "One key: all sounds with a key are moved into one key.",
                                    "Chord: the keynotes of the sounds go to the notes of one chord." };
            say (says[i]);
            repaint();
            return;
        }
    if (kKeyBtn.contains (p))
    {
        setInt ("key", (float) (((intOf ("key") + (e.mods.isShiftDown() ? -1 : 1)) % 12 + 12) % 12));
        repaint();
        return;
    }
    {   // the three arcs: press on one and drag up or down
        const float d = p.getDistanceFrom (kArcs);
        for (int i = 0; i < 3; ++i)
            if (std::abs (d - kArcR[i]) <= 3.0f)
            {
                arcDrag = i;
                arcFrom = proc.apvts.getRawParameterValue (kArcIds[i])->load();
                arcY = p.y;
                if (auto* pa = proc.apvts.getParameter (kArcIds[i])) pa->beginChangeGesture();
                say (kArcNames[i]);
                repaint();
                return;
            }
        if (d < 11.0f && e.getNumberOfClicks() >= 2)
        {   // double-click in the middle: all three back to "as Distill made it"
            for (auto* id : kArcIds) if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 (0.5f));
            say ("Motion, Weight and Light back to the middle.");
            repaint();
            return;
        }
    }
    for (int slot = 0; slot < 4; ++slot)
        if (cardWave (slot).contains (p) && proc.getSample (slot) != nullptr)
        {   // choose the part of the sound: press at one end, drag to the other
            const auto wv = cardWave (slot);
            partFrom = juce::jlimit (0.0f, 1.0f, (p.x - wv.getX()) / wv.getWidth());
            partDrag = slot; partOnBench = false; partMoved = false;
            selectedSlot = slot;
            recAll = false; proc.makeMask = 1 << slot;
            setBenchOpen (slot / 2);
            return;
        }
    for (int slot = 0; slot < 4; ++slot)
        if (proc.getSample (slot) != nullptr && keyLabel (slot).expanded (4.0f).contains (p))
        {   // the key of this sound, when the analysis got it wrong
            juce::PopupMenu m;
            const int cur = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("skey", slot))->load());
            juce::String found = "Found by Nektar";
            if (const auto smp = proc.getSample (slot); smp != nullptr && smp->keyKnown)
                found << ": " << (smp->tonality.tonal ? juce::String (Tonality::noteName (smp->tonality.tonic)) + (smp->tonality.minor ? " minor" : " major") : juce::String ("no key"));
            m.addItem (1, found, true, cur == 0);
            m.addItem (2, "No key (leave as it is)", true, cur == 1);
            m.addSeparator();
            for (int k = 0; k < 24; ++k)
                m.addItem (3 + k, juce::String (Tonality::noteName (k / 2)) + (k % 2 ? " minor" : " major"), true, cur == 2 + k);
            m.addSeparator();
            {   // tempo: as found, none, half, double
                const int tc = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("stempo", slot))->load());
                juce::String tf = "Tempo found by Nektar";
                if (const auto smp = proc.getSample (slot); smp != nullptr && smp->tempoKnown)
                    tf << ": " << (smp->tempo.bpm > 0.0f ? juce::String (juce::roundToInt (smp->tempo.bpm)) + " bpm" : juce::String ("no tempo"));
                m.addItem (101, tf, true, tc == 0);
                m.addItem (102, "No tempo (not locked)", true, tc == 1);
                m.addItem (103, "Half of it", true, tc == 2);
                m.addItem (104, "Double", true, tc == 3);
            }
            juce::Component::SafePointer<NektarEditor> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, slot] (int r)
            {
                if (safe == nullptr || r <= 0) return;
                if (r > 100)
                {
                    if (auto* pt = safe->proc.apvts.getParameter (safe->slotId ("stempo", slot))) pt->setValueNotifyingHost (pt->convertTo0to1 ((float) (r - 101)));
                    safe->repaint();
                    return;
                }
                if (auto* pk = safe->proc.apvts.getParameter (safe->slotId ("skey", slot))) pk->setValueNotifyingHost (pk->convertTo0to1 ((float) (r - 1)));
                safe->repaint();
            });
            return;
        }
    if (const int sl = slotAt (p); sl >= 0)
    {   // a card: choose this sound; once more on the chosen one: all four
        if (sl == selectedSlot && ! recAll) { recAll = true; proc.makeMask = 15; }
        else { selectedSlot = sl; recAll = false; proc.makeMask = 1 << sl; }
        say (recAll ? "All four sounds: Rec and Resample make a new sound in the library."
                    : "Sound " + juce::String (sl + 1) + ": Rec and Resample go into it.");
        repaint();
    }
}

void NektarEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position / ((float) getWidth() / kDW);
    if (voiceDrag > 0) { voiceSet (p); return; }
    if (chkDrag >= 2000 && charPanel >= 0)
    {   // a knob of the bench: up = more
        const int snd = panelSound(), idx = chkDrag - 2000;
        const float v = juce::jlimit (0.0f, 1.0f, chkFrom + (chkY - p.y) / 160.0f);
        benchSet (idx, snd, v);
        say (juce::String (kBench[idx].name) + " of sound " + juce::String (snd + 1) + ": " + juce::String (juce::roundToInt (benchGet (idx, snd) * 100.0f)));
        repaint();
        return;
    }
    if (arcDrag >= 0)
    {
        if (auto* pa = proc.apvts.getParameter (kArcIds[arcDrag]))
            pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jlimit (0.0f, 1.0f, arcFrom + (arcY - p.y) / 160.0f)));
        repaint();
        return;
    }
    if (partDrag < 0) return;
    const auto wv = partOnBench ? benchWave() : cardWave (partDrag);
    const float to = juce::jlimit (0.0f, 1.0f, (p.x - wv.getX()) / wv.getWidth());
    if (std::abs (to - partFrom) < 0.01f && ! partMoved) return;
    partMoved = true;
    if (auto* pa = proc.apvts.getParameter (slotId ("grstart", partDrag))) pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jmin (partFrom, to)));
    if (auto* pa = proc.apvts.getParameter (slotId ("grend", partDrag))) pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jmax (partFrom, to)));
    repaint();
}

void NektarEditor::mouseUp (const juce::MouseEvent&)
{
    if (arcDrag >= 0 && arcDrag < 3) if (auto* pa = proc.apvts.getParameter (kArcIds[arcDrag])) pa->endChangeGesture();
    if (partDrag >= 0 && partOnBench && ! partMoved)
    {   // a click on the bench's wave: the point to read from, inside the part in use
        const float a = proc.apvts.getRawParameterValue (slotId ("grstart", partDrag))->load(), b = proc.apvts.getRawParameterValue (slotId ("grend", partDrag))->load();
        const float lo = juce::jmin (a, b), hi = juce::jmax (a, b);
        if (auto* pa = proc.apvts.getParameter (slotId ("grpos", partDrag)))
            pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jlimit (0.0f, 1.0f, (partFrom - lo) / juce::jmax (0.02f, hi - lo))));
        if (juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", partDrag))->load()) == 0) benchSet (0, partDrag, proc.apvts.getRawParameterValue (slotId ("grscan", partDrag))->load());
        repaint();
    }
    arcDrag = -1;
    partDrag = -1;
    partOnBench = false;
    chkDrag = -1;
    voiceDrag = 0;
}

void NektarEditor::mouseMove (const juce::MouseEvent& e)
{   // the main screen has no words on its symbols: their names come up in the message line
    const auto p = e.position / ((float) getWidth() / kDW);
    juce::String name;
    for (int pr = 0; pr < 2 && name.isEmpty(); ++pr)
    {
        if (charBtn (pr).contains (p)) name = "Bench: shape the chosen sound - material, timbre, rhythm, motion";
        if (name.isEmpty() && p.getDistanceFrom (shapeAt (pr)) <= 15.0f) name = "Movement of the joystick (click: next shape, wheel: speed)";
    }
    {
        const float d = p.getDistanceFrom (kArcs);
        for (int i = 0; i < 3 && name.isEmpty(); ++i) if (std::abs (d - kArcR[i]) <= 3.0f) name = kArcNames[i];
    }
    const char* harmNames[3] = { "As is", "One key", "Chord" };
    for (int i = 0; i < 3 && name.isEmpty(); ++i) if (harmChip (i).contains (p)) name = harmNames[i];
    if (name.isEmpty() && p.getDistanceFrom (kPresetAt) <= 15.0f) name = "Presets";
    if (name.isEmpty() && p.getDistanceFrom (kSaveAt) <= 15.0f) name = "Save the chosen sound to the library";
    if (name.isEmpty() && kVoicePad.contains (p)) name = "Second voice: pair 2 sings with pair 1. Across: unison, octave, fifth, third, seventh, ninth, cluster. Up: near, down: far";
    if (name.isEmpty() && kVoiceBar.contains (p)) name = "Second voice: how strong (left end: off)";
    if (name.isEmpty() && kKeyBtn.contains (p)) name = "Key (click: next, Shift-click: back)";
    if (name.isEmpty() && p.getDistanceFrom (kRecAt) <= 13.0f) name = recAll ? "Rec: the microphone into a new sound in the library" : "Rec: the microphone into sound " + juce::String (selectedSlot + 1);
    if (name.isEmpty() && p.getDistanceFrom (kResampleAt) <= 13.0f) name = recAll ? "Resample: the whole Nectar into a new sound in the library" : "Resample: sound " + juce::String (selectedSlot + 1) + " with all its changes, into itself";
    for (int pr = 0; pr < 2 && name.isEmpty(); ++pr) if (p.getDistanceFrom (keepAt (pr)) <= 15.0f) name = "Keep: save the sounds as a preset";
    if (name.isEmpty() && kMkOval.contains (p)) name = "Which sounds: 1, 1+2, 1-3, 1-4 (press again for the next)";
    if (name.isEmpty() && kBackBtn.contains (p)) name = "Back: the sound before the last Distill";
    {
        int hr = -1;
        for (int i = 0; i < 3; ++i)
        {
            if (std::abs (p.getDistanceFrom (kArcs) - kArcR[i]) <= 3.0f) hr = i;
        }
        if (hr != hoverRing) { hoverRing = hr; repaint(); }
    }
    if (name != hoverSaid)
    {
        hoverSaid = name;
        if (name.isNotEmpty()) { say (name); repaint(); }
    }
}

// ---- the effects at the end (VST3)
void NektarEditor::openFxWindow (int i)
{
    auto* inst = proc.fxRack.instance (i);
    if (inst == nullptr) return;
    auto& w = fxWindows[(size_t) i];
    if (w == nullptr || w->getContentComponent() == nullptr)
    {
        w = std::make_unique<FxWindow> (inst->getName());
        if (auto* ed = inst->createEditorIfNeeded()) w->setContentOwned (ed, true);
        else { auto* g = new juce::GenericAudioProcessorEditor (*inst); g->setSize (420, 360); w->setContentOwned (g, true); }
        w->setUsingNativeTitleBar (true);
        w->setResizable (false, false);
        w->centreWithSize (w->getWidth(), w->getHeight());
    }
    w->setVisible (true);
    w->toFront (true);
}

void NektarEditor::fxMenu (int i)
{
    juce::PopupMenu m;
    const bool has = proc.fxRack.instance (i) != nullptr;
    const auto list = proc.fxRack.effects();
    juce::PopupMenu choose;
    {
        juce::String maker;
        juce::PopupMenu sub;
        for (int k = 0; k < list.size(); ++k)
        {
            const auto mk = list[k].manufacturerName.isEmpty() ? juce::String ("Other") : list[k].manufacturerName;
            if (mk != maker && maker.isNotEmpty()) { choose.addSubMenu (maker, sub); sub = {}; }
            maker = mk;
            sub.addItem (1000 + k, list[k].name);
        }
        if (maker.isNotEmpty()) choose.addSubMenu (maker, sub);
    }
    if (has)
    {
        m.addItem (1, "Open its window");
        m.addItem (2, "On", true, proc.fxRack.isOn (i));
        m.addSubMenu ("Replace with", choose, list.size() > 0);
        m.addItem (3, "Remove");
    }
    else if (list.size() > 0)
    {
        m.addSectionHeader ("Effect " + juce::String (i + 1) + " (on the whole sound, at the end)");
        for (juce::PopupMenu::MenuItemIterator it (choose); it.next();) m.addItem (it.getItem());
    }
    else
        m.addItem (-1, proc.fxRack.scanning() ? "Looking for effects..." : "No effects found yet", false);
    m.addSeparator();
    m.addItem (4, proc.fxRack.scanning() ? "Looking for effects..." : "Look for effects (VST3)", ! proc.fxRack.scanning());
    juce::Component::SafePointer<NektarEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal ((fxSlot (i) * ((float) getWidth() / kDW)).toNearestInt())),
        [safe, i, list] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& ed = *safe;
            if (r == 1) ed.openFxWindow (i);
            else if (r == 2) ed.proc.fxRack.setOn (i, ! ed.proc.fxRack.isOn (i));
            else if (r == 3) { ed.fxWindows[(size_t) i] = nullptr; ed.proc.fxRack.load (i, nullptr); ed.say ("Effect " + juce::String (i + 1) + " removed."); }
            else if (r == 4)
            {
                ed.say ("Looking for VST3 effects on this computer...");
                ed.proc.fxRack.scan ([safe]
                {
                    if (safe != nullptr) { safe->say (juce::String (safe->proc.fxRack.effects().size()) + " effects found. Click + FX to choose one."); safe->repaint(); }
                });
            }
            else if (r >= 1000 && r - 1000 < list.size())
            {
                ed.fxWindows[(size_t) i] = nullptr;
                const auto d = list[r - 1000];
                const auto err = ed.proc.fxRack.load (i, &d);
                ed.say (err.isNotEmpty() ? err : d.name + " is on the whole sound. Click it to open its window.");
                if (err.isEmpty()) ed.openFxWindow (i);
            }
            ed.repaint();
        });
}
