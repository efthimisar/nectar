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
// Square pad of a pair joystick (pair 0 = samples 1+2, pair 1 = samples 3+4): the stick blends the four corner settings.
// It moves only while the mouse holds it and stays where it is let go.
class PairPad : public juce::Component
{
public:
    static constexpr float kHalf = 70.0f; // radius of the round pad in design units
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

// Round XY pad for the joystick modulation source; right-click clears the recorded path.
class JoyPad : public juce::Component
{
public:
    static constexpr float kR = 22.0f; // radius in design units
    explicit JoyPad (NektarProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void setFromMouse (const juce::MouseEvent&);
    NektarProcessor& proc;
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
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
    std::unique_ptr<PairPad> pairPads[2];
    bool detailsView = false;           // false = the joystick face, true = the panel with all the knobs
    void pressNew (int pair);
    void pressBack (int pair);
    void pressKeep();
    std::unique_ptr<SpacePad> spacePad;
    std::unique_ptr<LibraryMap> libMap;
    int chipPage = 0;                   // which categories the chips above the map show (when there are more than six)
    juce::Rectangle<float> chipRect (int i) const;
    juce::Point<float> joyPadCentre;
    juce::String message;
    std::set<int> heldNotes;
    int octaveShift = 0;
    int selectedSlot = 0;
    void drawFace (juce::Graphics& g);
    void faceClick (const juce::MouseEvent& e, juce::Point<float> p);
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int partDrag = -1;                  // the sample screen: the slot whose part is being dragged
    float partFrom = 0.0f;
    std::vector<float> scrPeaks;        // the sample screen's waveform (whole file), and which sound it is
    int scrSerial = -1;
    struct Spark { float x, y, life; };
    std::vector<Spark> sparks;          // grains as they are read
    std::array<int, 4> sparkSeen {};
    std::array<float, 4> levelShown {};
    float mixShown = 0.0f;
    int picking = -1;                   // main screen: the joystick whose movement shapes are shown over its pad (-1 = none)
    bool topBarClick (juce::Point<float> p);
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int spaceSlot = 0;                  // main screen: the sound whose movement the Space chips and Speed knob show
    juce::String spaceSpeedId;          // kept alive for the Speed knob's parameter id
    std::set<int> selection { 0 };
    int fixedKnobs = 0;                 // knobs that are always there (top bar); the rest belong to the panel
    std::map<juce::String, float> lastPanel; // last seen values of the first selected sample (to copy a change to the others)
    juce::String overlayKey;
    bool panelGrains = false;
    bool panelClean = false;            // the Clean knob is shown only when the recording has a background noise to learn
    bool noiseFound (int slot) const { const auto sm = proc.getSample (slot); return sm != nullptr && sm->analysis.valid && sm->analysis.noiseFloorDb > -95.0f; }
    int panelMix = -1;                  // which Master effects are open (bit per effect); a closed one shows only its Mix knob
    int mixMask() const;
    bool panelHum = false;              // the Hum knob is shown only when mains hum was found in the sample
    bool humFound (int slot) const { const auto sm = proc.getSample (slot); return sm != nullptr && sm->analysis.valid && sm->analysis.humHz > 0.0; }
    int pairIndex() const { if (masterView || selection.size() != 2) return -1; if (selection.count (0) && selection.count (1)) return 0; if (selection.count (2) && selection.count (3)) return 1; return -1; }
    juce::Rectangle<float> screenArea() const;
    void rebuildPanel();
    void drawOverlay (juce::Graphics&);
    juce::String slotId (const char* base, int slot) const { return slot == 0 ? juce::String (base) : juce::String (base) + juce::String (slot + 1); }
    int primarySlot() const { return selection.empty() ? 0 : *selection.begin(); }
    bool masterView = false;
    bool motionView = false;            // third view: LFOs, envelope, slicer (masterView is also true then)
    int resampleArmedSlot = -1;  // Resample asks once before it replaces a sound
    int resampleArmTicks = 0;
    bool hudWasRecording = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NektarEditor)
};
