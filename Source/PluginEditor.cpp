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
constexpr float kCx = 500.0f, kCy = 410.0f, kRingR = 255.0f, kScreenR = 134.0f, kSlotR = 135.0f; // the ring and the slot discs are designed at this size...
constexpr float kRingScale = 140.0f / 255.0f;    // ...and drawn smaller: library radius 140
constexpr float kSlotScale = 84.0f / 135.0f;     // slot radius 84
constexpr float kDotR = 32.0f;                   // v2: radius of the small slot circles
constexpr float kLibX = 520.0f, kLibY = 250.0f;
constexpr float kSlotCx[4] = { 138.0f, 222.0f, 818.0f, 902.0f };
constexpr float kSlotCy[4] = { 350.0f, 350.0f, 350.0f, 350.0f };
constexpr float kPadCx[2] = { 180.0f, 860.0f };
constexpr float kPadCy = 214.0f;
constexpr float kCharR = 84.0f; // the characters round each joystick
constexpr float kModR = 56.0f;                   // module radius
const juce::Rectangle<float> kScreen (24.0f, 450.0f, 992.0f, 62.0f);
const juce::Rectangle<float> kModeBtn[3] = { { 150.0f, 414.0f, 54.0f, 26.0f }, { 208.0f, 414.0f, 78.0f, 26.0f }, { 290.0f, 414.0f, 84.0f, 26.0f } };
const juce::Rectangle<float> kScaleBtn[2] = { { 386.0f, 414.0f, 170.0f, 26.0f }, { 562.0f, 414.0f, 74.0f, 26.0f } };
const juce::Rectangle<float> kFaceBtn (644.0f, 412.0f, 94.0f, 28.0f);      // details view: back to the joysticks
const juce::Rectangle<float> kDetailsBtn (892.0f, 498.0f, 120.0f, 32.0f);  // main screen: open the panel with all the knobs
// ---- the main screen (design units): four panels
const juce::Rectangle<float> kPanelL (10.0f, 68.0f, 340.0f, 392.0f), kPanelC (358.0f, 68.0f, 324.0f, 392.0f),
                             kPanelR (690.0f, 68.0f, 340.0f, 392.0f), kPanelB (10.0f, 468.0f, 1020.0f, 190.0f);
// centre: the library categories, Random sounds and the relation of the pairs; the Space circle with its oval on the rim
inline juce::Rectangle<float> libChip (int i) { return { 376.0f + (float) i * 42.0f, 80.0f, 38.0f, 22.0f }; }
const juce::Rectangle<float> kRandBtn (440.0f, 404.0f, 130.0f, 28.0f);
const juce::Point<float> kRelBtn (592.0f, 418.0f);
const juce::Rectangle<float> kSpaceOval (472.0f, 305.0f, 96.0f, 30.0f);      // sits on the bottom rim of the Space circle
// bottom: the river of the four sounds; the three arcs; Keep, Make, Back; Harmony and Key; the message
const juce::Rectangle<float> kRiver (26.0f, 480.0f, 988.0f, 92.0f);
const juce::Point<float> kArcs (66.0f, 615.0f);
constexpr float kArcR[3] = { 16.0f, 22.0f, 28.0f };  // Motion (inside), Weight, Light (outside)
const juce::Rectangle<float> kMakeBtn (430.0f, 590.0f, 180.0f, 44.0f), kKeepBtn (360.0f, 596.0f, 56.0f, 32.0f), kBackBtn (624.0f, 596.0f, 56.0f, 32.0f);
inline juce::Rectangle<float> harmChip (int i) { return { 742.0f + (float) i * 56.0f, 596.0f, 50.0f, 32.0f }; }
const juce::Rectangle<float> kKeyBtn (912.0f, 596.0f, 96.0f, 32.0f);
const juce::Rectangle<float> kMsgLine (300.0f, 638.0f, 440.0f, 16.0f);
// a pair panel: the movement and Gentle - Wild on top, the joystick with the 13 characters round it, the two sound cards
inline juce::Point<float> charAt (int pair, int i)
{
    const float a = juce::MathConstants<float>::pi * (-0.5f + 2.0f * (float) i / 13.0f);
    return { kPadCx[pair] + kCharR * std::cos (a), kPadCy + kCharR * std::sin (a) };
}
inline juce::Point<float> shapeAt (int pair) { return { pair == 0 ? 34.0f : 714.0f, 92.0f }; }
inline juce::Rectangle<float> speedDots (int pair) { const auto c = shapeAt (pair); return { c.x + 20.0f, c.y - 6.0f, 40.0f, 12.0f }; }
inline juce::Point<float> wildAt (int pair)  { return { pair == 0 ? 326.0f : 1006.0f, 92.0f }; }
inline juce::Rectangle<float> cardRect (int slot) { return { (slot < 2 ? 20.0f : 700.0f) + (float) (slot % 2) * 166.0f, 322.0f, 154.0f, 128.0f }; }
inline juce::Rectangle<float> cardWave (int slot) { const auto r = cardRect (slot); return { r.getX() + 8.0f, r.getY() + 24.0f, r.getWidth() - 16.0f, 44.0f }; }
inline juce::Point<float> lockAt (int slot) { const auto r = cardRect (slot); return { r.getRight() - 15.0f, r.getBottom() - 15.0f }; }
inline juce::Point<float> soloAt (int slot) { const auto r = cardRect (slot); return { r.getRight() - 38.0f, r.getBottom() - 15.0f }; }
const juce::Rectangle<float> kRecOval (118.0f, 593.0f, 160.0f, 44.0f);  // Rec, where it goes, Resample
const juce::Point<float> kRecAt (kRecOval.getX() + 23.0f, kRecOval.getCentreY()), kResampleAt (kRecOval.getRight() - 23.0f, kRecOval.getCentreY());
inline juce::Point<float> blendAt (int pair) { return { cardRect (pair * 2).getRight() + 6.0f, cardRect (pair * 2).getY() + 46.0f }; }
inline juce::Rectangle<float> keyLabel (int slot) { const auto r = cardRect (slot); return { r.getX() + 10.0f, r.getY() + 92.0f, r.getWidth() - 20.0f, 14.0f }; }
constexpr float kBadgeR = 10.0f;
constexpr float kColW = (992.0f - 48.0f) / 5.0f;
inline float colX (int c) { return 24.0f + (float) c * (kColW + 12.0f); }
const char* const kSlotIds[] = { "level", "pitch", "tail", "grmode", "grstart", "grend", "grpos", "grscan", "grspray", "grsize", "grdens", "grjit", "grrev", "grspread", "grgate", "grpat", "keeplength", "stretch", "blur", "freeze", "bodytone", "bodyfocus", "bodysize", "srcrough", "model", "modelnoise", "sfiltype", "sfilcut", "sfilres", "sfilmix", "air", "fshift", "ring", "rev", "noise", "hum", "lowcut", "pan", "loop" };
const juce::Rectangle<float> kAllBtn (744.0f, 412.0f, 62.0f, 28.0f), kMasterBtn (920.0f, 412.0f, 96.0f, 28.0f), kMotionBtn (814.0f, 412.0f, 98.0f, 28.0f);

// The nine modules of the bottom panel. A knob without a parameter id is not built yet (shown dim).
struct ModuleDef { const char* title; const char* sub; juce::uint32 colour; const char* knob[4]; const char* id[4]; };
const ModuleDef kModules[] = {
    { "TIME",      "length \xc2\xb7 freeze",       0xfffdc57e, { "Keep length", "Stretch", "Loop", "Freeze" },  { "keeplength", "stretch", "loop", "freeze" } },
    { "BODY",      "tone \xc2\xb7 morph",          0xffb5b1d8, { "Tone", "Focus", "Size", "Morph" },            { "bodytone", "bodyfocus", "bodysize", "morph" } },
    { "SOURCE",    "model \xc2\xb7 grit \xc2\xb7 spread", 0xff78cdd0, { "Model", "Grit", "Spread", "Noise" }, { "model", "srcrough", "blur", "modelnoise" } },
    { "RESONATOR", "follows key",                     0xffc1c494, { "Tune", "Decay", "Material", "Mix" },          { "restune", "resdecay", "resmat", "resmix" } },
    { "FILTER",    "ladder \xc2\xb7 svf \xc2\xb7 comb \xc2\xb7 formant", 0xffe07a5f, { "Type", "Cutoff", "Reso", "Mix" }, { "filtype", "filcut", "filres", "filmix" } },
    { "REVERB",    "shimmer \xc2\xb7 tuned",       0xff8fb8de, { "Note", "Decay", "Shimmer", "Mix" },           { "revsize", "revdecay", "revshimmer", "revmix" } },
    { "DELAY",     "pitch per repeat",               0xfff2cc8f, { "Time", "Feedback", "Pitch", "Mix" },          { "deltime", "delfb", "delpitch", "delmix" } },
    { "LFO",       "1 \xc2\xb7 2",                 0xfff4a6c0, { "Rate 1", "Shape 1", "Rate 2", "Shape 2" },    { "lfo1rate", "lfo1shape", "lfo2rate", "lfo2shape" } },
    { "SLICER",    "rhythmic gate",                  0xff9bc89b, { "Rate", "Pattern", "Smooth", "Depth" },        { "slrate", "slpattern", "slsmooth", "sldepth" } },
    { "CRUSH",     "bits \xc2\xb7 rate",              0xffd9a066, { "Bits", "Rate", "Dither", "Mix" },             { "crbits", "crrate", "crdither", "crmix" } },
    { "ENV",       "ADSR from keys",                 0xffb08fd8, { "Attack", "Decay", "Sustain", "Release" },     { "envatk", "envdec", "envsus", "envrel" } },
    { "JOY",       "x \xc2\xb7 y \xc2\xb7 path",       0xff8fd8b0, { "Rec", "Play", "", "" },                        { "joyrec", "joyplay", nullptr, nullptr } },
    { "SPACE",     "around the head",                0xff7ec8e3, { "Amount", "Speakers", "Far", "Back" },         { "spmix", "spspk", "spfar", "spback" } },
};
constexpr int kNumModules = 13;
constexpr float kSpaceX = 520.0f, kSpaceY = 250.0f, kSpaceR = 70.0f; // the placement pad (top view of the listener)
constexpr int kJoyModule = 11;
// four pyramids of three: one circle on top, two below. {group, place} per module in the order of kModules
// (place 0 = top, 1 = bottom left, 2 = bottom right)
constexpr int kModPlace[13][2] = { { 0, 1 }, { 0, 2 }, { 0, 0 },   // Time, Body, Source
                                   { 1, 1 }, { 1, 0 }, { 2, 0 },   // Resonator, Filter, Reverb
                                   { 2, 1 }, { 3, 0 }, { 2, 2 },   // Delay, LFO, Slicer
                                   { 1, 2 }, { 3, 1 }, { 3, 2 },   // Crush, Env, Joy
                                   { 4, 0 } };                        // Space (centre, no disc: pad above, four knobs below)
// layout A: mirror symmetric about x = 680. Sound on the left (two pyramids), effects and controls on the right, Space in the middle.
juce::Point<float> moduleCentre (int m)
{
    static const float gx[4] = { 140.0f, 395.0f, 965.0f, 1220.0f };
    if (kModPlace[m][0] == 4)
        return { 680.0f, 590.0f };
    const float x = gx[kModPlace[m][0]];
    switch (kModPlace[m][1])
    {
        case 0:  return { x, 458.0f };
        case 1:  return { x - 57.0f, 560.0f };
        default: return { x + 57.0f, 560.0f };
    }
}
juce::Point<float> moduleKnobCentre (int m, int k)
{
    static const float dx[4] = { -21.0f, 21.0f, -21.0f, 21.0f }, dy[4] = { -28.0f, -28.0f, 17.0f, 17.0f };
    const auto c = moduleCentre (m);
    if (m == kJoyModule) // Rec and Play side by side below the pad
        return { c.x + (k == 0 ? -21.0f : 21.0f), c.y + 22.0f };
    if (kModPlace[m][0] == 4) // Space: the four knobs in a row below the pad
        return { c.x + ((float) k - 1.5f) * 44.0f, c.y };
    return { c.x + dx[k], c.y + dy[k] };
}
bool isToggleId (const juce::String& id) { return id == "loop" || id == "joyrec" || id == "joyplay" || id == "spspk"; }

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

void wave (Graphics& g, float cx, float cy, float w, float amp, int seed, float decay, int n = 40)
{
    juce::Random rnd (seed);
    g.setColour (kLight);
    for (int i = 0; i < n; ++i)
    {
        const float x = cx - w / 2 + w * (float) i / (float) (n - 1);
        const float h = juce::jmax (1.5f, amp * std::exp (-decay * (float) i / (float) n) * (0.35f + 0.65f * rnd.nextFloat()));
        g.drawLine (x, cy - h, x, cy + h, 2.0f);
    }
}

void realWave (Graphics& g, float cx, float cy, float w, float amp, const juce::Array<float>& peaks)
{
    const int n = peaks.size();
    g.setColour (kLight);
    for (int i = 0; i < n; ++i)
    {
        const float x = cx - w / 2 + w * (float) i / (float) juce::jmax (1, n - 1);
        const float h = juce::jmax (1.5f, amp * peaks[i]);
        g.drawLine (x, cy - h, x, cy + h, 2.0f);
    }
}

void insetBox (Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setColour (Colour (0xff1a1918));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.translated (0, 0.5f), corner, 1.0f);
}

void sphere (Graphics& g, float x, float y, float s, Colour c)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (x + s * 0.2f, y + s * 0.45f, s * 2.0f, s * 2.0f);
    ColourGradient grad (c.brighter (0.28f), x - s * 0.3f, y - s * 0.35f, c.darker (0.5f), x + s * 0.6f, y + s * 0.7f, true);
    grad.addColour (0.45, c);
    g.setGradientFill (grad);
    g.fillEllipse (x - s, y - s, 2 * s, 2 * s);
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

// Slot disc without the knobs (the knobs are live components on top).
void slotDisc (Graphics& g, const juce::Typeface::Ptr& reg, const juce::Typeface::Ptr& bold,
               float cx, float cy, int index, const juce::String& title, bool empty,
               const juce::String& emptyText, const juce::Array<float>* peaks, int seed, bool selected = false)
{
    shadowCircle (g, cx, cy, kSlotR, 0.5f, 30, { 14, 22 });
    if (selected)
    {
        g.setColour (kAcc.withAlpha (0.85f));
        g.drawEllipse (cx - kSlotR - 6, cy - kSlotR - 6, 2 * (kSlotR + 6), 2 * (kSlotR + 6), 1.8f);
    }
    g.setGradientFill (ColourGradient (Colour (0xff3d3b39), cx - kSlotR * 0.6f, cy - kSlotR * 0.7f,
                                       Colour (0xff222120), cx + kSlotR * 0.7f, cy + kSlotR, false));
    g.fillEllipse (cx - kSlotR, cy - kSlotR, 2 * kSlotR, 2 * kSlotR);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (cx - kSlotR + 0.75f, cy - kSlotR + 0.75f, 2 * kSlotR - 1.5f, 2 * kSlotR - 1.5f, 1.5f);

    g.setColour (kLight);
    g.drawEllipse (cx - 62 - 11, cy - 88 - 11, 22, 22, 1.2f);
    g.setFont (font (bold, 13));
    g.drawText (juce::String (index), juce::Rectangle<float> (cx - 73, cy - 99, 22, 22), juce::Justification::centred);
    g.setFont (font (reg, 16));
    g.drawText (title, juce::Rectangle<float> (cx - 44, cy - 99, 120, 22), juce::Justification::centredLeft);

    const juce::Rectangle<float> box (cx - 80, cy - 68, 160, 46);
    insetBox (g, box, 8);
    if (empty)
    {
        const float dash[] = { 4.0f, 4.0f };
        juce::Path outline;
        outline.addRoundedRectangle (box.reduced (1), 8.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dash, 2);
        g.setColour (kMuted.withAlpha (0.6f));
        g.fillPath (dashed);
        g.setColour (kLight);
        g.drawEllipse (box.getX() + 18, box.getCentreY() - 10, 20, 20, 1.5f);
        g.drawLine (box.getX() + 28, box.getCentreY() - 5, box.getX() + 28, box.getCentreY() + 5, 1.5f);
        g.drawLine (box.getX() + 23, box.getCentreY(), box.getX() + 33, box.getCentreY(), 1.5f);
        g.setFont (font (reg, 13));
        g.drawText (emptyText, juce::Rectangle<float> (box.getX() + 44, box.getCentreY() - 9, 90, 18), juce::Justification::centredLeft);
    }
    else if (peaks != nullptr && peaks->size() > 0)
        realWave (g, box.getCentreX(), box.getCentreY(), 140, 17.0f, *peaks);
    else
        wave (g, box.getCentreX(), box.getCentreY(), 140, 17.0f, seed, 1.4f);
}

// v2: small slot circle with its name underneath.
void slotDot (Graphics& g, const juce::Typeface::Ptr& reg, const juce::Typeface::Ptr& bold, float cx, float cy, int index,
              const juce::String& name, bool empty, const juce::Array<float>& peaks, bool selected)
{
    shadowCircle (g, cx, cy, kDotR, 0.5f, 18, { 7, 11 });
    g.setGradientFill (ColourGradient (Colour (0xff3d3b39), cx - kDotR * 0.6f, cy - kDotR * 0.7f,
                                       Colour (0xff222120), cx + kDotR * 0.7f, cy + kDotR, false));
    g.fillEllipse (cx - kDotR, cy - kDotR, 2 * kDotR, 2 * kDotR);
    g.setColour (selected ? kAcc : juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (cx - kDotR + 1.0f, cy - kDotR + 1.0f, 2 * kDotR - 2.0f, 2 * kDotR - 2.0f, selected ? 3.0f : 1.5f);
    g.setColour (selected ? kAcc : kLight);
    g.setFont (font (bold, 15));
    g.drawText (juce::String (index), juce::Rectangle<float> (cx - 20, cy - kDotR + 5, 40, 16), juce::Justification::centred);
    if (empty)
    {
        g.setColour (kMuted);
        g.drawLine (cx - 9, cy + 6, cx + 9, cy + 6, 1.8f);
        g.drawLine (cx, cy - 3, cx, cy + 15, 1.8f);
    }
    else if (peaks.size() > 0)
        realWave (g, cx, cy + 7, 40, 8.0f, peaks);
    g.setColour (selected ? kLight : kMuted);
    g.setFont (font (reg, 15));
    g.drawText (empty ? juce::String ("Empty") : name, juce::Rectangle<float> (cx - 40, cy + kDotR + 5, 80, 20), juce::Justification::centred, true);
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
    if (modProc != nullptr && parameter != nullptr && ! toggle)
    {
        const auto r = modProc->getModRoute (parameter->paramID);
        if (r.src > 0)
        {
            // ring around the knob: the dark track is the whole range, the bright part is how far the modulation reaches
            const float cx = kWidth / 2.0f, cy = radius, rr = radius + 5.0f;
            const float a0 = juce::degreesToRadians (-135.0f), span = juce::degreesToRadians (270.0f);
            juce::Path track, reach;
            track.addCentredArc (cx, cy, rr, rr, 0.0f, a0, a0 + span, true);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (track, juce::PathStrokeType (2.0f));
            const float from = a0 + span * p, to = a0 + span * juce::jlimit (0.0f, 1.0f, p + r.amount);
            reach.addCentredArc (cx, cy, rr, rr, 0.0f, juce::jmin (from, to), juce::jmax (from, to) + 0.001f, true);
            g.setColour (juce::Colour (0xffe8a23a));
            g.strokePath (reach, juce::PathStrokeType (2.6f));
        }
    }
}

void KnobComponent::mouseDown (const juce::MouseEvent& e)
{
    if (parameter == nullptr)
        return;
    if (e.mods.isPopupMenu())
    {
        if (modProc == nullptr || toggle)
            return;
        auto r = modProc->getModRoute (parameter->paramID);
        {
            // first click: choose the source
            juce::PopupMenu menu;
            menu.addSectionHeader ("Modulate with");
            menu.addItem (1, "None", true, r.src == 0);
            menu.addItem (2, "LFO 1", true, r.src == NektarProcessor::kModLfo1);
            menu.addItem (3, "LFO 2", true, r.src == NektarProcessor::kModLfo2);
            menu.addItem (7, "Envelope", true, r.src == NektarProcessor::kModEnv);
            menu.addItem (8, "Joystick X", true, r.src == NektarProcessor::kModJoyX);
            menu.addItem (9, "Joystick Y", true, r.src == NektarProcessor::kModJoyY);
            menu.addItem (10, "Velocity", true, r.src == NektarProcessor::kModVel);
            menu.addItem (11, "Key (C3 = centre)", true, r.src == NektarProcessor::kModKey);
            menu.addItem (12, "Ramp (time after the first key)", true, r.src == NektarProcessor::kModRamp);
            menu.addSeparator();
            menu.addItem (100, "Amount: hold Shift and drag the knob", false);
            juce::Component::SafePointer<KnobComponent> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options(), [safe, r] (int choice)
            {
                if (safe == nullptr || choice < 1 || choice > 12)
                    return;
                auto n = r;
                n.src = choice - 1;
                if (n.src > 0 && std::abs (n.amount) < 0.01f)
                    n.amount = 0.5f;
                safe->modProc->setModRoute (safe->parameter->paramID, n);
                safe->repaint();
            });
        }
        return;
    }
    if (e.mods.isShiftDown() && modProc != nullptr && ! toggle && modProc->getModRoute (parameter->paramID).src > 0)
    {
        rightDrag = true; // Shift + drag: how far the modulation reaches
        amountStart = modProc->getModRoute (parameter->paramID).amount;
        return;
    }
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
    if (rightDrag)
    {
        if (modProc != nullptr)
        {
            auto r = modProc->getModRoute (parameter->paramID);
            const float s = (float) getWidth() / kWidth;
            r.amount = juce::jlimit (-1.0f, 1.0f, amountStart - (float) e.getDistanceFromDragStartY() / (150.0f * s));
            modProc->setModRoute (parameter->paramID, r);
            repaint();
        }
        return;
    }
    if (e.mods.isPopupMenu())
        return;
    const float s = (float) getWidth() / kWidth;
    const float delta = -(float) e.getDistanceFromDragStartY() / (150.0f * s);
    parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStart + delta));
}

void KnobComponent::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || rightDrag)
    {
        rightDrag = false;
        return;
    }
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

// ------------------------------------------------------------------ live top bar
void RecordHud::paint (Graphics& g)
{
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / kDW));
    if (proc.recordingSlot() > -100) return; // v2: recording is shown on the cards and the river
    const bool rt = proc.isRecordingRoomTone();
    const bool rs = proc.isResampling();
    const bool rec = proc.isRecording() && ! rt && ! rs;
    const bool can = proc.canRecord();
    const float a = can ? 1.0f : 0.35f;

    // timer
    const double secs = (rec || rt || rs) ? proc.getRecordedSeconds() : 0.0;
    const int m = (int) (secs / 60.0);
    const double s = secs - 60.0 * m;
    g.setColour (kLight.withMultipliedAlpha ((rec || rt || rs) ? 1.0f : 0.6f));
    g.setFont (font (typeface, 19));
    g.drawText (juce::String::formatted ("%02d:%04.1f", m, s), juce::Rectangle<float> (100, 14, 90, 32), juce::Justification::centredLeft);

    // progress (how much of the 60 s limit is used)
    const float frac = (float) juce::jlimit (0.0, 1.0, secs / NektarProcessor::kMaxRecordSeconds);
    if (frac > 0.0f)
    {
        g.setColour (kLight);
        g.fillRoundedRectangle (196, 26, juce::jmax (9.0f, 110.0f * frac), 9, 4.5f);
    }

    // room tone button: the ring glows while the room is being recorded
    if (rt)
    {
        const float lv = juce::jlimit (0.0f, 1.0f, proc.getInputLevel() * 1.5f);
        g.setColour (kAcc.withAlpha (0.12f + 0.4f * lv));
        g.fillEllipse (447 - 27, 30 - 27, 54, 54);
        g.setColour (Colour (0xff1e1d1b));
        g.fillEllipse (447 - 21, 30 - 21, 42, 42);
        g.setColour (kAcc);
        g.drawEllipse (447 - 21, 30 - 21, 42, 42, 2.0f);
        for (int k = -2; k <= 2; ++k)
        {
            const float hh = 4.0f + (float) (3 - std::abs (k)) * 3.0f;
            g.drawLine (447 + (float) k * 4.5f, 30 - hh, 447 + (float) k * 4.5f, 30 + hh, 2.0f);
        }
    }

    // resample button: the ring glows and a stop square shows while the output is being recorded
    if (rs)
    {
        g.setColour (kAcc.withAlpha (0.35f));
        g.fillEllipse (497 - 27, 30 - 27, 54, 54);
        g.setColour (Colour (0xff1e1d1b));
        g.fillEllipse (497 - 21, 30 - 21, 42, 42);
        g.setColour (kAcc);
        g.drawEllipse (497 - 21, 30 - 21, 42, 42, 2.0f);
        g.fillRoundedRectangle (497 - 8, 30 - 8, 16, 16, 3.0f);
    }

    // record button: ring glows with the microphone level while recording
    const float cx = 398, cy = 30;
    if (rec)
    {
        const float lv = juce::jlimit (0.0f, 1.0f, proc.getInputLevel() * 1.5f);
        g.setColour (kAcc.withAlpha (0.12f + 0.4f * lv));
        g.fillEllipse (cx - 27, cy - 27, 54, 54);
    }
    g.setColour (Colour (0xff1e1d1b));
    g.fillEllipse (cx - 21, cy - 21, 42, 42);
    g.setColour (kAcc.withMultipliedAlpha (a));
    g.drawEllipse (cx - 21, cy - 21, 42, 42, 2.0f);
    if (rec)
        g.fillRoundedRectangle (cx - 8, cy - 8, 16, 16, 3.0f); // stop
    else
        g.fillEllipse (cx - 9, cy - 9, 18, 18);                 // record
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
        g.drawFittedText ("Make", getLocalBounds(), juce::Justification::centred, 1);
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
    {   // the four corners (four versions of the pair) on the rim
        const auto q = toPix ((i & 1) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f);
        g.setColour (tint.withAlpha (0.35f));
        g.fillEllipse (q.x - 4.0f, q.y - 4.0f, 8.0f, 8.0f);
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

// ------------------------------------------------------------------ JoyPad
void JoyPad::paint (Graphics& g)
{
    const float w = (float) getWidth(), c = w * 0.5f, r = c - 1.5f;
    const bool rec = proc.joyIsRecording();
    const bool armed = proc.joyIsArmed() && (juce::Time::getMillisecondCounter() / 350) % 2 == 0; // blinks while waiting for the trigger
    g.setColour (Colour (0xff1e1d1b));
    g.fillEllipse (c - r, c - r, 2 * r, 2 * r);
    g.setColour (Colour (0xff8fd8b0).withAlpha (rec ? 1.0f : 0.7f));
    g.drawEllipse (c - r, c - r, 2 * r, 2 * r, 1.4f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (c - r, c, c + r, c, 1.0f);
    g.drawLine (c, c - r, c, c + r, 1.0f);
    const float x = (proc.modSourceNow (NektarProcessor::kModJoyX) + 1.0f) * 0.5f;
    const float y = (proc.modSourceNow (NektarProcessor::kModJoyY) + 1.0f) * 0.5f;
    const float px = c + (x * 2.0f - 1.0f) * r * 0.86f, py = c - (y * 2.0f - 1.0f) * r * 0.86f;
    g.setColour (rec ? Colour (0xffe05a4f) : armed ? Colour (0xffe8a24a) : Colour (0xfffbe6a0));
    g.fillEllipse (px - 4.5f, py - 4.5f, 9.0f, 9.0f);
}

void JoyPad::setFromMouse (const juce::MouseEvent& e)
{
    const float w = (float) getWidth(), c = w * 0.5f, r = c * 0.86f;
    float dx = (e.position.x - c) / r, dy = -(e.position.y - c) / r;
    const float len = std::sqrt (dx * dx + dy * dy);
    if (len > 1.0f) { dx /= len; dy /= len; }
    if (auto* px = proc.apvts.getParameter ("joyx")) px->setValueNotifyingHost ((dx + 1.0f) * 0.5f);
    if (auto* py = proc.apvts.getParameter ("joyy")) py->setValueNotifyingHost ((dy + 1.0f) * 0.5f);
}

void JoyPad::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        proc.joyClearPath();
        return;
    }
    if (auto* px = proc.apvts.getParameter ("joyx")) px->beginChangeGesture();
    if (auto* py = proc.apvts.getParameter ("joyy")) py->beginChangeGesture();
    setFromMouse (e);
}
void JoyPad::mouseDrag (const juce::MouseEvent& e) { setFromMouse (e); }
void JoyPad::mouseUp (const juce::MouseEvent&)
{
    if (auto* px = proc.apvts.getParameter ("joyx")) px->endChangeGesture();
    if (auto* py = proc.apvts.getParameter ("joyy")) py->endChangeGesture();
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
            if (! detailsView) { rebuildPanel(); repaint(); }
        };
        addAndMakeVisible (*spacePad);
        spacePad->onSay = [this] (const juce::String& m) { say (m); repaint(); };
        spaceOval = std::make_unique<Overlay>();
        spaceOval->draw = [this] (juce::Graphics& g) { drawSpaceOval (g, spaceOval->getLocalBounds().toFloat()); };
        spaceOval->down = [this] (const juce::MouseEvent& e) { spaceOvalClick (e); };
        addAndMakeVisible (*spaceOval);
    }

    addKnob (845, 30, 13, "Out", false, 0.0f, "master"); // Master out, -24..+6 dB (default 0 dB)
    addKnob (740, 30, 13, "", false, 0.5f, "bright"); // Tilt (formerly Bright); replaces the unwired Out knob
    joyPad = std::make_unique<JoyPad> (proc);
    joyPadCentre = { colX (4) + 62.0f, 590.0f };
    addChildComponent (*joyPad);
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
    hud = std::make_unique<RecordHud> (proc, regular);
    addAndMakeVisible (*hud);
    startTimerHz (20);
    setSize ((int) kDW, (int) kDH);
    grabKeyboardFocus();
}

NektarEditor::~NektarEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    releaseAllKeys();
}

void NektarEditor::addKnob (float cx, float cy, float r, const juce::String& label, bool dim, float fixed,
                            const char* paramId, bool toggle)
{
    juce::RangedAudioParameter* param = paramId != nullptr ? proc.apvts.getParameter (paramId) : nullptr;
    auto* k = knobs.add (new KnobComponent (regular, r, label, dim, fixed, param, toggle));
    if (param != nullptr && ! toggle)
        k->setModulation (&proc);
    knobPos.add ({ cx, cy });
    addAndMakeVisible (k);
}

void NektarEditor::resized()
{
    const float s = (float) getWidth() / kDW;
    if (hud != nullptr) hud->setBounds (getLocalBounds());
    for (int i = 0; i < knobs.size(); ++i)
    {
        auto* k = knobs[i];
        const auto c = knobPos[i];
        const float r = (k->height() - 20.0f) / 2.0f;
        k->setBounds (juce::Rectangle<float> ((c.x - KnobComponent::kWidth / 2.0f) * s, (c.y - r) * s,
                                              KnobComponent::kWidth * s, k->height() * s).toNearestInt());
    }
    if (libMap != nullptr)
        libMap->setBounds (juce::Rectangle<float> ((kLibX - 140.0f) * s, (kLibY - 140.0f) * s, 280.0f * s, 280.0f * s).toNearestInt());
    if (spaceOval != nullptr)
    {
        spaceOval->setBounds (juce::Rectangle<float> (kSpaceOval.getX() * s, kSpaceOval.getY() * s, kSpaceOval.getWidth() * s, kSpaceOval.getHeight() * s).toNearestInt());
        spaceOval->setVisible (! detailsView);
    }
    if (spacePad != nullptr)
        spacePad->setBounds (juce::Rectangle<float> ((kSpaceX - kSpaceR) * s, (kSpaceY - kSpaceR) * s,
                                                     2 * kSpaceR * s, 2 * kSpaceR * s).toNearestInt());
    for (int i = 0; i < 2; ++i)
        if (pairPads[i] != nullptr)
            pairPads[i]->setBounds (juce::Rectangle<float> ((kPadCx[i] - PairPad::kHalf) * s, (kPadCy - PairPad::kHalf) * s,
                                                            2 * PairPad::kHalf * s, 2 * PairPad::kHalf * s).toNearestInt());
    if (joyPad != nullptr)
        joyPad->setBounds (juce::Rectangle<float> ((joyPadCentre.x - JoyPad::kR) * s, (joyPadCentre.y - JoyPad::kR) * s,
                                                   2 * JoyPad::kR * s, 2 * JoyPad::kR * s).toNearestInt());
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
    drawOverlay (g);
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
    if (! detailsView) { faceClick (e, p); return; }
    if (kFaceBtn.contains (p))
    {
        detailsView = false; masterView = false; motionView = false;
        cache = juce::Image();
        rebuildPanel();
        repaint();
        return;
    }
    const int slot = slotAt (p);
    if (slot >= 0)
    {
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            if (selection.count (slot) > 0 && selection.size() > 1 && ! masterView) selection.erase (slot);
            else selection.insert (slot);
            masterView = false; motionView = false;
            selectedSlot = slot;
            cache = juce::Image();
            rebuildPanel();
            repaint();
        }
        else
        {
            const bool changed = masterView || selection.size() != 1 || selection.count (slot) == 0;
            selection = { slot };
            masterView = false; motionView = false;
            if (changed) { selectedSlot = slot; cache = juce::Image(); rebuildPanel(); repaint(); }
        }
    }
    else if (! masterView && (kModeBtn[0].contains (p) || kModeBtn[1].contains (p) || kModeBtn[2].contains (p)))
    {
        const int mode = kModeBtn[0].contains (p) ? 0 : (kModeBtn[1].contains (p) ? 1 : 2);
        for (int sl : selection)
            if (auto* pa = proc.apvts.getParameter (slotId ("grmode", sl)))
                pa->setValueNotifyingHost (pa->convertTo0to1 ((float) mode));
        repaint();
    }
    else if (! masterView && screenArea().withTop (kScreen.getBottom() - 16.0f).contains (p)
             && juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", primarySlot()))->load()) == 2)
    {
        const int step = juce::jlimit (0, 15, (int) ((p.x - (screenArea().getX() + 16.0f)) / (screenArea().getWidth() - 32.0f) * 16.0f));
        const int pat = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grpat", primarySlot()))->load()) ^ (1 << step);
        for (int sl : selection)
            if (auto* pa = proc.apvts.getParameter (slotId ("grpat", sl)))
                pa->setValueNotifyingHost (pa->convertTo0to1 ((float) pat));
        repaint();
    }
    else if (masterView && ! motionView && (kScaleBtn[0].contains (p) || kScaleBtn[1].contains (p)))
    {
        const bool sc = kScaleBtn[0].contains (p);
        if (auto* pa = proc.apvts.getParameter (sc ? "scale" : "key"))
        {
            const int count = sc ? 8 : 12;
            const int cur = juce::roundToInt (proc.apvts.getRawParameterValue (sc ? "scale" : "key")->load());
            const int next = ((cur + (e.mods.isShiftDown() ? -1 : 1)) % count + count) % count;
            pa->setValueNotifyingHost (pa->convertTo0to1 ((float) next));
        }
        repaint();
    }
    else if (kAllBtn.contains (p))
    {
        selection = { 0, 1, 2, 3 };
        masterView = false; motionView = false;
        cache = juce::Image();
        rebuildPanel();
        repaint();
    }
    else if (kMotionBtn.contains (p))
    {
        motionView = ! motionView;
        masterView = motionView;
        cache = juce::Image();
        rebuildPanel();
        repaint();
    }
    else if (kMasterBtn.contains (p))
    {
        masterView = motionView ? true : ! masterView;
        motionView = false;
        cache = juce::Image();
        rebuildPanel();
        repaint();
    }
}

// the bar at the top works the same on both screens
bool NektarEditor::topBarClick (juce::Point<float> p)
{
    if (juce::Rectangle<float> (130, 15, 64, 30).contains (p)) showPresetMenu();
    else if (juce::Rectangle<float> (570, 11, 142, 38).contains (p)) say (proc.saveToLibrary (selectedSlot));
    else return false;
    return true;
}

void NektarEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{   // the wheel on a joystick's movement button: its speed
    const auto p = e.position / ((float) getWidth() / kDW);
    if (detailsView) return;
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

void NektarEditor::pressNew (int pair)
{
    if (proc.getSample (pair * 2) == nullptr && proc.getSample (pair * 2 + 1) == nullptr)
    {
        say (juce::String ("Load a sound into circle ") + (pair == 0 ? "1 or 2" : "3 or 4") + " first (double-click it).");
        return;
    }
    proc.newVector (pair);
    say ("New corners. Hold a key and move the joystick.");
}

void NektarEditor::pressBack (int pair)
{
    if (proc.backVector (pair)) say ("Back to the corners before.");
}

void NektarEditor::pressKeep()
{
    if (! proc.vectorOn (0) && ! proc.vectorOn (1)) return;
    say (proc.savePreset ("Joystick " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S")));
}

int NektarEditor::slotAt (juce::Point<float> p) const
{
    for (int i = 0; i < 4; ++i)
        if (detailsView ? p.getDistanceFrom ({ kSlotCx[i], kSlotCy[i] }) <= kDotR : cardRect (i).contains (p))
            return i;
    return -1;
}

void NektarEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.position / ((float) getWidth() / kDW));
    if (slot >= 0)
    {
        selection = { slot };
        masterView = false; motionView = false;
        selectedSlot = slot;
        cache = juce::Image();
        rebuildPanel();
        repaint();
        chooseFile (slot);
    }
}

void NektarEditor::selectSlot (int slot)
{
    if (slot == selectedSlot)
        return;
    selectedSlot = slot;
    cache = juce::Image();
    repaint();
}

void NektarEditor::say (const juce::String& text)
{
    message = text;
    cache = juce::Image();
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

void NektarEditor::toggleRoomTone()
{
    if (! proc.canRecord())
    {
        say ("Room tone works in the standalone app.");
        return;
    }
    if (proc.isRecordingRoomTone())
    {
        say (proc.stopRecording());
        return;
    }
    if (proc.isRecording())
        return; // a normal recording is running
    say ("Keep quiet: recording the room. Click again.");
    proc.startRecording (-1);
}

void NektarEditor::timerCallback()
{
    if (resampleArmTicks > 0 && --resampleArmTicks == 0)
    {
        resampleArmedSlot = -1;
        say ({});
    }
    if (proc.recordingFinishedByLimit())
        say (proc.stopRecording());
    if (proc.isRecording() || hudWasRecording)
        hud->repaint();
    hudWasRecording = proc.isRecording();
    if (joyPad != nullptr)
        joyPad->repaint();
    if (spacePad != nullptr)
        spacePad->repaint();
    if (libMap != nullptr && ! detailsView)
        libMap->repaint();
    if (! detailsView)
    {   // the main screen's movement: grains read, levels, the river, the joystick trails
        auto& row = river[(size_t) riverAt];
        for (int slot = 0; slot < 4; ++slot)
        {
            levelShown[(size_t) slot] += (proc.slotLevel[(size_t) slot].load() - levelShown[(size_t) slot]) * 0.35f;
            row[(size_t) slot] = levelShown[(size_t) slot];
            const int c = proc.tapCount[(size_t) slot].load();
            const int fresh = juce::jmin (8, c - sparkSeen[(size_t) slot]);
            for (int k = 0; k < fresh; ++k) // y holds the slot and the height within its card
                sparks.push_back ({ proc.tapPos[(size_t) slot][(size_t) ((c - 1 - k) & 15)].load(), (float) slot + 0.15f + 0.7f * juce::Random::getSystemRandom().nextFloat(), 1.0f });
            sparkSeen[(size_t) slot] = c;
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
    for (auto& pp : pairPads)
        if (pp != nullptr) pp->repaint();

    // a change on the first selected sample is copied to the other selected samples (only the knob that moved)
    juce::String key;
    const int prim = primarySlot();
    for (auto* base : kSlotIds)
    {
        auto* src = proc.apvts.getParameter (slotId (base, prim));
        if (src == nullptr) continue;
        const float v = src->getValue();
        key << juce::String (v, 4) << ";";
        auto it = lastPanel.find (base);
        if (it != lastPanel.end() && std::abs (it->second - v) > 1.0e-6f && ! masterView && detailsView)
        {
            const bool byHand = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
            if (byHand) proc.pinVector (slotId (base, prim), v); // a knob turned by hand is no longer moved by the pair joystick
            for (int sl : selection)
                if (sl != prim)
                    if (auto* dst = proc.apvts.getParameter (slotId (base, sl)))
                    {
                        dst->setValueNotifyingHost (v);
                        if (byHand) proc.pinVector (slotId (base, sl), v);
                    }
        }
        lastPanel[base] = v;
    }
    key << juce::roundToInt (proc.apvts.getRawParameterValue ("scale")->load()) << "k" << juce::roundToInt (proc.apvts.getRawParameterValue ("key")->load());
    key << (detailsView ? "D" : "F") << juce::roundToInt (proc.apvts.getRawParameterValue ("character")->load()) << (proc.vectorOn (0) ? "a" : "") << (proc.vectorOn (1) ? "b" : "")
        << proc.vectorHistory (0) << "h" << proc.vectorHistory (1) << message;
    key << prim << (motionView ? "o" : "") << (masterView ? "m" : "s") << (int) selection.size() << (proc.getSample (prim) != nullptr ? proc.getSample (prim)->serial : -1);
    if (detailsView && ! motionView && panelMix != mixMask())
    {
        cache = juce::Image();
        rebuildPanel();
    }
    if (detailsView && ! masterView && (panelHum != humFound (prim) || panelClean != noiseFound (prim) || panelGrains != (juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", prim))->load()) > 0)))
    {
        cache = juce::Image();
        rebuildPanel();
    }
    if (key != overlayKey)
    {
        overlayKey = key;
        repaint();
    }
}

int NektarEditor::mixMask() const
{
    const char* ids[2] = { "filmix", "resmix" };
    const int bits[2] = { 1, 2 };
    int m = 16; // Space is always on now (the sounds are always placed)
    for (int i = 0; i < 2; ++i)
        if (proc.apvts.getRawParameterValue (ids[i])->load() > 0.001f) m |= bits[i];
    return m;
}

juce::Rectangle<float> NektarEditor::screenArea() const
{
    return pairIndex() >= 0 ? kScreen.withTrimmedRight (342.0f) : kScreen;
}

void NektarEditor::rebuildPanel()
{
    while (knobs.size() > fixedKnobs)
    {
        knobs.removeLast();
        knobPos.removeLast();
    }
    lastPanel.clear();
    if (joyPad != nullptr) joyPad->setVisible (detailsView && motionView);
    if (! detailsView)
    {   // the main screen: how far apart the corners are, the three big knobs, and the speed of the chosen sound's movement
        for (int pr = 0; pr < 2; ++pr)
        {   // Gentle - Wild of each joystick, at the outer top of its panel
            const auto w = wildAt (pr);
            addKnob (w.x, w.y, 12.0f, "", false, 0.0f, pr == 0 ? "wild1" : "wild2");
        }
        spaceSpeedId = slotId ("spspeed", spaceSlot);
        addKnob (kSpaceOval.getRight() - 17.0f, kSpaceOval.getCentreY(), 10.0f, "", false, 0.0f, spaceSpeedId.toRawUTF8());
        resized();
        return;
    }
    const float y1 = 568.0f, y2 = 612.0f, r = 10.0f;
    auto row = [&] (int col, float y, std::initializer_list<std::pair<const char*, juce::String>> ks)
    {
        const float cx = colX (col) + kColW * 0.5f, sp = juce::jmin (54.0f, (kColW - 30.0f) / (float) juce::jmax (1, (int) ks.size()));
        int i = 0;
        for (auto& k : ks)
        {
            addKnob (cx + ((float) i - 0.5f * (float) (ks.size() - 1)) * sp, y, r, k.first, false, 0.0f, k.second.toRawUTF8(),
                     k.second == "spspk");
            ++i;
        }
    };
    if (motionView)
    {
        row (0, 590.0f, { { "Rate", "lfo1rate" }, { "Shape", "lfo1shape" } });
        row (1, 590.0f, { { "Rate", "lfo2rate" }, { "Shape", "lfo2shape" } });
        row (2, 590.0f, { { "Fade in", "lfodelay" }, { "Fade out", "lfofade" }, { "Spiral", "lfospiral" } });
        row (3, y1, { { "Attack", "envatk" }, { "Decay", "envdec" }, { "Sustain", "envsus" }, { "Release", "envrel" } });
        row (3, y2, { { "Ramp", "ramptime" } });
        addKnob (colX (4) + kColW - 34.0f, y1, r, "Record", false, 0.0f, "joyrec", true);
        addKnob (colX (4) + kColW - 34.0f, y2, r, "Play", false, 0.0f, "joyplay");
    }
    else if (masterView)
    {
        if (mixMask() & 16)
        {
            row (0, y1, { { "Speakers", "spspk" }, { "Width", "width" } });
            row (0, y2, { { "Far", "spfar" }, { "Back", "spback" } });
        }
        else
            row (0, 590.0f, { { "Amount", "spmix" }, { "Width", "width" } });
        // an effect whose Mix is at zero shows only its Mix knob: no knob on screen that cannot be heard
        panelMix = mixMask();
        if (panelMix & 1) row (1, 590.0f, { { "Type", "filtype" }, { "Cutoff", "filcut" }, { "Reso", "filres" }, { "Mix", "filmix" } });
        else              row (1, 590.0f, { { "Mix", "filmix" } });
        if (panelMix & 2)
        {
            row (2, 590.0f, { { "Tune", "restune" }, { "Tail", "resdecay" }, { "Material", "resmat" }, { "Mix", "resmix" } });
            row (3, 590.0f, { { "Tune B", "restuneb" }, { "Mix B", "resmixb" } });
        }
        else
            row (2, 590.0f, { { "Mix", "resmix" } });
    }
    else
    {
        const int s = primarySlot();
        panelMix = mixMask();
        if (panelMix & 16) // with Space open the pad places the sounds, so Pan would not be heard
            row (0, y1, { { "Level", slotId ("level", s) }, { "Pitch", slotId ("pitch", s) }, { "Tail", slotId ("tail", s) } });
        else
            row (0, y1, { { "Level", slotId ("level", s) }, { "Pitch", slotId ("pitch", s) }, { "Tail", slotId ("tail", s) }, { "Pan", slotId ("pan", s) } });
        const bool grains = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", s))->load()) > 0;
        panelGrains = grains;
        if (! grains)
            row (0, y2, { { "Stretch", slotId ("stretch", s) }, { "Freeze", slotId ("freeze", s) }, { "Keep", slotId ("keeplength", s) } });
        row (3, y1, { { "Tone", slotId ("bodytone", s) }, { "Focus", slotId ("bodyfocus", s) }, { "Size", slotId ("bodysize", s) }, { "Blur", slotId ("blur", s) } });
        panelHum = humFound (s);
        panelClean = noiseFound (s);
        {
            const juce::String air = slotId ("air", s), grit = slotId ("srcrough", s), cln = slotId ("noise", s), hum = slotId ("hum", s), lc = slotId ("lowcut", s);
            if (panelClean && panelHum) row (3, y2, { { "Air", air }, { "Grit", grit }, { "Clean", cln }, { "Hum", hum }, { "Low cut", lc } });
            else if (panelClean)        row (3, y2, { { "Air", air }, { "Grit", grit }, { "Clean", cln }, { "Low cut", lc } });
            else if (panelHum)          row (3, y2, { { "Air", air }, { "Grit", grit }, { "Hum", hum }, { "Low cut", lc } });
            else                        row (3, y2, { { "Air", air }, { "Grit", grit }, { "Low cut", lc } });
        }
        if (const int pi = pairIndex(); pi >= 0)
        {
            const char* ids[2][4] = { { "morph12", "imorph12", "xmorph12", "chromo12" }, { "morph34", "imorph34", "xmorph34", "chromo34" } };
            const float x0 = kScreen.getRight() - 330.0f + 92.0f, yk = kScreen.getY() + 21.0f;
            addKnob (x0, yk, r, "Morph", false, 0.0f, ids[pi][0]);
            addKnob (x0 + 52.0f, yk, r, "Inter", false, 0.0f, ids[pi][1]);
            addKnob (x0 + 104.0f, yk, r, "Cross", false, 0.0f, ids[pi][2]);
            addKnob (x0 + 156.0f, yk, r, "Chroma", false, 0.0f, ids[pi][3]);
            addKnob (x0 + 208.0f, yk, r, "Move", false, 0.0f, "chromomove");
        }
        row (4, y1, { { "Type", slotId ("sfiltype", s) }, { "Cutoff", slotId ("sfilcut", s) }, { "Reso", slotId ("sfilres", s) }, { "Mix", slotId ("sfilmix", s) } });
        row (4, y2, { { "Shift", slotId ("fshift", s) }, { "Ring", slotId ("ring", s) } });
        if (grains)
        {
            row (1, y1, { { "Start", slotId ("grstart", s) }, { "End", slotId ("grend", s) } });
            row (1, y2, { { "Position", slotId ("grpos", s) }, { "Scan", slotId ("grscan", s) } });
            row (2, y1, { { "Size", slotId ("grsize", s) }, { "Density", slotId ("grdens", s) }, { "Spray", slotId ("grspray", s) }, { "Jitter", slotId ("grjit", s) } });
            row (2, y2, { { "Reverse", slotId ("grrev", s) }, { "Spread", slotId ("grspread", s) }, { "Gate", slotId ("grgate", s) } });
        }
        else
        {
            row (1, y1, { { "Start", slotId ("grstart", s) }, { "End", slotId ("grend", s) } });
            addKnob (colX (1) + kColW * 0.5f + 34.0f, y2, r, "Loop", false, 0.0f, slotId ("loop", s).toRawUTF8(), true);
        }
        addKnob (grains ? colX (1) + kColW - 30.0f : colX (1) + kColW * 0.5f - 34.0f, grains ? y1 : y2, r, "Backwards", false, 0.0f, slotId ("rev", s).toRawUTF8(), true);
    }
    resized();
}

// Live parts of the panel (drawn in design units on top of the cached picture).
namespace
{
// symbols instead of words: the five Characters, the dice, and the first four Space movements
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
void diceSymbol (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col)
{
    g.setColour (col);
    g.drawRoundedRectangle (c.x - 0.8f * r, c.y - 0.8f * r, 1.6f * r, 1.6f * r, 0.3f * r, juce::jmax (1.2f, 0.12f * r));
    const float d = 0.42f * r, s = 0.24f * r;
    for (auto q : { juce::Point<float> (-d, -d), juce::Point<float> (d, d), juce::Point<float> (0, 0), juce::Point<float> (d, -d), juce::Point<float> (-d, d) })
        g.fillEllipse (c.x + q.x - s * 0.5f, c.y + q.y - s * 0.5f, s, s);
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

void NektarEditor::drawOverlay (Graphics& g)
{
    if (! detailsView) { drawFace (g); return; }
    if (masterView)
    {
        g.setColour (kMuted);
        g.setFont (font (regular, 16));
        if (motionView)
        {
            g.drawFittedText ("Right-click any knob (in Sample or Master) and choose what moves it: LFO 1, LFO 2, Envelope, Joystick X or Y, Velocity, Key, Ramp.\nThen hold Shift and drag that knob to set how far it moves.", kScreen.reduced (20.0f, 4.0f).toNearestInt(), juce::Justification::centred, 2);
            return;
        }
        g.drawText ("The four sounds together", kScreen, juce::Justification::centred);
        static const char* const keys[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int sc = juce::roundToInt (proc.apvts.getRawParameterValue ("scale")->load());
        const int key = juce::jlimit (0, 11, juce::roundToInt (proc.apvts.getRawParameterValue ("key")->load()));
        const juce::String scName = proc.apvts.getParameter ("scale")->getCurrentValueAsText();
        for (int i = 0; i < 2; ++i)
        {
            g.setColour (i == 0 && sc > 0 ? kWada[2] : Colour (0xff2f2d2b));
            g.fillRoundedRectangle (kScaleBtn[i], 13);
            g.setColour (i == 0 && sc > 0 ? kDark : kLight);
            g.setFont (font (regular, 14));
            g.drawText (i == 0 ? "Scale: " + scName : "Key: " + juce::String (keys[key]), kScaleBtn[i], juce::Justification::centred);
        }
        return;
    }
    if (! panelGrains)
    {
        g.setColour (kMuted);
        g.setFont (font (regular, 14));
        g.drawFittedText ("Choose Melodic or Rhythmic\nto cut the sound into grains", juce::Rectangle<float> (colX (2), 556.0f, kColW, 60.0f).toNearestInt(), juce::Justification::centred, 2);
    }
    const int prim = primarySlot();
    auto val = [&] (const char* base) { return proc.apvts.getRawParameterValue (slotId (base, prim))->load(); };
    const int mode = juce::roundToInt (val ("grmode"));
    const char* modeNames[3] = { "Off", "Melodic", "Rhythmic" };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == mode ? kWada[2] : Colour (0xff2f2d2b));
        g.fillRoundedRectangle (kModeBtn[i], 13);
        g.setColour (i == mode ? kDark : kLight);
        g.setFont (font (regular, 14));
        g.drawText (i == 0 ? "Grains off" : modeNames[i], kModeBtn[i], juce::Justification::centred);
    }
    const auto smp = proc.getSample (prim);
    const auto scr = screenArea();
    const auto inner = scr.reduced (16.0f, 4.0f);
    if (smp == nullptr)
    {
        g.setColour (kMuted);
        g.setFont (font (regular, 16));
        g.drawText ("Empty: double-click the circle or drop a WAV", scr, juce::Justification::centred);
        return;
    }
    const bool rhythmic = mode == 2;
    const auto wr = rhythmic ? inner.withTrimmedBottom (16.0f) : inner;
    const auto pk = peaksOf (smp.get(), 220);
    const float a = juce::jmin (val ("grstart"), val ("grend")), b = juce::jmax (val ("grstart"), val ("grend"));
    const float xa = wr.getX() + a * wr.getWidth(), xb = wr.getX() + b * wr.getWidth();
    for (int i = 0; i < pk.size(); ++i)
    {
        const float x = wr.getX() + wr.getWidth() * ((float) i + 0.5f) / (float) pk.size();
        const float h = juce::jmax (1.2f, wr.getHeight() * 0.46f * pk[i]);
        const bool in = x >= xa && x <= xb;
        g.setColour (in ? kLight : kMuted.withAlpha (0.35f));
        g.drawLine (x, wr.getCentreY() - h, x, wr.getCentreY() + h, 2.0f);
    }
    {
        g.setColour (kAcc.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (xa, wr.getY(), xb - xa, wr.getHeight()));
        g.setColour (kAcc);
        g.drawLine (xa, wr.getY(), xa, wr.getBottom(), 2.0f);
        g.drawLine (xb, wr.getY(), xb, wr.getBottom(), 2.0f);
        if (mode == 1)
        {
            float pos = val ("grpos"); pos -= std::floor (pos);
            const float xp = xa + pos * (xb - xa), sw = 0.5f * val ("grspray") * (xb - xa);
            g.setColour (kWada[2].withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (juce::jmax (xa, xp - sw), wr.getY(), juce::jmin (xb, xp + sw) - juce::jmax (xa, xp - sw), wr.getHeight()));
            g.setColour (kWada[2]);
            g.drawLine (xp, wr.getY(), xp, wr.getBottom(), 2.0f);
        }
    }
    if (rhythmic)
    {
        const int pat = juce::roundToInt (val ("grpat"));
        const float sw = inner.getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i)
        {
            const juce::Rectangle<float> r (inner.getX() + (float) i * sw + 2.0f, inner.getBottom() - 13.0f, sw - 4.0f, 11.0f);
            g.setColour (((pat >> i) & 1) != 0 ? kWada[i % 4 == 0 ? 0 : 2] : Colour (0xff2f2d2b));
            g.fillRoundedRectangle (r, 3.0f);
        }
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
    message = proc.loadSample (f, slot);
    if (auto* pk = proc.apvts.getParameter (slotId ("skey", slot))) pk->setValueNotifyingHost (0.0f); // a new sound: its key is found again
    if (auto* pt = proc.apvts.getParameter (slotId ("stempo", slot))) pt->setValueNotifyingHost (0.0f); // ...and its tempo
    cache = juce::Image();
    repaint();
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
    g.setColour (kMuted);
    g.drawRoundedRectangle (130, 15, 64, 30, 15, 1.5f);
    g.setColour (kLight);
    g.setFont (font (reg, 13));
    g.drawText ("Presets", juce::Rectangle<float> (130, 15, 64, 30), juce::Justification::centred);

    g.setColour (kAcc);
    g.fillRoundedRectangle (570, 11, 142, 38, 19);
    g.setColour (kDark);
    g.setFont (font (bld, 15));
    g.drawText ("Save to library", juce::Rectangle<float> (570, 11, 142, 38), juce::Justification::centred);

    g.setColour (kMuted);
    g.setFont (font (reg, 13));
    g.drawText ("Tilt", juce::Rectangle<float> (760, 18, 90, 24), juce::Justification::centredLeft);
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

    // ---- v2: four small slot circles (two each side of the ring); the main screen draws cards instead (drawFace)
    for (int slot = 0; slot < 4 && detailsView; ++slot)
    {
        const auto smp = proc.getSample (slot);
        const auto peaks = peaksOf (smp.get(), 30);
        slotDot (g, reg, bld, kSlotCx[slot], kSlotCy[slot], slot + 1, smp != nullptr ? smp->name : juce::String(),
                 smp == nullptr, peaks, selection.count (slot) > 0 && ! masterView && detailsView);
    }

    // ---- v2: one panel from edge to edge
    {
        if (! detailsView)
        {   // four plates, each raised on its ledge: the two pairs, the centre, the whole sound
            for (auto plate : { kPanelL, kPanelC, kPanelR, kPanelB })
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRoundedRectangle (plate.translated (0.0f, 5.0f), 22.0f);
                g.setGradientFill (ColourGradient (Colour (0xff6b6454).withAlpha (0.55f), 0, plate.getY(),
                                                   Colour (0xff4d4739).withAlpha (0.55f), 0, plate.getBottom(), false));
                g.fillRoundedRectangle (plate, 22.0f);
                g.setColour (juce::Colours::white.withAlpha (0.10f));
                g.drawLine (plate.getX() + 22.0f, plate.getY() + 1.0f, plate.getRight() - 22.0f, plate.getY() + 1.0f, 1.0f);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawRoundedRectangle (plate.reduced (0.5f), 22.0f, 1.2f);
            }
            return; // the face itself is drawn live (drawFace)
        }
        const juce::Rectangle<float> plate (8.0f, 426.0f, kDW - 16.0f, 230.0f);
        g.setGradientFill (ColourGradient (Colour (0xff6b6454).withAlpha (0.55f), 0, plate.getY(),
                                           Colour (0xff4d4739).withAlpha (0.55f), 0, plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 24.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (plate.reduced (0.5f), 24.0f, 1.2f);

        if (! detailsView)
            return; // the joystick face: its buttons are drawn live (drawOverlay)
        {
            g.setColour (Colour (0xff2f2d2b));
            g.fillRoundedRectangle (kFaceBtn, 14);
            g.setColour (kLight);
            g.setFont (font (reg, 16));
            g.drawText ("Joysticks", kFaceBtn, juce::Justification::centred);
        }
        juce::String title = motionView ? "Motion (what moves the knobs)" : "Master (all four together)";
        if (! masterView)
        {
            title = "Sample ";
            bool first = true;
            for (int i : selection) { title << (first ? "" : " + ") << juce::String (i + 1); first = false; }
        }
        g.setColour (kAcc);
        g.setFont (font (bld, 20));
        g.drawText (title, juce::Rectangle<float> (28, 412, masterView ? 360.0f : 120.0f, 30), juce::Justification::centredLeft);
        if (message.isNotEmpty())
        {
            g.setColour (kMuted);
            g.setFont (font (reg, 15));
            g.drawText (message, juce::Rectangle<float> (386, 412, 250, 30), juce::Justification::centredLeft);
        }
        const bool allOn = ! masterView && selection.size() == 4;
        for (int k = 0; k < 3; ++k)
        {
            const juce::Rectangle<float> r = k == 0 ? kAllBtn : (k == 1 ? kMasterBtn : kMotionBtn);
            const bool on = k == 0 ? allOn : (k == 1 ? masterView && ! motionView : motionView);
            g.setColour (on ? kAcc : Colour (0xff2f2d2b));
            g.fillRoundedRectangle (r, 14);
            g.setColour (on ? kDark : kLight);
            g.setFont (font (reg, 16));
            g.drawText (k == 0 ? "All" : (k == 1 ? "Master" : "Motion"), r, juce::Justification::centred);
        }

        insetBox (g, masterView ? kScreen : screenArea(), 10); // the sound itself is drawn live on top (drawOverlay)
        if (pairIndex() >= 0)
        {
            const juce::Rectangle<float> pr (kScreen.getRight() - 330.0f, kScreen.getY(), 330.0f, kScreen.getHeight());
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRoundedRectangle (pr, 10);
            g.setColour (kWada[1]);
            g.setFont (font (bld, 15));
            g.drawFittedText (pairIndex() == 0 ? "Pair\n1 + 2" : "Pair\n3 + 4", pr.withWidth (70.0f).toNearestInt(), juce::Justification::centred, 2);
        }
        else if (! masterView && selection.size() == 2 && message.isEmpty())
        {
            g.setColour (kMuted);
            g.setFont (font (reg, 15));
            g.drawText ("Pair tools work on 1 + 2 and on 3 + 4", juce::Rectangle<float> (386, 412, 250, 30), juce::Justification::centredLeft);
        }

        // columns (controls come next)
        const char* motionNames[5] = { "LFO 1", "LFO 2", "LFO fade \xc2\xb7 Spiral", "Envelope \xc2\xb7 Ramp", "Joystick" };
        const char* names[2][5] = { { "Level \xc2\xb7 Time", "Part of the sound", "Granular", "Colour \xc2\xb7 Clean", "Filter \xc2\xb7 Shift" },
                                    { "Space", "Filter", "Resonator", "Resonator B", "" } };
        const float cw = kColW;
        for (int c = 0; c < 5; ++c)
        {
            if (masterView && ! motionView && c == 4) continue; // four columns in the Master view
            const juce::Rectangle<float> col (24.0f + (float) c * (cw + 12.0f), 524.0f, cw, 120.0f);
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRoundedRectangle (col, 10);
            g.setColour (kWada[c % 5]);
            g.setFont (font (bld, 15));
            g.drawText (juce::String (juce::CharPointer_UTF8 (motionView ? motionNames[c] : names[masterView ? 1 : 0][c])), col.withHeight (26.0f).translated (0, 4), juce::Justification::centred);
        }
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
        // the 13 characters round the joystick, by family
        const int cur = juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "character1" : "character2")->load());
        for (int i = 0; i < 13; ++i)
        {
            const int ch = kCharOrder[i];
            const auto c = charAt (pr, i);
            const auto fc = familyColour (ch);
            g.setColour (ch == cur ? fc : Colour (0xff2a2927));
            g.fillEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f);
            g.setColour (fc.withAlpha (ch == cur ? 1.0f : 0.55f));
            g.drawEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f, 1.2f);
            charSymbol (g, ch, c, 6.5f, ch == cur ? kDark : kLight.withAlpha (0.85f));
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
            for (const auto& sp : sparks)
                if ((int) sp.y == slot)
                {   // the grains of this sound, as they are read
                    g.setColour (col.withAlpha (juce::jlimit (0.0f, 1.0f, sp.life)));
                    const float x = wv.getX() + (a0 + sp.x * (b0 - a0)) * wv.getWidth(), y = wv.getY() + (sp.y - (float) slot) * wv.getHeight();
                    const float rad = 1.2f + 1.6f * sp.life;
                    g.fillEllipse (x - rad, y - rad, 2 * rad, 2 * rad);
                }
            g.setColour (kLight);
            g.setFont (font (regular, 12));
            g.drawFittedText (smp->name, juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 74.0f, r.getWidth() - 20.0f, 16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            auto k = proc.keyText (slot);
            k = k.replace (" minor", "m").replace (" major", "");
            if (k == "no key") k = "-";
            auto t = proc.tempoText (slot);
            t = t.endsWith (" bpm") ? t.upToFirstOccurrenceOf (" ", false, false) : (t == "..." ? t : "-");
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

    // ================= the centre panel: categories, Random sounds, the relation
    {
        const auto cats = proc.libIndex.categories();
        const int pages = juce::jmax (1, (cats.size() + 5) / 6);
        chipPage = juce::jlimit (0, pages - 1, chipPage);
        for (int i = 0; i < 7; ++i)
        {
            juce::String name; bool on = false;
            if (i == 0) { name = "All"; on = proc.libCategory.isEmpty(); }
            else if (i == 6) { if (pages <= 1) break; name = "+"; }
            else { const int k = chipPage * 6 + i - 1; if (k >= cats.size()) break; name = cats[k]; on = proc.libCategory == name; }
            const auto r = libChip (i);
            g.setColour (on ? kAcc : Colour (0xff2f2d2b));
            g.fillRoundedRectangle (r, 11);
            g.setColour (on ? kDark : kLight);
            g.setFont (font (regular, 11));
            g.drawFittedText (name, r.reduced (3, 0).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }
    bool allLocked = true, anySound = false;
    for (int slot = 0; slot < 4; ++slot) { allLocked = allLocked && proc.slotLocked (slot); anySound = anySound || proc.getSample (slot) != nullptr; }
    button (kRandBtn, ! allLocked, false);
    g.setColour (allLocked ? kMuted.withAlpha (0.45f) : kLight);
    g.setFont (font (regular, 13));
    g.drawText ("Random sounds", kRandBtn, juce::Justification::centred);
    {
        const int rel = juce::roundToInt (proc.apvts.getRawParameterValue ("relation")->load());
        disc (kRelBtn, 13.0f, rel > 0, kAcc);
        relationSymbol (g, rel, kRelBtn, 8.5f, rel > 0 ? kDark : kLight);
    }

    // ================= the bottom panel: the river of the four sounds
    {
        const auto r = kRiver;
        g.setColour (Colour (0xff1c1b19));
        g.fillRoundedRectangle (r, 10.0f);
        const int n = (int) river.size();
        const float dx = (r.getWidth() - 20.0f) / (float) (n - 1), base = r.getBottom() - 10.0f, hMax = r.getHeight() - 20.0f;
        std::array<float, 300> below {};
        for (int slot = 3; slot >= 0; --slot)
        {   // the four sounds as bands, one on another, flowing to the left
            juce::Path band;
            for (int i = 0; i < n; ++i)
            {
                const auto& v = river[(size_t) ((riverAt + i) % n)];
                const float h = juce::jlimit (0.0f, hMax * 0.25f, std::sqrt (v[(size_t) slot]) * hMax * 0.55f);
                const float x = r.getX() + 10.0f + (float) i * dx, y = base - below[(size_t) i] - h;
                if (i == 0) band.startNewSubPath (x, y); else band.lineTo (x, y);
            }
            for (int i = n - 1; i >= 0; --i)
                band.lineTo (r.getX() + 10.0f + (float) i * dx, base - below[(size_t) i]);
            band.closeSubPath();
            g.setColour (kWada[slot % 5].withAlpha (0.85f));
            g.fillPath (band);
            for (int i = 0; i < n; ++i)
            {
                const auto& v = river[(size_t) ((riverAt + i) % n)];
                below[(size_t) i] += juce::jlimit (0.0f, hMax * 0.25f, std::sqrt (v[(size_t) slot]) * hMax * 0.55f);
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        for (int i = 0; i < n; ++i)
            if (riverBeat[(size_t) ((riverAt + i) % n)])
                g.fillRect (r.getX() + 10.0f + (float) i * dx, r.getY() + 8.0f, 1.0f, r.getHeight() - 16.0f);
    }

    {   // Rec and Resample in one oval; in the middle, where the take goes: one chosen sound, or a new sound in the library
        const bool rs = proc.isResampling();
        const bool busyRec = micRec || rs;
        g.setColour (Colour (0xff2a2927));
        g.fillRoundedRectangle (kRecOval, kRecOval.getHeight() * 0.5f);
        g.setColour (Colour (0xff7ec8e3).withAlpha (0.8f));
        g.drawRoundedRectangle (kRecOval.reduced (0.5f), kRecOval.getHeight() * 0.5f, 1.4f);
        const int tgt = busyRec ? proc.recordingSlot() : (recAll ? NektarProcessor::kToLibrary : selectedSlot);
        g.setColour (busyRec ? kLight : (tgt >= 0 ? kWada[tgt % 5] : kLight.withAlpha (0.8f)));
        g.setFont (font (regular, busyRec ? 12 : 13));
        const juce::String where = tgt >= 0 ? "-> " + juce::String (tgt + 1) : juce::String ("-> library");
        g.drawText (busyRec ? clock (proc.getRecordedSeconds()) : where, kRecOval.reduced (40.0f, 0.0f), juce::Justification::centred);
        {   // Rec
            const auto c = kRecAt;
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
        }
        {   // Resample
            const auto c = kResampleAt;
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
        }
    }

    // ================= Motion, Weight, Light: three arcs round one circle
    {
        const float a0 = juce::MathConstants<float>::pi * 0.75f, span = juce::MathConstants<float>::pi * 1.5f;
        g.setColour (Colour (0xff1c1b19));
        g.fillEllipse (kArcs.x - 11.0f, kArcs.y - 11.0f, 22.0f, 22.0f);
        const juce::Colour arcCol[3] = { kWada[1], kWada[2], kWada[3] };
        for (int i = 0; i < 3; ++i)
        {
            const float v = juce::jlimit (0.0f, 1.0f, proc.apvts.getRawParameterValue (kArcIds[i])->load());
            juce::Path track, fill;
            track.addCentredArc (kArcs.x, kArcs.y, kArcR[i], kArcR[i], 0.0f, a0 + juce::MathConstants<float>::halfPi, a0 + span + juce::MathConstants<float>::halfPi, true);
            fill.addCentredArc (kArcs.x, kArcs.y, kArcR[i], kArcR[i], 0.0f, a0 + juce::MathConstants<float>::halfPi, a0 + span * v + juce::MathConstants<float>::halfPi, true);
            g.setColour (Colour (0xff2a2927));
            g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (arcCol[i].withAlpha (arcDrag == i ? 1.0f : 0.85f));
            g.strokePath (fill, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // its icon at the start of the arc
            const float ia = a0 - 0.25f;
            arcIcon (g, i, { kArcs.x + std::cos (ia) * kArcR[i], kArcs.y + std::sin (ia) * kArcR[i] }, arcCol[i]);
        }
    }

    // ================= Keep, Make, Back
    const bool busy = proc.baking();
    button (kKeepBtn, proc.vectorOn (0) || proc.vectorOn (1), false);
    keepSymbol (g, kKeepBtn.getCentre().translated (0.0f, 2.0f), kLight);
    {   // Make breathes slowly while the corners are being prepared
        const float br = busy ? 0.55f + 0.45f * (0.5f + 0.5f * (float) std::sin (now * 2.4)) : 1.0f;
        g.setColour ((anySound ? kAcc : Colour (0xff2f2d2b)).withMultipliedAlpha (br));
        g.fillRoundedRectangle (kMakeBtn, kMakeBtn.getHeight() * 0.5f);
        g.setColour (anySound ? kDark : kMuted);
        g.setFont (font (bold, 21));
        g.drawText ("Make", kMakeBtn, juce::Justification::centred);
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

void NektarEditor::faceClick (const juce::MouseEvent& e, juce::Point<float> p)
{
    auto setInt = [this] (const juce::String& id, float v) { if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 (v)); };
    auto intOf = [this] (const juce::String& id) { return juce::roundToInt (proc.apvts.getRawParameterValue (id)->load()); };
    if (kRandBtn.contains (p)) { say (proc.randomSounds()); repaint(); return; }
    if (kMakeBtn.contains (p)) { say (proc.makeAll()); repaint(); return; }
    if (kBackBtn.contains (p)) { if (proc.backAll()) say ("Back to what was there before the last Make."); repaint(); return; }
    if (kKeepBtn.contains (p)) { pressKeep(); return; }
    for (int pr = 0; pr < 2; ++pr)
    {
        for (int i = 0; i < 13; ++i)
            if (p.getDistanceFrom (charAt (pr, i)) <= 12.0f)
            {
                const int ch = kCharOrder[i];
                setInt (pr == 0 ? "character1" : "character2", (float) ch);
                say (juce::String (kCharNames[ch]) + " for sounds " + (pr == 0 ? "1 and 2" : "3 and 4") + ". Press Make.");
                repaint();
                return;
            }
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
    if (p.getDistanceFrom (kRelBtn) <= 14.0f)
    {
        const int rel = (intOf ("relation") + (e.mods.isShiftDown() ? 4 : 1)) % 5;
        setInt ("relation", (float) rel);
        const char* says[5] = { "The two pairs go their own ways.", "Contrast: Make gives sounds 3 and 4 the opposite character of 1 and 2.",
                                "Shadow: joystick 2 follows joystick 1, half a second later, mirrored.", "Call and response: the two pairs take turns, in time.",
                                "Depth: sounds 1 and 2 near and in front, 3 and 4 far and behind." };
        say (says[rel]);
        repaint();
        return;
    }
    if (p.getDistanceFrom (kResampleAt) <= 16.0f) { toggleResample(); repaint(); return; }
    if (p.getDistanceFrom (kRecAt) <= 16.0f) { toggleRecording(); repaint(); return; }
    if (kRiver.contains (p)) { recAll = true; say ("All four sounds: Rec and Resample make a new sound in the library."); repaint(); return; }
    for (int slot = 0; slot < 4; ++slot)
    {
        if (p.getDistanceFrom (lockAt (slot)) <= 10.0f)
        {
            proc.setSlotLock (slot, ! proc.slotLocked (slot));
            say (proc.slotLocked (slot) ? "Sound " + juce::String (slot + 1) + " is locked: Make and Random sounds leave it as it is."
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
    for (int i = 0; i < 7; ++i)
        if (libChip (i).contains (p))
        {
            const auto cats = proc.libIndex.categories();
            if (i == 0) proc.libCategory = {};
            else if (i == 6) { ++chipPage; chipPage = cats.size() > 0 ? chipPage % juce::jmax (1, (cats.size() + 5) / 6) : 0; }
            else if (const int k = chipPage * 6 + i - 1; k < cats.size()) proc.libCategory = cats[k];
            proc.rescanLibrary();
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
        {   // double-click in the middle: all three back to "as Make made it"
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
            partDrag = slot;
            selectedSlot = slot;
            recAll = false;
            repaint();
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
        if (sl == selectedSlot && ! recAll) recAll = true;
        else { selectedSlot = sl; recAll = false; }
        say (recAll ? "All four sounds: Rec and Resample make a new sound in the library."
                    : "Sound " + juce::String (sl + 1) + ": Rec and Resample go into it.");
        repaint();
    }
}

void NektarEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position / ((float) getWidth() / kDW);
    if (arcDrag >= 0)
    {
        if (auto* pa = proc.apvts.getParameter (kArcIds[arcDrag]))
            pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jlimit (0.0f, 1.0f, arcFrom + (arcY - p.y) / 160.0f)));
        repaint();
        return;
    }
    if (partDrag < 0) return;
    const auto wv = cardWave (partDrag);
    const float to = juce::jlimit (0.0f, 1.0f, (p.x - wv.getX()) / wv.getWidth());
    if (std::abs (to - partFrom) < 0.01f) return;
    if (auto* pa = proc.apvts.getParameter (slotId ("grstart", partDrag))) pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jmin (partFrom, to)));
    if (auto* pa = proc.apvts.getParameter (slotId ("grend", partDrag))) pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jmax (partFrom, to)));
    repaint();
}

void NektarEditor::mouseUp (const juce::MouseEvent&)
{
    if (arcDrag >= 0) if (auto* pa = proc.apvts.getParameter (kArcIds[arcDrag])) pa->endChangeGesture();
    arcDrag = -1;
    partDrag = -1;
}

void NektarEditor::mouseMove (const juce::MouseEvent& e)
{   // the main screen has no words on its symbols: their names come up in the message line
    if (detailsView) return;
    const auto p = e.position / ((float) getWidth() / kDW);
    juce::String name;
    for (int pr = 0; pr < 2 && name.isEmpty(); ++pr)
    {
        for (int i = 0; i < 13; ++i) if (p.getDistanceFrom (charAt (pr, i)) <= 12.0f) { name = kCharNames[kCharOrder[i]]; break; }
        if (name.isEmpty() && p.getDistanceFrom (shapeAt (pr)) <= 15.0f) name = "Movement of the joystick (click: next shape, wheel: speed)";
        if (name.isEmpty() && p.getDistanceFrom (wildAt (pr)) <= 13.0f) name = "Gentle - Wild: how far apart the four corners are";
    }
    {
        const float d = p.getDistanceFrom (kArcs);
        for (int i = 0; i < 3 && name.isEmpty(); ++i) if (std::abs (d - kArcR[i]) <= 3.0f) name = kArcNames[i];
    }
    const char* harmNames[3] = { "As is", "One key", "Chord" };
    for (int i = 0; i < 3 && name.isEmpty(); ++i) if (harmChip (i).contains (p)) name = harmNames[i];
    if (name.isEmpty() && kKeyBtn.contains (p)) name = "Key (click: next, Shift-click: back)";
    if (name.isEmpty() && p.getDistanceFrom (kRecAt) <= 16.0f) name = recAll ? "Rec: the microphone into a new sound in the library" : "Rec: the microphone into sound " + juce::String (selectedSlot + 1);
    if (name.isEmpty() && p.getDistanceFrom (kResampleAt) <= 16.0f) name = recAll ? "Resample: the whole Nectar into a new sound in the library" : "Resample: sound " + juce::String (selectedSlot + 1) + " with all its changes, into itself";
    if (name.isEmpty() && kKeepBtn.contains (p)) name = "Keep: save this sound as a preset";
    if (name.isEmpty() && kBackBtn.contains (p)) name = "Back: the sound before the last Make";
    if (name.isEmpty() && p.getDistanceFrom (kRelBtn) <= 14.0f) name = "How the two pairs behave towards each other (click: next)";
    if (name != hoverSaid)
    {
        hoverSaid = name;
        if (name.isNotEmpty()) { say (name); repaint(); }
    }
}
