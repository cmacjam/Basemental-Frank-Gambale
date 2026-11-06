#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "TunerPanel.h"

// Vintage boutique look: brushed-brass faceplate in an oxblood tolex frame,
// black bakelite chicken-head knobs, engraved serif lettering, jewel lamps.
class AmpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmpLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

// Square rocker power switch: "I" side pressed = on
class RockerSwitch : public juce::Button
{
public:
    RockerSwitch() : juce::Button ("Power") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// Chrome stomp footswitch with a status LED (click to toggle)
class FootswitchButton : public juce::Button
{
public:
    FootswitchButton() : juce::Button ("Reverb") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// Analogue VU meter: 0 VU = -18 dBFS, VU ballistics (~300 ms), light purple
// face with white lettering, latching red peak light (click to reset)
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LevelMeter (juce::String l) : label (std::move (l)) {}
    void push (float rmsLinear, float peakLinear, double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clipped = false; repaint(); }
private:
    juce::String label;
    float needle = 0.0f;      // 0..1 across the scale
    bool clipped = false;
};

// FET Stack panel: two FET compressors in series + Class-A drive (opens over the amp)
class CompPanel : public juce::Component, private juce::Timer
{
public:
    explicit CompPanel (Fusion250Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
private:
    void timerCallback() override;
    Fusion250Processor& proc;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    struct K { std::unique_ptr<juce::Slider> s; std::unique_ptr<SA> a; juce::String label; };
    std::vector<K> knobs;
    FootswitchButton onBtn;
    juce::TextButton stackBtn { "STACKED" }, ratioBtns[4];
    std::unique_ptr<BA> onAtt, stackAtt;
    float gr1 = 0, gr2 = 0;   // displayed gain reduction (dB), with release ballistics
};

class Fusion250Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Fusion250Editor (Fusion250Processor&);
    ~Fusion250Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Slider& makeKnob (const char* paramId, const juce::String& label, bool small = false);
    void rebuildTextures();

    Fusion250Processor& proc;
    AmpLookAndFeel lnf;

    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::String label;
        bool small = false;
    };
    std::vector<Knob> knobs;     // 0-4 lead, 5 reverb, 6-10 clean, 11 input, 12 master

    juce::TextButton channelBtn { "CH" }, padBtn { "-6 dB" }, irBtn { "Load IR..." }, tunerBtn { "TUNER" };
    FootswitchButton reverbBtn;
    CompPanel compPanel;
    juce::TextButton compBtn { "COMP" };
    TunerPanel tuner;
    RockerSwitch powerBtn;
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    juce::ComboBox guitarBox, outputBox, presetBox, cabBox, micBox;
    juce::Label irLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> revAtt, padAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> guitarAtt, outputAtt, cabAtt, micAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> header, plate, leadBand, cleanBand, footer, guitarGroup, speakerGroup, levelGroup;
    juce::Image brushed, tolex;
    bool leadOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fusion250Editor)
};
