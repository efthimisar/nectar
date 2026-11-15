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
constexpr float kCx = 500.0f, kCy = 410.0f, kRingR = 255.0f, kScreenR = 106.0f, kSlotR = 135.0f;
constexpr float kSlotScale = 0.8f;   // the slot discs and the centre are drawn at 80 % so that the module grid fits below
constexpr float kSlotCx[4] = { 186.0f, 814.0f, 186.0f, 814.0f };
constexpr float kSlotCy[4] = { 220.0f, 220.0f, 450.0f, 450.0f };
constexpr float kCentreY = 335.0f;   // centre of the ring and the check screen after scaling

// The nine modules of the bottom panel. A knob without a parameter id is not built yet (shown dim).
struct ModuleDef { const char* title; const char* sub; juce::uint32 colour; const char* knob[4]; const char* id[4]; };
const ModuleDef kModules[] = {
    { "TIME",      "length \xc2\xb7 freeze",       0xfffdc57e, { "Keep length", "Stretch", "Loop", "Freeze" },  { "keeplength", "stretch", "loop", "freeze" } },
    { "BODY",      "tone \xc2\xb7 morph",          0xffb5b1d8, { "Tone", "Focus", "Size", "Morph" },            { "bodytone", "bodyfocus", "bodysize", "morph" } },
    { "SOURCE",    "model \xc2\xb7 grit \xc2\xb7 spread", 0xff78cdd0, { "Model", "Grit", "Spread", "Noise" }, { "model", "srcrough", "blur", "modelnoise" } },
    { "RESONATOR", "follows key",                     0xffc1c494, { "Tune", "Decay", "Material", "Mix" },          { "restune", "resdecay", "resmat", "resmix" } },
    { "FILTER",    "ladder \xc2\xb7 svf \xc2\xb7 comb \xc2\xb7 formant", 0xffe07a5f, { "Type", "Cutoff", "Reso", "Mix" }, { "filtype", "filcut", "filres", "filmix" } },
    { "REVERB",    "shimmer \xc2\xb7 tuned",       0xff8fb8de, { "Size", "Decay", "Shimmer", "Mix" },           { nullptr, nullptr, nullptr, nullptr } },
    { "DELAY",     "pitch per repeat",               0xfff2cc8f, { "Time", "Feedback", "Pitch", "Mix" },          { nullptr, nullptr, nullptr, nullptr } },
    { "LFO",       "1 \xc2\xb7 2",                 0xfff4a6c0, { "Rate 1", "Shape 1", "Rate 2", "Shape 2" },    { "lfo1rate", "lfo1shape", "lfo2rate", "lfo2shape" } },
    { "SLICER",    "rhythmic gate",                  0xff9bc89b, { "Rate", "Pattern", "Smooth", "Depth" },        { "slrate", "slpattern", "slsmooth", "sldepth" } },
};
constexpr int kNumModules = 9;
constexpr float kGridX = 30.0f, kGridY = 566.0f, kGridW = 940.0f, kGridH = 418.0f, kGridPad = 12.0f, kGridGap = 10.0f;
juce::Rectangle<float> moduleRect (int m)
{
    const float w = (kGridW - 2 * kGridPad - 2 * kGridGap) / 3.0f, h = (kGridH - 2 * kGridPad - 2 * kGridGap) / 3.0f;
    return { kGridX + kGridPad + (float) (m % 3) * (w + kGridGap), kGridY + kGridPad + (float) (m / 3) * (h + kGridGap), w, h };
}
juce::Point<float> moduleKnobCentre (int m, int k)
{
    const auto r = moduleRect (m);
    return { r.getX() + 10.0f + ((float) k + 0.5f) * ((r.getWidth() - 20.0f) / 4.0f), r.getY() + 52.0f };
}

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
juce::Array<float> peaksOf (const Sample* smp)
{
    juce::Array<float> peaks;
    if (smp == nullptr)
        return peaks;
    const int n = smp->data.getNumSamples();
    const int bars = 40;
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
    nektar::drawKnob (g, typeface, kWidth / 2.0f, radius, radius, p, text, dimmed);
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
            menu.addItem (4, "Slicer", true, r.src == NektarProcessor::kModSlicer);
            menu.addSeparator();
            menu.addItem (100, "Amount: hold Shift and drag the knob", false);
            juce::Component::SafePointer<KnobComponent> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options(), [safe, r] (int choice)
            {
                if (safe == nullptr || choice < 1 || choice > 4)
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
        parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);
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
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / 1000.0f));
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

NektarEditor::NektarEditor (NektarProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::CarlitoRegular_ttf, BinaryData::CarlitoRegular_ttfSize);
    bold    = juce::Typeface::createSystemTypefaceFor (BinaryData::CarlitoBold_ttf, BinaryData::CarlitoBold_ttfSize);

    // slots: 6 knobs each
    const float slotX[] = { kSlotCx[0], kSlotCx[1], kSlotCx[2], kSlotCx[3] };
    const float slotY[] = { kSlotCy[0], kSlotCy[1], kSlotCy[2], kSlotCy[3] };
    const char* names[] = { "Level", "Pitch", "Noise", "Hum", "Low cut", "Tail" };
    const float fixedVals[4][6] = { { 0.8f, 0.5f, 0.50f, 0.95f, 0.18f, 0.25f },
                                    { 0.40f, 0.70f, 0.55f, 0.92f, 0.45f, 0.80f },
                                    { 0.5f, 0.5f, 0.5f, 0.5f, 0.2f, 0.7f },
                                    { 0.55f, 0.38f, 0.30f, 0.10f, 0.22f, 0.72f } };
    // row 1: Level, Pitch, Pan (+ Intermorph on slots 1 and 3); row 2: Noise, Hum, Low cut, Tail
    for (int s = 0; s < 4; ++s)
    {
        static const char* const levelIds[] = { "level", "level2", "level3", "level4" };
        static const char* const pitchIds[] = { "pitch", "pitch2", "pitch3", "pitch4" };
        static const char* const tailIds[] = { "tail", "tail2", "tail3", "tail4" };
        static const char* const noiseIds[] = { "noise", "noise2", "noise3", "noise4" };
        static const char* const humIds[] = { "hum", "hum2", "hum3", "hum4" };
        static const char* const lowCutIds[] = { "lowcut", "lowcut2", "lowcut3", "lowcut4" };
        static const char* const panIds[] = { "pan", "pan2", "pan3", "pan4" };
        struct K { const char* label; const char* id; float fixed; };
        std::vector<K> row1 { { "Level", levelIds[s], fixedVals[s][0] }, { "Pitch", pitchIds[s], fixedVals[s][1] }, { "Pan", panIds[s], 0.5f } };
        if (s == 0) row1.push_back ({ "Inter", "imorph12", 0.0f });
        if (s == 2) row1.push_back ({ "Inter", "imorph34", 0.0f });
        const std::vector<K> row2 { { "Noise", noiseIds[s], fixedVals[s][2] }, { "Hum", humIds[s], fixedVals[s][3] },
                                    { "Low cut", lowCutIds[s], fixedVals[s][4] }, { "Tail", tailIds[s], fixedVals[s][5] } };
        const float spacing = 33.0f;
        for (int r = 0; r < 2; ++r)
        {
            const auto& row = r == 0 ? row1 : row2;
            for (size_t i = 0; i < row.size(); ++i)
                addKnob (slotX[s] + ((float) i - 0.5f * (float) (row.size() - 1)) * spacing, slotY[s] + (r == 0 ? 8.0f : 52.0f), 11.5f,
                         row[i].label, false, row[i].fixed, row[i].id);
        }
    }
    (void) names;

    // module grid (one panel, 3 x 3): see kModules
    for (int m = 0; m < kNumModules; ++m)
        for (int k = 0; k < 4; ++k)
        {
            const auto& d = kModules[m];
            const auto c = moduleKnobCentre (m, k);
            addKnob (c.x, c.y, 17.0f, d.knob[k], d.id[k] == nullptr, 0.0f, d.id[k], d.id[k] != nullptr && juce::String (d.id[k]) == "loop");
        }

    addKnob (740, 30, 13, "", false, 0.5f, "bright"); // Tilt (formerly Bright); replaces the unwired Out knob

    constrainer.setFixedAspectRatio (1.0);
    constrainer.setMinimumSize (500, 500);
    constrainer.setMaximumSize (2400, 2400);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setWantsKeyboardFocus (true);
    proc.addChangeListener (this);
    hud = std::make_unique<RecordHud> (proc, regular);
    addAndMakeVisible (*hud);
    startTimerHz (20);
    setSize (860, 860);
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
    const float s = (float) getWidth() / 1000.0f;
    hud->setBounds (getLocalBounds());
    for (int i = 0; i < knobs.size(); ++i)
    {
        auto* k = knobs[i];
        const auto c = knobPos[i];
        const float r = (k->height() - 20.0f) / 2.0f;
        k->setBounds (juce::Rectangle<float> ((c.x - KnobComponent::kWidth / 2.0f) * s, (c.y - r) * s,
                                              KnobComponent::kWidth * s, k->height() * s).toNearestInt());
    }
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
        ig.addTransform (juce::AffineTransform::scale ((float) pw / 1000.0f, (float) ph / 1000.0f));
        renderDesign (ig);
        addGrain (cache);
    }
    g.drawImage (cache, getLocalBounds().toFloat());
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
    const auto p = e.position / ((float) getWidth() / 1000.0f);
    const int slot = slotAt (p);
    if (slot >= 0 && juce::Rectangle<float> (kSlotCx[slot] - 80 * kSlotScale, kSlotCy[slot] - 68 * kSlotScale, 160 * kSlotScale, 46 * kSlotScale).contains (p))
    {
        selectSlot (slot);
        chooseFile (slot);
    }
    else if (slot >= 0)
        selectSlot (slot);
    else if (p.getDistanceFrom ({ 398.0f, 30.0f }) <= 22.0f)
        toggleRecording();
    else if (p.getDistanceFrom ({ 447.0f, 30.0f }) <= 22.0f)
        toggleRoomTone();
    else if (p.getDistanceFrom ({ 497.0f, 30.0f }) <= 22.0f)
        toggleResample();
    else if (juce::Rectangle<float> (316, 15, 56, 30).contains (p))
        showPresetMenu();

    else if (juce::Rectangle<float> (570, 11, 142, 38).contains (p))
        say (proc.saveToLibrary (selectedSlot));
}

int NektarEditor::slotAt (juce::Point<float> p) const
{
    for (int i = 0; i < 4; ++i)
        if (p.getDistanceFrom ({ kSlotCx[i], kSlotCy[i] }) <= kSlotR * kSlotScale)
            return i;
    return -1;
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
    const auto presets = NektarProcessor::listPresets();
    for (int i = 0; i < presets.size(); ++i)
        menu.addItem (100 + i, presets[i].getFileNameWithoutExtension());
    if (presets.isEmpty())
        menu.addItem (2, "(no presets yet)", false);
    juce::Component::SafePointer<NektarEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (
                            juce::Rectangle<int> ((int) (316 * getWidth() / 1000.0f), (int) (45 * getWidth() / 1000.0f), 64, 4))),
                        [safe, presets] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result >= 100 && result - 100 < presets.size())
        {
            safe->say (safe->proc.loadPreset (presets[result - 100]));
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
    if (proc.recordingFinishedByLimit())
        say (proc.stopRecording());
    if (proc.isRecording() || hudWasRecording)
        hud->repaint();
    hudWasRecording = proc.isRecording();
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
    int slot = slotAt (juce::Point<float> ((float) x, (float) y) / ((float) getWidth() / 1000.0f));
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
    shadowRect (g, { 0, 0, 1000, 60 }, 0, 0.5f, 14, { 0, 6 });
    g.setGradientFill (ColourGradient (Colour (0xff353331), 0, 0, Colour (0xff272523), 0, 60, false));
    g.fillRect (0.0f, 0.0f, 1000.0f, 60.0f);

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
                juce::Rectangle<float> (850, 8, 130, 44), juce::Justification::centredRight);

    // ---- style chips
    {
        const char* chips[] = { "All", "Glass", "Metal", "Wood", "Paper", "Water", "Voice" };
        const Colour cc[] = { kLight, kWada[0], kWada[1], kWada[2], kWada[3], kWada[2], kWada[0] };
        float x = 253;
        for (int i = 0; i < 7; ++i)
        {
            const juce::Rectangle<float> r (x, 77, 62, 26);
            if (i == 1)
            {
                g.setColour (kWada[0]);
                g.fillRoundedRectangle (r, 13);
                g.setColour (kDark);
            }
            else
            {
                g.setColour (Colour (0xff2f2d2b));
                g.fillRoundedRectangle (r, 13);
                g.setColour (cc[i].withAlpha (0.8f));
                g.drawRoundedRectangle (r, 13, 1.3f);
                g.setColour (kLight);
            }
            g.setFont (font (reg, 14));
            g.drawText (chips[i], r, juce::Justification::centred);
            x += 72;
        }
    }

    // ---- ring (library map): recessed bowl; the ring and the check screen are drawn at 80 % (kSlotScale)
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (kSlotScale, kSlotScale, kCx, kCy).translated (0.0f, kCentreY - kCy));
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillEllipse (kCx - kRingR + 4, kCy - kRingR + 5, 2 * kRingR, 2 * kRingR);
    g.setGradientFill (ColourGradient (Colour (0xff141312), kCx - 120, kCy - 150, Colour (0xff2a2826), kCx + 200, kCy + 230, true));
    g.fillEllipse (kCx - kRingR, kCy - kRingR, 2 * kRingR, 2 * kRingR);

    {
        juce::Random rnd (42);
        for (int i = 0; i < 70; ++i)
        {
            const float a = rnd.nextFloat() * juce::MathConstants<float>::twoPi;
            const float rr = std::sqrt (rnd.nextFloat()) * (kRingR - 18.0f);
            sphere (g, kCx + std::cos (a) * rr, kCy + std::sin (a) * rr, 3.8f + rnd.nextFloat() * 4.2f, kWada[i % 5]);
        }
        g.setColour (kAcc);
        g.drawEllipse (640 - 17, 290 - 17, 34, 34, 1.5f);
        g.setColour (kAcc.withAlpha (0.18f));
        g.fillEllipse (640 - 26, 290 - 26, 52, 52);
        g.setColour (kAcc);
        g.fillEllipse (640 - 6.5f, 290 - 6.5f, 13, 13);
    }

    // ---- centre screen
    g.setGradientFill (ColourGradient (kAcc.withAlpha (0.10f), kCx, kCy, kAcc.withAlpha (0.0f), kCx + 150, kCy, true));
    g.fillEllipse (kCx - 150, kCy - 150, 300, 300);
    shadowCircle (g, kCx, kCy, kScreenR, 0.6f, 26, { 9, 14 });
    g.setGradientFill (ColourGradient (Colour (0xff3a3836), kCx - 70, kCy - 80, Colour (0xff222120), kCx + 80, kCy + 100, false));
    g.fillEllipse (kCx - kScreenR, kCy - kScreenR, 2 * kScreenR, 2 * kScreenR);
    g.setGradientFill (ColourGradient (Colour (0xff181716), kCx - 40, kCy - 60, Colour (0xff201f1d), kCx + 60, kCy + 70, false));
    g.fillEllipse (kCx - 94, kCy - 94, 188, 188);

    const auto sel = proc.getSample (selectedSlot);
    g.setColour (kMuted);
    g.setFont (font (reg, 13));
    g.drawText (message.isNotEmpty() ? message : juce::String (juce::CharPointer_UTF8 ("Check \xc2\xb7 slot ")) + juce::String (selectedSlot + 1), juce::Rectangle<float> (kCx - 80, kCy - 88, 160, 18), juce::Justification::centred);
    {
        const auto pk = peaksOf (sel.get());
        if (pk.size() > 0)
            realWave (g, kCx, kCy - 52, 114, 10.0f, pk);
    }
    const char* rows[] = { "Noise floor", "Hum", "Clipping" };
    juce::String vals[3] = { "-", "-", "-" };
    bool good[3] = { false, false, false };
    if (sel != nullptr && ! sel->analysis.valid)
        vals[0] = "analysing...";
    else if (sel != nullptr)
    {
        const auto& an = sel->analysis;
        vals[0] = juce::String (an.noiseFloorDb, 0) + " dB";
        good[0] = an.noiseFloorDb <= -45.0f;
        if (an.humHz > 0) vals[1] = juce::String (an.humHz) + " Hz, " + juce::String (an.humHarmonics) + " harm.";
        else              { vals[1] = "none"; good[1] = true; }
        if (an.clippedSpots > 0) vals[2] = juce::String (an.clippedSpots) + " spots";
        else                     { vals[2] = "none"; good[2] = true; }
    }
    for (int i = 0; i < 3; ++i)
    {
        const float y = kCy - 12 + (float) i * 22.0f;
        g.setColour (kAcc);
        if (good[i]) g.fillEllipse (kCx - 80, y - 5, 10, 10);
        else         g.drawEllipse (kCx - 79.5f, y - 4.5f, 9, 9, 1.3f);
        g.setColour (kLight);
        g.setFont (font (reg, 14));
        g.drawText (rows[i], juce::Rectangle<float> (kCx - 64, y - 9, 90, 18), juce::Justification::centredLeft);
        g.setColour (kMuted);
        g.setFont (font (reg, 12));
        g.drawText (vals[i], juce::Rectangle<float> (kCx - 4, y - 9, 84, 18), juce::Justification::centredRight);
    }
    g.setColour (kMuted.withAlpha (0.8f));
    g.setFont (font (reg, 11));
    g.drawText (proc.hasRoomTone() ? "room tone: recorded" : "room tone: automatic", juce::Rectangle<float> (kCx - 70, kCy + 58, 140, 16), juce::Justification::centred);

    g.restoreState();

    // ---- four slots: each one holds its own sound
    const float p3[] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.2f, 0.7f };
    for (int slot = 0; slot < 4; ++slot)
    {
        const auto smp = proc.getSample (slot);
        const auto peaks = peaksOf (smp.get());
        g.saveState();
        g.addTransform (juce::AffineTransform::scale (kSlotScale, kSlotScale, kSlotCx[slot], kSlotCy[slot]));
        slotDisc (g, reg, bld, kSlotCx[slot], kSlotCy[slot], slot + 1, smp != nullptr ? smp->name : juce::String ("Empty"),
                  smp == nullptr, "load WAV", &peaks, 0, slot == selectedSlot);
        g.restoreState();
    }
    (void) p3;

    // ---- module panel: nine modules in one panel (3 x 3)
    shadowRect (g, { kGridX, kGridY, kGridW, kGridH }, 16, 0.55f, 26, { 10, 14 });
    g.setGradientFill (ColourGradient (Colour (0xff393735), kGridX, kGridY, Colour (0xff242220), kGridX + kGridW, kGridY + kGridH, false));
    g.fillRoundedRectangle (kGridX, kGridY, kGridW, kGridH, 16);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (kGridX + 0.5f, kGridY + 0.5f, kGridW - 1, kGridH - 1, 16, 1.2f);
    for (int m = 0; m < kNumModules; ++m)
    {
        const auto r = moduleRect (m);
        const Colour col (kModules[m].colour);
        g.setColour (Colour (0xff2b2927));
        g.fillRoundedRectangle (r, 12);
        g.setColour (col);
        g.fillRoundedRectangle (r.getX() + 12, r.getY(), r.getWidth() - 24, 2.0f, 1.0f);
        g.setFont (font (bld, 14));
        g.drawText (kModules[m].title, juce::Rectangle<float> (r.getX() + 12, r.getY() + 6, 110, 20), juce::Justification::centredLeft);
        g.setColour (kMuted);
        g.setFont (font (reg, 11));
        g.drawText (juce::String (juce::CharPointer_UTF8 (kModules[m].sub)), juce::Rectangle<float> (r.getX() + 100, r.getY() + 6, r.getWidth() - 112, 20),
                    juce::Justification::centredRight);
    }
}
