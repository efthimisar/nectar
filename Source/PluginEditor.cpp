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
constexpr float kPadCy = 212.0f;
constexpr float kCharR = 92.0f; // the characters round each joystick
constexpr float kModR = 56.0f;                   // module radius
const juce::Rectangle<float> kScreen (24.0f, 450.0f, 992.0f, 62.0f);
const juce::Rectangle<float> kModeBtn[3] = { { 150.0f, 414.0f, 54.0f, 26.0f }, { 208.0f, 414.0f, 78.0f, 26.0f }, { 290.0f, 414.0f, 84.0f, 26.0f } };
const juce::Rectangle<float> kScaleBtn[2] = { { 386.0f, 414.0f, 170.0f, 26.0f }, { 562.0f, 414.0f, 74.0f, 26.0f } };
const juce::Rectangle<float> kFaceBtn (644.0f, 412.0f, 94.0f, 28.0f);      // details view: back to the joysticks
const juce::Rectangle<float> kDetailsBtn (892.0f, 498.0f, 120.0f, 32.0f);  // main screen: open the panel with all the knobs
// the main screen (design units): sounds, Make, Back, Keep; Character and Gentle - Wild; the three big knobs and Space
const juce::Rectangle<float> kRandBtn (455.0f, 395.0f, 130.0f, 26.0f), kMakeBtn (420.0f, 528.0f, 200.0f, 48.0f),
                             kBackBtn (632.0f, 532.0f, 86.0f, 40.0f), kKeepBtn (322.0f, 532.0f, 86.0f, 40.0f);
const juce::Rectangle<float> kSpkBtn (836.0f, 601.0f, 110.0f, 30.0f), kNewSpaceBtn (954.0f, 601.0f, 52.0f, 30.0f);
const juce::Rectangle<float> kFaceScreen (28.0f, 438.0f, 984.0f, 78.0f); // the sample screen of the main screen
const juce::Point<float> kSpaceMove (764.0f, 616.0f);                       // the chosen dot's movement (click = next)
const juce::Point<float> kRelBtn (606.0f, 409.0f);                          // how the two pairs behave towards each other
inline juce::Point<float> charAt (int pair, int i)
{
    const float a = juce::MathConstants<float>::pi * (-0.5f + 2.0f * (float) i / 13.0f);
    return { kPadCx[pair] + kCharR * std::cos (a), kPadCy + kCharR * std::sin (a) };
}
inline juce::Point<float> wildAt (int pair)  { return { kPadCx[pair] + (pair == 0 ? 1.0f : -1.0f) * 88.0f, kPadCy - 88.0f }; }
inline juce::Point<float> shapeAt (int pair) { return { kPadCx[pair] + (pair == 0 ? -1.0f : 1.0f) * 88.0f, kPadCy - 88.0f }; }
inline juce::Rectangle<float> speedDots (int pair) { const auto c = shapeAt (pair); return { c.x - 18.0f, c.y - 32.0f, 36.0f, 12.0f }; }
// Space: 32 movements as symbols in two rows (still, round the listener, left - right, front - back, then the shapes)
inline juce::Rectangle<float> moveCell (int i) { return { 330.0f + (float) (i % 16) * 26.0f, 572.0f + (float) (i / 16) * 28.0f, 24.0f, 26.0f }; }
// beside each joystick: its movement (shape), a dice, its speed
inline juce::Point<float> joyBtn (int pair, int which) { return { pair == 0 ? 52.0f : 988.0f, 170.0f + 44.0f * (float) which }; }
inline juce::Rectangle<float> pickCell (int pair, int i) { return { kPadCx[pair] - 78.0f + (float) (i % 6) * 26.0f, kPadCy - 78.0f + (float) (i / 6) * 31.0f, 26.0f, 31.0f }; }
inline juce::Rectangle<float> charChip (int i) { return { 28.0f + (float) i * 100.0f, 498.0f, 92.0f, 32.0f }; }
inline juce::Rectangle<float> libChip (int i) { return { 273.0f + (float) i * 72.0f, 77.0f, 66.0f, 26.0f }; }
inline juce::Rectangle<float> harmChip (int i) { const float x[3] = { 740.0f, 802.0f, 878.0f }, w[3] = { 58.0f, 72.0f, 56.0f }; return { x[i], 536.0f, w[i], 32.0f }; }
const juce::Rectangle<float> kKeyBtn (940.0f, 536.0f, 72.0f, 32.0f);
inline juce::Rectangle<float> keyLabel (int slot) { return { kSlotCx[slot] - 42.0f, kSlotCy[slot] + 56.0f, 84.0f, 14.0f }; }

// small round buttons on each sound circle: lock (top right) and solo (top left); between the two circles of a pair: the blend lock
inline juce::Point<float> lockAt (int slot) { return { kSlotCx[slot] + 24.0f, kSlotCy[slot] - 26.0f }; }
inline juce::Point<float> soloAt (int slot) { return { kSlotCx[slot] - 24.0f, kSlotCy[slot] - 26.0f }; }
inline juce::Point<float> blendAt (int pair) { return { (kSlotCx[pair * 2] + kSlotCx[pair * 2 + 1]) * 0.5f, kSlotCy[pair * 2] }; }
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
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (float f : { 0.36f, 0.68f })
        g.drawEllipse (R - R * f, R - R * f, 2 * R * f, 2 * R * f, 1.0f);
    g.drawLine (R, 4, R, 2 * R - 4, 1.0f);
    g.drawLine (4, R, 2 * R - 4, R, 1.0f);
    g.setColour (Colour (0xff7ec8e3).withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("front", juce::Rectangle<float> (R - 25, 5, 50, 13), juce::Justification::centred);
    g.drawText ("back", juce::Rectangle<float> (R - 25, 2 * R - 18, 50, 13), juce::Justification::centred);
    // the listener: a head seen from above, nose up
    g.setColour (kLight);
    g.fillEllipse (R - 9, R - 9, 18, 18);
    juce::Path nose;
    nose.addTriangle (R - 4, R - 8, R + 4, R - 8, R, R - 15);
    g.fillPath (nose);
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
    if (p.getDistanceFrom ({ 398.0f, 30.0f }) <= 22.0f) toggleRecording();
    else if (p.getDistanceFrom ({ 447.0f, 30.0f }) <= 22.0f) toggleRoomTone();
    else if (p.getDistanceFrom ({ 497.0f, 30.0f }) <= 22.0f) toggleResample();
    else if (juce::Rectangle<float> (316, 15, 56, 30).contains (p)) showPresetMenu();
    else if (juce::Rectangle<float> (570, 11, 142, 38).contains (p)) say (proc.saveToLibrary (selectedSlot));
    else return false;
    return true;
}

void NektarEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{   // the wheel on a joystick's movement button: the next / previous shape
    const auto p = e.position / ((float) getWidth() / kDW);
    if (detailsView) return;
    for (int pair = 0; pair < 2; ++pair)
        if (p.getDistanceFrom (joyBtn (pair, 0)) <= 18.0f)
        {
            const char* id = pair == 0 ? "vec1shape" : "vec2shape";
            const int cur = juce::roundToInt (proc.apvts.getRawParameterValue (id)->load());
            const int n = motionshapes::count + 1;
            if (auto* pa = proc.apvts.getParameter (id)) pa->setValueNotifyingHost (pa->convertTo0to1 ((float) (((cur + (w.deltaY < 0 ? 1 : -1)) % n + n) % n)));
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
        if (p.getDistanceFrom ({ kSlotCx[i], kSlotCy[i] }) <= kDotR)
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
    proc.startRecording (selectedSlot);
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
    if (proc.getSample (selectedSlot) != nullptr && resampleArmedSlot != selectedSlot)
    {
        resampleArmedSlot = selectedSlot;
        resampleArmTicks = 100; // 5 s
        say ("Slot " + juce::String (selectedSlot + 1) + " has a sound. Click Resample again to replace it.");
        return;
    }
    resampleArmedSlot = -1;
    say ("Resampling the output into slot " + juce::String (selectedSlot + 1) + ". Play, then click again.");
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
    {   // the main screen's movement: grains read, levels, the joystick trails
        for (int slot = 0; slot < 4; ++slot)
        {
            levelShown[(size_t) slot] += (proc.slotLevel[(size_t) slot].load() - levelShown[(size_t) slot]) * 0.35f;
            const int c = proc.tapCount[(size_t) slot].load();
            if (slot == selectedSlot)
            {
                const int fresh = juce::jmin (16, c - sparkSeen[(size_t) slot]);
                for (int k = 0; k < fresh; ++k)
                    sparks.push_back ({ proc.tapPos[(size_t) slot][(size_t) ((c - 1 - k) & 15)].load(), 0.2f + 0.6f * juce::Random::getSystemRandom().nextFloat(), 1.0f });
            }
            sparkSeen[(size_t) slot] = c;
        }
        mixShown += (proc.mixLevel.load() - mixShown) * 0.3f;
        for (auto& sp : sparks) sp.life -= 0.06f;
        sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& sp) { return sp.life <= 0.0f; }), sparks.end());
        if (sparks.size() > 300) sparks.erase (sparks.begin(), sparks.begin() + (long) (sparks.size() - 300));
        for (auto& pp : pairPads) if (pp != nullptr) pp->repaint();
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
        {   // Gentle - Wild of each joystick, at its upper outer side
            const auto w = wildAt (pr);
            addKnob (w.x, w.y, 12.0f, "", false, 0.0f, pr == 0 ? "wild1" : "wild2");
        }
        addKnob (70.0f, 600.0f, 22.0f, "Motion", false, 0.0f, "mspeed");
        addKnob (164.0f, 600.0f, 22.0f, "Weight", false, 0.0f, "mweight");
        addKnob (258.0f, 600.0f, 22.0f, "Light", false, 0.0f, "mlight");
        spaceSpeedId = slotId ("spspeed", spaceSlot);
        addKnob (kSpaceMove.x + 46.0f, kSpaceMove.y, 12.0f, "", false, 0.0f, spaceSpeedId.toRawUTF8());
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
    insetBox (g, { 196, 26, 110, 9 }, 4.5f);

    const float bx[] = { 398, 447, 497 };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (Colour (0xff1e1d1b));
        g.fillEllipse (bx[i] - 21, 9, 42, 42);
        g.setColour (kAcc);
        g.drawEllipse (bx[i] - 21, 9, 42, 42, 2.0f);
    }
    g.fillEllipse (bx[0] - 9, 21, 18, 18); // record
    for (int k = -2; k <= 2; ++k)           // room tone
    {
        const float h = 4.0f + (float) (3 - std::abs (k)) * 3.0f;
        g.drawLine (bx[1] + (float) k * 4.5f, 30 - h, bx[1] + (float) k * 4.5f, 30 + h, 2.0f);
    }
    {                                       // resample
        juce::Path arc;
        arc.addCentredArc (bx[2], 30, 9, 9, 0, 0.6f, 5.6f, true);
        g.strokePath (arc, juce::PathStrokeType (2.0f));
        juce::Path head;
        head.addTriangle (bx[2] + 5, 20, bx[2] + 13, 22, bx[2] + 8, 29);
        g.fillPath (head);
    }

    g.setColour (kMuted);
    g.drawRoundedRectangle (316, 15, 56, 30, 15, 1.5f);
    g.setColour (kLight);
    g.setFont (font (reg, 13));
    g.drawText ("Presets", juce::Rectangle<float> (316, 15, 56, 30), juce::Justification::centred);

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

    // ---- v2: four small slot circles (two each side of the ring)
    for (int slot = 0; slot < 4; ++slot)
    {
        const auto smp = proc.getSample (slot);
        const auto peaks = peaksOf (smp.get(), 30);
        slotDot (g, reg, bld, kSlotCx[slot], kSlotCy[slot], slot + 1, smp != nullptr ? smp->name : juce::String(),
                 smp == nullptr, peaks, selection.count (slot) > 0 && ! masterView && detailsView);
    }

    // ---- v2: one panel from edge to edge
    {
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

void NektarEditor::drawFace (Graphics& g)
{
    auto button = [&] (juce::Rectangle<float> r, const juce::String& text, bool usable, bool strong, float size)
    {
        g.setColour (strong && usable ? kAcc : Colour (0xff2f2d2b));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (strong && usable ? kDark : (usable ? kLight : kMuted.withAlpha (0.45f)));
        g.setFont (font (strong ? bold : regular, size));
        g.drawText (text, r, juce::Justification::centred);
    };
    auto disc = [&] (juce::Point<float> c, float r, bool on, juce::Colour tint)
    {
        g.setColour (on ? tint : Colour (0xff2f2d2b));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    };
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // ---- the two joysticks: characters round them, Gentle - Wild and the movement at their upper corners
    for (int pr = 0; pr < 2; ++pr)
    {
        const auto tint = kWada[pr == 0 ? 1 : 2];
        const int cur = juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "character1" : "character2")->load());
        for (int i = 0; i < 13; ++i)
        {
            const auto c = charAt (pr, i);
            disc (c, 11.0f, i == cur, tint);
            charSymbol (g, i, c, 6.5f, i == cur ? kDark : kLight.withAlpha (0.85f));
        }
        const int shape = juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "vec1shape" : "vec2shape")->load());
        const auto sc = shapeAt (pr);
        disc (sc, 15.0f, shape > 0, tint);
        static const int pick[4] = { 1, 3, 9, 10 };
        if (shape > 0) motionshapes::drawSymbol (g, pick[juce::jlimit (0, 3, shape - 1)], sc, 10.0f, kDark);
        else { g.setColour (kLight); g.fillEllipse (sc.x - 3.0f, sc.y - 3.0f, 6.0f, 6.0f); }
        if (shape > 0)
        {   // its speed: five dots above it
            const int speed = juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "vec1speed" : "vec2speed")->load());
            const auto r = speedDots (pr);
            for (int k = 0; k < 5; ++k)
            {
                g.setColour (k <= speed ? kAcc : kMuted.withAlpha (0.35f));
                g.fillEllipse (r.getX() + 3.0f + 7.5f * (float) k - 2.2f, r.getCentreY() - 2.2f, 4.4f, 4.4f);
            }
        }
    }

    // ---- the sound circles breathe with their level; the link between two shows which of them is heard more
    for (int slot = 0; slot < 4; ++slot)
    {
        const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (levelShown[(size_t) slot]) * 2.2f);
        if (proc.getSample (slot) != nullptr && lv > 0.01f)
        {
            const float rr = kDotR + 3.0f + 12.0f * lv;
            g.setColour (kWada[slot % 5].withAlpha (0.12f + 0.3f * lv));
            g.drawEllipse (kSlotCx[slot] - rr, kSlotCy[slot] - rr, 2 * rr, 2 * rr, 2.0f + 3.0f * lv);
        }
    }
    auto badge = [&] (juce::Point<float> c, bool on, int kind)
    {
        g.setColour (on ? kAcc : Colour (0xff2f2d2b));
        g.fillEllipse (c.x - kBadgeR, c.y - kBadgeR, 2 * kBadgeR, 2 * kBadgeR);
        g.setColour (on ? kDark : kMuted);
        if (kind == 0)
        {
            g.fillRoundedRectangle (c.x - 4.5f, c.y - 1.0f, 9.0f, 6.5f, 1.2f);
            juce::Path sh;
            sh.addCentredArc (c.x, c.y - 1.5f, 3.0f, 3.2f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (sh, juce::PathStrokeType (1.4f));
        }
        else if (kind == 1)
        {
            g.setFont (font (bold, 12));
            g.drawText ("S", juce::Rectangle<float> (c.x - kBadgeR, c.y - kBadgeR, 2 * kBadgeR, 2 * kBadgeR), juce::Justification::centred);
        }
        else
        {
            g.drawEllipse (c.x - 6.0f, c.y - 3.0f, 7.0f, 6.0f, 1.3f);
            g.drawEllipse (c.x - 1.0f, c.y - 3.0f, 7.0f, 6.0f, 1.3f);
        }
    };
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
        {   // which of the two is heard more right now (the Cross of the pair), and how far Melt has gone
            const float x = proc.knobNow01 (pr == 0 ? "xmorph12" : "xmorph34");
            g.setColour (kWada[(pr * 2) % 5].withAlpha (0.25f + 0.75f * (1.0f - x)));
            g.fillEllipse (c.x - kBadgeR - 9.0f, c.y - 3.0f, 6.0f, 6.0f);
            g.setColour (kWada[(pr * 2 + 1) % 5].withAlpha (0.25f + 0.75f * x));
            g.fillEllipse (c.x + kBadgeR + 3.0f, c.y - 3.0f, 6.0f, 6.0f);
            if (juce::roundToInt (proc.apvts.getRawParameterValue (pr == 0 ? "character1" : "character2")->load()) == 11)
            {
                const float m = proc.knobNow01 (pr == 0 ? "morph12" : "morph34");
                g.setColour (kAcc.withAlpha (0.8f));
                g.drawLine (c.x - 12.0f, c.y + kBadgeR + 4.0f, c.x - 12.0f + 24.0f * m, c.y + kBadgeR + 4.0f, 2.0f);
            }
        }
    }
    g.setFont (font (regular, 11.5f));
    for (int slot = 0; slot < 4; ++slot)
    {   // key and tempo under each circle (click to correct)
        auto k = proc.keyText (slot);
        if (k.isEmpty()) continue;
        k = k.replace (" minor", "m").replace (" major", "");
        if (k == "no key") k = "-";
        auto t = proc.tempoText (slot);
        t = t.endsWith (" bpm") ? t.upToFirstOccurrenceOf (" ", false, false) : (t == "..." ? t : "-");
        g.setColour (kWada[slot % 5]);
        g.drawText (k + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + t, keyLabel (slot), juce::Justification::centred);
    }

    // ---- the library: categories above the map, Random sounds and the relation below it, the level round the Space pad
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
            g.fillRoundedRectangle (r, 13);
            g.setColour (on ? kDark : kLight);
            g.setFont (font (regular, 13));
            g.drawFittedText (name, r.reduced (5, 0).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }
    bool allLocked = true, anySound = false;
    for (int slot = 0; slot < 4; ++slot) { allLocked = allLocked && proc.slotLocked (slot); anySound = anySound || proc.getSample (slot) != nullptr; }
    button (kRandBtn, "Random sounds", ! allLocked, false, 14);
    {
        const int rel = juce::roundToInt (proc.apvts.getRawParameterValue ("relation")->load());
        disc (kRelBtn, 13.0f, rel > 0, kAcc);
        relationSymbol (g, rel, kRelBtn, 8.5f, rel > 0 ? kDark : kLight);
    }
    {   // the level of everything, round the Space pad (to be judged)
        const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (mixShown) * 2.0f);
        for (int i = 0; i < 48; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 48.0f;
            const float wob = 0.6f + 0.4f * (float) std::sin (now * 3.0 + i * 0.7);
            const float len = 2.0f + 9.0f * lv * wob;
            g.setColour (kWada[2].withAlpha (0.25f + 0.6f * lv));
            g.drawLine (kLibX + std::cos (a) * 72.0f, kLibY + std::sin (a) * 72.0f, kLibX + std::cos (a) * (72.0f + len), kLibY + std::sin (a) * (72.0f + len), 1.6f);
        }
    }

    // ---- the sample screen: the chosen circle's sound, its part, where it is read, its grains, its steps, the beat
    {
        const auto r = kFaceScreen;
        g.setColour (Colour (0xff1b1a19));
        g.fillRoundedRectangle (r, 10.0f);
        const auto smp = proc.getSample (selectedSlot);
        const auto col = kWada[selectedSlot % 5];
        g.setFont (font (regular, 12));
        g.setColour (kMuted);
        g.drawText (juce::String (selectedSlot + 1) + (smp != nullptr ? "  " + smp->name : juce::String ("  empty - click a circle to choose")),
                    r.withHeight (16.0f).translated (10.0f, 4.0f), juce::Justification::centredLeft);
        if (smp != nullptr && smp->data.getNumSamples() > 0)
        {
            if (smp->serial != scrSerial)
            {
                scrSerial = smp->serial;
                const int cols = 320, n = smp->data.getNumSamples();
                scrPeaks.assign (cols, 0.0f);
                float mx = 1.0e-6f;
                for (int i = 0; i < cols; ++i)
                {
                    const int a = (int) ((double) i * n / cols), b = juce::jmax (a + 1, (int) ((double) (i + 1) * n / cols));
                    float m = 0.0f;
                    for (int c = 0; c < smp->data.getNumChannels(); ++c) m = juce::jmax (m, smp->data.getMagnitude (c, a, b - a));
                    scrPeaks[(size_t) i] = m; mx = juce::jmax (mx, m);
                }
                for (auto& v : scrPeaks) v /= mx;
            }
            const auto wave = r.reduced (10.0f, 0.0f).withTrimmedTop (22.0f).withTrimmedBottom (18.0f);
            const float pa = proc.apvts.getRawParameterValue (slotId ("grstart", selectedSlot))->load();
            const float pb = proc.apvts.getRawParameterValue (slotId ("grend", selectedSlot))->load();
            const float a0 = juce::jmin (pa, pb), b0 = juce::jmax (pa, pb);
            g.setColour (col.withAlpha (0.13f));
            g.fillRect (wave.getX() + a0 * wave.getWidth(), wave.getY() - 2.0f, (b0 - a0) * wave.getWidth(), wave.getHeight() + 4.0f);
            const float cw = wave.getWidth() / (float) scrPeaks.size();
            for (size_t i = 0; i < scrPeaks.size(); ++i)
            {
                const float x = wave.getX() + (float) i * cw, f = (float) i / (float) scrPeaks.size();
                const float h = juce::jmax (0.5f, scrPeaks[i] * wave.getHeight() * 0.5f);
                g.setColour ((f >= a0 && f <= b0) ? col.withAlpha (0.9f) : kMuted.withAlpha (0.35f));
                g.fillRect (x, wave.getCentreY() - h, juce::jmax (1.0f, cw * 0.7f), 2 * h);
            }
            const float ph = proc.playheadOf (selectedSlot);
            if (ph >= 0.0f)
            {
                const float x = wave.getX() + (a0 + ph * (b0 - a0)) * wave.getWidth();
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillRect (x - 0.75f, wave.getY() - 4.0f, 1.5f, wave.getHeight() + 8.0f);
            }
            for (const auto& sp : sparks)
            {   // the grains, as they are read
                g.setColour (col.withAlpha (juce::jlimit (0.0f, 1.0f, sp.life)));
                const float x = wave.getX() + (a0 + sp.x * (b0 - a0)) * wave.getWidth(), rad = 1.5f + 2.0f * sp.life;
                g.fillEllipse (x - rad, wave.getY() + sp.y * wave.getHeight() - rad, 2 * rad, 2 * rad);
            }
            if (juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grmode", selectedSlot))->load()) == 2)
            {   // the 16 steps, the one playing lit
                const int pat = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("grpat", selectedSlot))->load());
                const int st = proc.tapStep[(size_t) selectedSlot].load();
                const float sw = 14.0f, x0 = r.getRight() - 12.0f - 16.0f * sw;
                for (int k = 0; k < 16; ++k)
                {
                    const bool on = ((pat >> k) & 1) != 0;
                    g.setColour (k == st ? kAcc : (on ? col.withAlpha (0.6f) : kMuted.withAlpha (0.2f)));
                    g.fillRoundedRectangle (x0 + (float) k * sw, r.getBottom() - 13.0f, sw - 3.0f, 7.0f, 2.0f);
                }
            }
        }
        if (proc.beatOn.load())
        {   // the beat of the host
            const float bt = proc.beatNow.load();
            g.setColour (kAcc.withAlpha (bt < 0.15f ? 1.0f : 0.25f));
            g.fillEllipse (r.getRight() - 16.0f, r.getY() + 8.0f, 8.0f, 8.0f);
        }
    }

    // ---- Keep, Make, Back; the message
    const bool busy = proc.baking();
    button (kKeepBtn, "Keep", proc.vectorOn (0) || proc.vectorOn (1), false, 15);
    button (kMakeBtn, "Make", anySound, true, 21);
    if (busy)
    {   // Make "simmers" while the sounds of the corners are being prepared
        for (int k = 0; k < 8; ++k)
        {
            const float a = (float) (now * 2.2) + (float) k * 0.785f, rr = 1.0f + 0.15f * (float) std::sin (now * 4.0 + k);
            g.setColour (kWada[k % 5].withAlpha (0.85f));
            g.fillEllipse (kMakeBtn.getCentreX() + std::cos (a) * 108.0f * rr - 3.0f, kMakeBtn.getCentreY() + std::sin (a) * 30.0f * rr - 3.0f, 6.0f, 6.0f);
        }
    }
    button (kBackBtn, "Back", proc.vectorHistory (0) > 0 || proc.vectorHistory (1) > 0, false, 15);
    if (message.isNotEmpty())
    {
        g.setColour (kMuted);
        g.setFont (font (regular, 13));
        g.drawText (message, juce::Rectangle<float> (300.0f, 582.0f, 440.0f, 18.0f), juce::Justification::centred);
    }
    g.setColour (kMuted.withAlpha (0.8f));
    g.setFont (font (regular, 11));
    const char* ends[3][2] = { { "slow", "fast" }, { "thin", "heavy" }, { "dull", "bright" } };
    for (int i = 0; i < 3; ++i)
    {
        const float cx = 70.0f + 94.0f * (float) i;
        g.drawText (ends[i][0], juce::Rectangle<float> (cx - 58.0f, 612.0f, 34.0f, 14.0f), juce::Justification::centredRight);
        g.drawText (ends[i][1], juce::Rectangle<float> (cx + 24.0f, 612.0f, 36.0f, 14.0f), juce::Justification::centredLeft);
    }

    // ---- Harmony and Key; the chosen dot's movement, its speed, headphones / speakers, a new placement
    const char* harm[3] = { "As is", "One key", "Chord" };
    const int hm = juce::roundToInt (proc.apvts.getRawParameterValue ("harmony")->load());
    for (int i = 0; i < 3; ++i)
    {
        const auto r = harmChip (i);
        g.setColour (i == hm ? kAcc : Colour (0xff2f2d2b));
        g.fillRoundedRectangle (r, 16);
        g.setColour (i == hm ? kDark : kLight);
        g.setFont (font (regular, 14));
        g.drawText (harm[i], r, juce::Justification::centred);
    }
    button (kKeyBtn, juce::String ("Key ") + Tonality::noteName (juce::roundToInt (proc.apvts.getRawParameterValue ("key")->load())), hm != 0, false, 14);
    {
        const int mv = juce::roundToInt (proc.apvts.getRawParameterValue (slotId ("spmove", spaceSlot))->load());
        disc (kSpaceMove, 16.0f, mv > 0, kWada[spaceSlot % 5]);
        moveSymbol (g, mv, kSpaceMove, 10.0f, mv > 0 ? kDark : kLight);
        g.setColour (kWada[spaceSlot % 5]);
        g.setFont (font (bold, 12));
        g.drawText (juce::String (spaceSlot + 1), juce::Rectangle<float> (kSpaceMove.x - 40.0f, kSpaceMove.y - 8.0f, 16.0f, 16.0f), juce::Justification::centred);
    }
    const bool spk = proc.apvts.getRawParameterValue ("spspk")->load() > 0.5f;
    button (kSpkBtn, spk ? "Speakers" : "Headphones", true, false, 13);
    button (kNewSpaceBtn, {}, true, false, 14);
    diceSymbol (g, kNewSpaceBtn.getCentre(), 9.0f, kLight);
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
                setInt (pr == 0 ? "character1" : "character2", (float) i);
                static const char* const names[13] = { "Cloud", "Rhythm", "Metal", "Air", "Reverse", "Drone", "Swell", "Glitch", "Dust", "Bells", "Shimmer", "Melt", "Arp" };
                say (juce::String (names[i]) + " for sounds " + (pr == 0 ? "1 and 2" : "3 and 4") + ". Press Make.");
                repaint();
                return;
            }
        if (p.getDistanceFrom (shapeAt (pr)) <= 16.0f)
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
    for (int slot = 0; slot < 4; ++slot)
    {
        if (p.getDistanceFrom (lockAt (slot)) <= kBadgeR + 2.0f)
        {
            proc.setSlotLock (slot, ! proc.slotLocked (slot));
            say (proc.slotLocked (slot) ? "Sound " + juce::String (slot + 1) + " is locked: Make and Random sounds leave it as it is."
                                        : "Sound " + juce::String (slot + 1) + " is free again.");
            repaint();
            return;
        }
        if (p.getDistanceFrom (soloAt (slot)) <= kBadgeR + 2.0f)
        {
            proc.setSolo (proc.solo() == slot ? -1 : slot);
            say (proc.solo() == slot ? "Only sound " + juce::String (slot + 1) + " is heard." : "All four sounds are heard.");
            repaint();
            return;
        }
    }
    for (int pr = 0; pr < 2; ++pr)
        if (p.getDistanceFrom (blendAt (pr)) <= kBadgeR + 2.0f)
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
            const char* says[3] = { "Each sound plays as it was recorded.", "All sounds with a key are moved into one key: the Key button.",
                                    "The keynotes of the sounds go to the notes of one chord on the Key button." };
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
    if (p.getDistanceFrom (kSpaceMove) <= 17.0f)
    {   // the chosen dot's next movement
        const juce::String id = slotId ("spmove", spaceSlot);
        setInt (id, (float) ((intOf (id) + (e.mods.isShiftDown() ? 5 : 1)) % 6));
        repaint();
        return;
    }
    if (kSpkBtn.contains (p))
    {
        if (auto* pa = proc.apvts.getParameter ("spspk")) pa->setValueNotifyingHost (pa->getValue() > 0.5f ? 0.0f : 1.0f);
        repaint();
        return;
    }
    if (kNewSpaceBtn.contains (p)) { proc.newSpace(); say ("A new place and movement for each sound."); repaint(); return; }
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
    if (kFaceScreen.contains (p) && proc.getSample (selectedSlot) != nullptr)
    {   // choose the part of the sound: press at one end, drag to the other
        const auto wave = kFaceScreen.reduced (10.0f, 0.0f);
        partFrom = juce::jlimit (0.0f, 1.0f, (p.x - wave.getX()) / wave.getWidth());
        partDrag = selectedSlot;
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
    if (const int sl = slotAt (p); sl >= 0) { selectedSlot = sl; repaint(); }
}

void NektarEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (partDrag < 0) return;
    const auto p = e.position / ((float) getWidth() / kDW);
    const auto wave = kFaceScreen.reduced (10.0f, 0.0f);
    const float to = juce::jlimit (0.0f, 1.0f, (p.x - wave.getX()) / wave.getWidth());
    if (std::abs (to - partFrom) < 0.01f) return;
    if (auto* pa = proc.apvts.getParameter (slotId ("grstart", partDrag))) pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jmin (partFrom, to)));
    if (auto* pa = proc.apvts.getParameter (slotId ("grend", partDrag))) pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jmax (partFrom, to)));
    repaint();
}

void NektarEditor::mouseUp (const juce::MouseEvent&) { partDrag = -1; }
