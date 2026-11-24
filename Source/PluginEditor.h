#pragma once
#include <set>
#include <map>
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
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Square pad of a pair joystick (pair 0 = samples 1+2, pair 1 = samples 3+4): the stick blends the four corner settings.
// It moves only while the mouse holds it and stays where it is let go.
class PairPad : public juce::Component
{
public:
    static constexpr float kHalf = 60.0f; // radius of the round pad in design units
    std::vector<juce::Point<float>> trail; // the last places of the stick (disc, -1..1), for its trail
    PairPad (NektarProcessor& p, int pairIn) : proc (p), pair (pairIn) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override { setFromMouse (e); }
    void mouseUp (const juce::MouseEvent&) override;
private:
    void setFromMouse (const juce::MouseEvent&);
    NektarProcessor& proc;
    int pair;
    bool dragging = false;
};

// The library map around the Space pad: every sound of the chosen category as a slowly drifting dot (see LibraryIndex);
// drag the cursor to a group of sounds (Random sounds picks from there); double-click a dot to load it into the chosen circle.
class LibraryMap : public juce::Component
{
public:
    explicit LibraryMap (NektarProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;
    std::function<void (const juce::File&)> onLoad;
    std::function<void (const juce::String&)> onHover;
private:
    juce::Point<float> toUnit (juce::Point<float> p) const;
    juce::Point<float> toPix (float x, float y) const;
    int dotAt (juce::Point<float> p) const;
    void refresh();
    NektarProcessor& proc;
    std::vector<LibraryIndex::Dot> dots;
    int seenVersion = -1;
    juce::String seenCategory = "\x01";
    int hover = -1;
};

// Top view of the listener with the four slots as numbered dots: direction = angle, distance = radius.
class SpacePad : public juce::Component
{
public:
    explicit SpacePad (NektarProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    std::function<void (int)> onPick;   // a dot was clicked (its sound becomes the one the Space controls show)
    std::function<void (const juce::String&)> onSay;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool overHead = false, overPhones = false;
    int chosen = 0;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void setDot (int slot, juce::Point<float> pos);
    NektarProcessor& proc;
    int dragging = -1;
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void toggleRecording();
    void toggleResample();
    void showPresetMenu();
    void fxMenu (int i);                 // the menu of one effect slot in the top bar
    void openFxWindow (int i);
    struct FxWindow : juce::DocumentWindow
    {
        FxWindow (const juce::String& t) : juce::DocumentWindow (t, juce::Colours::black, juce::DocumentWindow::closeButton) {}
        void closeButtonPressed() override { setVisible (false); }
    };
    std::array<std::unique_ptr<FxWindow>, 2> fxWindows;
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
    std::unique_ptr<PairPad> pairPads[2];
    void pressKeep();
    std::unique_ptr<SpacePad> spacePad;
    std::unique_ptr<LibraryMap> libMap;
    int chipPage = 0;                   // which categories the chips above the map show (when there are more than six)
    juce::String message;
    std::set<int> heldNotes;
    int octaveShift = 0;
    int selectedSlot = 0;
    void drawFace (juce::Graphics& g);
    void drawSpaceOval (juce::Graphics& g, juce::Rectangle<float> r);
    void spaceOvalClick (const juce::MouseEvent& e);
    void mouseMove (const juce::MouseEvent&) override;
    struct Overlay : juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        std::function<void (const juce::MouseEvent&)> down;
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
        void mouseDown (const juce::MouseEvent& e) override { if (down) down (e); }
    };
    std::unique_ptr<Overlay> spaceOval;
    std::array<std::array<float, 4>, 300> river {};
    std::array<std::array<float, 4>, 300> riverPan {}; // -1 right .. 1 left, per sound
    float outShown = 0.0f; int clipHold = 0;  // the four sounds' levels, newest last (about 10 s)
    std::array<bool, 300> riverBeat {};
    int riverAt = 0;
    float lastBeat = 0.0f;
    int voiceDrag = 0;                  // the shared voice: 1 = its pad, 2 = its Intensity bar
    void voiceSet (juce::Point<float> p);
    int arcDrag = -1;                   // which of Motion / Weight / Light is being dragged
    int charPanel = -1;                 // the characters' panel open for joystick 0 or 1 (-1: closed)
    int chkDrag = -1;                   // a knob of the panel being dragged: character * 3 + knob (1000 + character: its share)
    int panelSnd = -1;                  // the sound the panel shows, when chosen in the panel itself
    int panelSound() const;
    float chkFrom = 0.0f, chkY = 0.0f;
    bool partOnBench = false, partMoved = false;
    float benchGet (int idx, int snd) const;
    void benchSet (int idx, int snd, float v);
    void setBenchOpen (int pair);
    void drawCharPanel (juce::Graphics& g);
    bool charPanelClick (juce::Point<float> p);
    float arcFrom = 0.5f, arcY = 0.0f;
    juce::String hoverSaid;
    void faceClick (const juce::MouseEvent& e, juce::Point<float> p);
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int hoverRing = -1; // which of the six rings the mouse is on (0..2 whole sound, 3..5 chosen sound)
    bool recAll = true; // all four chosen: Rec and Resample make a new sound in the library
    int partDrag = -1;                  // the sample screen: the slot whose part is being dragged
    float partFrom = 0.0f;
    struct Spark { float x, y, life; };
    std::vector<Spark> sparks;          // grains as they are read
    std::array<int, 4> sparkSeen {};
    std::array<float, 4> levelShown {};
    bool topBarClick (juce::Point<float> p);
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int spaceSlot = 0;                  // main screen: the sound whose movement the Space chips and Speed knob show
    juce::String spaceSpeedId;          // kept alive for the Speed knob's parameter id
    int fixedKnobs = 0;                 // knobs that are always there (top bar); the rest are rebuilt (the space speed)
    void rebuildPanel();
    juce::String slotId (const char* base, int slot) const { return slot == 0 ? juce::String (base) : juce::String (base) + juce::String (slot + 1); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NektarEditor)
};
