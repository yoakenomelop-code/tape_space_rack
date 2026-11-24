#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

class TapeSpaceRackEditor;

namespace ts::ui
{
namespace col   // same values as the web version
{
    const juce::Colour bg { 0xff15171b }, panel { 0xff1f2228 }, panel2 { 0xff272b33 }, line { 0xff353a44 },
        ink { 0xffe7e9ee }, inkDim { 0xff9097a5 }, accent { 0xffe8b04a },
        tape { 0xfff08a3c }, motion { 0xffd46fd0 }, time { 0xff4cb6d6 }, delay { 0xff5fd0b0 }, eq { 0xff7fa8ff }, rev { 0xffb98ae0 },
        analog { 0xffd8c58f }, mix { 0xff4fbf86 }, good { 0xff4fbf86 }, warn { 0xffe8b04a }, bad { 0xffe0574f };
}
enum HL { hlNone = -1, hlTape = 0, hlMotion, hlTime, hlDelay, hlEq, hlRev, hlMix, hlAnalog };
juce::Colour hlColour (int h);

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
};

struct HoverSink { virtual ~HoverSink() = default; virtual void hover (int slot, int hl, const juce::String& text) = 0; };

class VKnob : public juce::Component
{
public:
    VKnob (const juce::String& caption, int slot, int hl, HoverSink& sink);
    void setList (int n);
    void setLin (double lo, double hi, double step);
    void setLog (double lo, double hi);
    std::function<double()> get;
    std::function<void (double)> set;
    std::function<juce::String()> text, describe;
    double def = 0;
    void sync();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::Slider slider;
    int slot, band;
    // clicking the value text: open a picker, or type an exact number (plain units)
    std::function<void()> onValueClick;
    std::function<double()> plainGet; std::function<void (double)> plainSet;
    double plainLo = 0, plainHi = 1; bool msUnit = false;
private:
    juce::String caption; HoverSink& sink; bool syncing = false;
    std::unique_ptr<juce::TextEditor> entry;
    void finishEntry (bool apply);
};

// grid of the 79 tempo-sync note values (same layout as the web version)
class SyncPicker : public juce::Component
{
public:
    SyncPicker();
    void open (const juce::String& title, std::function<int()> get, std::function<void (int)> set, std::function<double()> bpm);
    void close();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
private:
    static constexpr int kRows = 20;
    juce::Rectangle<int> box, head, grid, foot;
    juce::Rectangle<int> cell (int i) const;
    int hit (juce::Point<int>) const;
    void mark (int i);
    void pick (int i);
    void filter();
    juce::String title; std::function<int()> getter; std::function<void (int)> setter; std::function<double()> bpmFn;
    int cur = 0; std::vector<bool> match;
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> jumps;
    juce::TextButton closeBtn { juce::CharPointer_UTF8 ("\xc3\x97") };
};

class Seg : public juce::Component
{
public:
    Seg (const juce::String& caption, const juce::StringArray& names, int slot, int hl, HoverSink& sink);
    std::function<int()> get; std::function<void (int)> set; std::function<juce::String (int)> describe;
    void sync();
    void resized() override;
    void paint (juce::Graphics&) override;
    int preferredWidth() const;
private:
    juce::String caption; juce::OwnedArray<juce::TextButton> buttons; int slot, band; HoverSink& sink;
};

// a caption with a combo box or a text + button (impulse selection)
class IRBox : public juce::Component
{
public:
    IRBox (const juce::String& caption) : caption (caption) {}
    void resized() override;
    void paint (juce::Graphics& g) override;
    juce::ComboBox box; juce::TextButton button; juce::String text, caption;
};

// ---------------------------------------------------------------- previews under each module
class View : public juce::Component
{
public:
    enum Kind { tapeTransfer, tapeResp, echo, revEnv, revRT };
    View (TapeSpaceRackProcessor& p, int slot, Kind k, HoverSink& s) : proc (p), slot (slot), kind (k), sink (s) {}
    void paint (juce::Graphics&) override;
    void refresh (float liveIn);
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { sink.hover (-1, hlNone, {}); }
    bool lit = false;
private:
    TapeSpaceRackProcessor& proc; int slot; Kind kind; HoverSink& sink;
    Params last; double lastBpm = 0; int lastSerial = -1; bool valid = false, dirty = true; juce::uint32 lastCalc = 0;
    float live = 0;
    std::vector<double> a, b, c;            // curve data (meaning depends on kind)
    std::vector<std::pair<float, float>> wave;
    double dur = 0, ms = 0, extra = 0;
    bool usesIR = false;
};
class HistView : public juce::Component
{
public:
    void push (float in, float out, float wet) { ha[(size_t) pos] = in; hb[(size_t) pos] = out; hc[(size_t) pos] = wet; pos = (pos + 1) % N; repaint(); }
    void paint (juce::Graphics&) override;
    int type = 0;
private:
    static constexpr int N = 150;
    std::array<float, N> ha {}, hb {}, hc {}; int pos = 0;
};
class HarmDisplay : public juce::Component
{
public:
    HarmDisplay (TapeSpaceRackProcessor& p, int slot) : proc (p), slot (slot) {}
    void paint (juce::Graphics&) override;
    void refresh();
private:
    TapeSpaceRackProcessor& proc; int slot; Params last; bool valid = false; juce::uint32 lastCalc = 0;
    Harm h100 {}, h1k {};
};

// ---------------------------------------------------------------- one module
class ModulePanel : public juce::Component
{
public:
    ModulePanel (::TapeSpaceRackEditor&, TapeSpaceRackProcessor&, HoverSink&, int slot, int type);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void tick();
    int heightFor (int width) const;
    const int slot, type;
    static constexpr int railW = 44, headH = 40, rowH = 108, dynH = 136, circuitH = 172;
    juce::TextButton power { "ON" }, up { juce::CharPointer_UTF8 ("\xe2\x96\xb2") }, down { juce::CharPointer_UTF8 ("\xe2\x96\xbc") };
private:
    struct Group { juce::String name; int hl; std::vector<juce::Component*> items; std::vector<int> widths; juce::Rectangle<int> r; };
    void build();
    void clearControls();
    juce::String structureKey() const;
    Params P() const { return proc.reader.params (slot); }
    double get (const char* k) const { return proc.getPlain (slot, k); }
    void set (const char* k, double v) { proc.setPlain (slot, k, v); }
    void group (const juce::String& name, int hl);
    void addItem (juce::Component* c, int width);
    VKnob* knob (const juce::String& label, int hl);
    VKnob* lin (const char* key, const juce::String& label, double lo, double hi, double step, std::function<juce::String (double)> fmt, int hl, std::function<juce::String()> desc);
    VKnob* logKnob (const char* key, const juce::String& label, double lo, double hi, std::function<juce::String (double)> fmt, int hl, std::function<juce::String()> desc);
    VKnob* listKnob (const juce::String& label, int hl, const juce::StringArray& names, std::function<int()> getI, std::function<void (int)> setI, int def, std::function<juce::String()> desc);
    VKnob* syncKnob (const char* key, const juce::String& label, int hl, const juce::String& title, const juce::String& what);
    Seg* seg (const juce::String& caption, const juce::StringArray& names, int hl, std::function<int()> g, std::function<void (int)> s, std::function<juce::String (int)> desc);
    juce::TextButton* tog (const juce::String& label, int hl, std::function<bool()> g, std::function<void (bool)> s, std::function<juce::String()> desc = {});
    void mixOut (const juce::String& what);
    void wetEq (int hl, const juce::String& where);
    void chooseIR();

    ::TapeSpaceRackEditor& editor; TapeSpaceRackProcessor& proc; HoverSink& sink;
    std::vector<Group> groups;
    juce::OwnedArray<juce::Component> owned;
    std::vector<VKnob*> knobs; std::vector<Seg*> segs; std::vector<std::pair<juce::TextButton*, std::function<bool()>>> togs;
    std::vector<std::function<void()>> syncers;
    juce::String key;
    juce::Rectangle<int> controlsArea, dynArea, circuitArea, lvArea;
    juce::TextButton analogBtn { "ANALOG" }, machineBtn { juce::String::fromUTF8 ("マシン") },
        dupBtn { juce::String::fromUTF8 ("複製") }, delBtn { juce::String::fromUTF8 ("削除") }, circuitReset { juce::String::fromUTF8 ("初期化") };
    juce::ComboBox variant, presetBox;
    int subtitleRight = 0;
    std::vector<std::unique_ptr<View>> views;
    HistView hist;
    float lvI = 0, lvO = 0, lvW = 0;
    std::array<juce::ComboBox, 3> cBox;
    std::vector<std::unique_ptr<VKnob>> cKnobs;
    HarmDisplay harm;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragging = false;
};

class FreqMap : public juce::Component
{
public:
    FreqMap (TapeSpaceRackProcessor&, HoverSink&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { sink.hover (-1, hlNone, {}); }
    void tick();
    int hlSlot = -1, hl = hlNone;
    float range = 24.f;
private:
    static constexpr int NF = 256;
    using Curve = std::array<double, NF>;
    struct SlotCurves { int type = -1; bool on = false; Params p; double bpm = 0; bool valid = false; Curve tone {}; };
    TapeSpaceRackProcessor& proc; HoverSink& sink;
    std::vector<double> freqs;
    std::vector<SlotCurves> slots;
    std::array<float, NF> spec {};
    juce::dsp::FFT fft { TapeSpaceRackProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) TapeSpaceRackProcessor::fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ring = std::vector<float> ((size_t) TapeSpaceRackProcessor::fftSize), fftData = std::vector<float> ((size_t) TapeSpaceRackProcessor::fftSize * 2);
    int ringPos = 0; bool hasSpec = false;
    juce::Rectangle<float> plot() const;
    float xOf (double f) const; float yOf (double db) const;
};
} // namespace ts::ui

class TapeSpaceRackEditor : public juce::AudioProcessorEditor, public ts::ui::HoverSink, private juce::Timer
{
public:
    explicit TapeSpaceRackEditor (TapeSpaceRackProcessor&);
    ~TapeSpaceRackEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void hover (int slot, int hl, const juce::String& text) override;
    void dragPanel (ts::ui::ModulePanel&, int yInRack, bool finished);
    int highlightSlot() const { return hlSlot; }
    int highlightBand() const { return hlBand; }
    void openPicker (const juce::String& title, std::function<int()> get, std::function<void (int)> set)
    { picker.open (title, std::move (get), std::move (set), [this] { return proc.hostBpm.load(); }); }
    bool isMachineOpen (int slot) const { return juce::isPositiveAndBelow (slot, (int) machineOpen.size()) && machineOpen[(size_t) slot]; }
    void setMachineOpen (int slot, bool open) { if (juce::isPositiveAndBelow (slot, (int) machineOpen.size())) { machineOpen[(size_t) slot] = open; layoutRack(); } }
    void layoutRack();
    TapeSpaceRackProcessor& proc;
    void timerCallback() override;
private:
    void rebuildPanels();
    void applyPreset (int index, const juce::String& name);
    ts::ui::Look look;
    std::unique_ptr<ts::ui::FreqMap> map;
    juce::Viewport viewport;
    juce::Component rackContent;
    std::vector<std::unique_ptr<ts::ui::ModulePanel>> panels;
    std::vector<int> signature;
    std::array<bool, (size_t) ts::numSlots()> machineOpen {};
    juce::ComboBox headerPreset;
    juce::Slider bpm;
    juce::OwnedArray<juce::TextButton> addBtns;
    juce::TextButton r24 { juce::String::fromUTF8 ("±24 dB") }, r12 { juce::String::fromUTF8 ("±12 dB") }, r4 { juce::String::fromUTF8 ("±4 dB") };
    juce::String hint;
    int hlSlot = -1, hlBand = ts::ui::hlNone;
    float inLvl = 0, outLvl = 0;
    int dropY = -1;
    juce::Rectangle<int> hintArea, meterArea, toolbarArea, bpmArea;
    ts::ui::SyncPicker picker;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeSpaceRackEditor)
};
