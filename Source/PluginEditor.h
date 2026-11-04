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

    juce::TextButton channelBtn { "CH" }, reverbBtn { "REVERB" }, padBtn { "-6 dB" }, irBtn { "Load IR..." }, tunerBtn { "TUNER" };
    TunerPanel tuner;
    juce::ComboBox guitarBox, outputBox, presetBox;
    juce::Label irLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> revAtt, padAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> guitarAtt, outputAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> header, plate, leadBand, cleanBand, footer;
    juce::Image brushed, tolex;
    bool leadOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fusion250Editor)
};
