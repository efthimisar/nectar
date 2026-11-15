#pragma once
#include <set>
#include "PluginProcessor.h"

namespace nektar
{
// Flat "liquid" knob (the circle fills from the bottom). Drawn in design units (1000x1000 space).
void drawKnob (juce::Graphics& g, const juce::Typeface::Ptr& tf, float cx, float cy, float r, float p,
               const juce::String& label, bool dim = false, float fontSize = 12.0f);
}

// One knob on screen. Linked to a parameter it can be dragged (double-click = reset);
// without a parameter it only shows a fixed value (not wired up yet).
class KnobComponent : public juce::Component
{
public:
    KnobComponent (juce::Typeface::Ptr tf, float radius, juce::String label, bool dim, float fixedValue,
                   juce::RangedAudioParameter* param = nullptr, bool isToggle = false);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Lets the knob follow a modulation source (right mouse button: choose the source; drag with it: amount).
    void setModulation (NektarProcessor* p) { modProc = p; }

    static constexpr float kWidth = 90.0f; // design units
    float height() const { return 2.0f * radius + 20.0f; }

private:
    juce::Typeface::Ptr typeface;
    float radius;
    juce::String text;
    bool dimmed;
    float fixed;
    juce::RangedAudioParameter* parameter;
    bool toggle;
    float dragStart = 0.0f;
    NektarProcessor* modProc = nullptr;
    bool rightDrag = false;
    float amountStart = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Live parts of the top bar: timer, progress bar and the Record button (they change while recording).
// Round XY pad for the joystick modulation source; right-click clears the recorded path.
class JoyPad : public juce::Component
{
public:
    static constexpr float kR = 27.0f; // radius in design units
    explicit JoyPad (NektarProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void setFromMouse (const juce::MouseEvent&);
    NektarProcessor& proc;
};

// Top view of the listener with the four slots as numbered dots: direction = angle, distance = radius.
class SpacePad : public juce::Component
{
public:
    explicit SpacePad (NektarProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void setDot (int slot, juce::Point<float> pos);
    NektarProcessor& proc;
    int dragging = -1;
};

class RecordHud : public juce::Component
{
public:
    RecordHud (NektarProcessor& p, juce::Typeface::Ptr tf) : proc (p), typeface (std::move (tf)) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
private:
    NektarProcessor& proc;
    juce::Typeface::Ptr typeface;
};

class NektarEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit NektarEditor (NektarProcessor&);
    ~NektarEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void toggleRecording();
    void toggleRoomTone();
    void toggleResample();
    void showPresetMenu();
    void say (const juce::String& text);
    void renderDesign (juce::Graphics&);
    void addGrain (juce::Image&);
    void chooseFile (int slot);
    void loadFile (const juce::File&, int slot);
    int slotAt (juce::Point<float> designPos) const; // -1 = none
    void selectSlot (int slot);
    void releaseAllKeys();
    void addKnob (float cx, float cy, float r, const juce::String& label, bool dim, float fixed,
                  const char* paramId = nullptr, bool toggle = false);

    NektarProcessor& proc;
    juce::Typeface::Ptr regular, bold;
    juce::Image cache;
    juce::ComponentBoundsConstrainer constrainer;
    juce::OwnedArray<KnobComponent> knobs;
    juce::Array<juce::Point<float>> knobPos; // centre of each knob in design units
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<RecordHud> hud;
    std::unique_ptr<JoyPad> joyPad;
    std::unique_ptr<SpacePad> spacePad;
    juce::Point<float> joyPadCentre;
    juce::String message;
    std::set<int> heldNotes;
    int octaveShift = 0;
    int selectedSlot = 0;
    int resampleArmedSlot = -1;  // Resample asks once before it replaces a sound
    int resampleArmTicks = 0;
    bool hudWasRecording = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NektarEditor)
};
