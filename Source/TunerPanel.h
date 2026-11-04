#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Strobe tuner overlay: three strobe bands (fundamental, 2x, 4x) whose
// drift speed is proportional to the pitch error, plus a 0.1-cent readout.
class TunerPanel : public juce::Component, private juce::Timer
{
public:
    explicit TunerPanel (Fusion250Processor&);
    ~TunerPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    std::vector<int> currentTuningNotes() const;

    Fusion250Processor& proc;
    juce::Slider refSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label refLabel;
    juce::ToggleButton muteBtn { "Mute output while tuning" };
    juce::ComboBox tuningBox;
    juce::Label tuningLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> refAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tuningAtt;

    fusion::TunerResult shown;
    double smoothCents = 0, smoothDelta = 0, strobePhase = 0;
    double lastTime = 0, lastValidTime = -10;
    bool hasReading = false;
    juce::Rectangle<int> strobeArea, readoutArea, guideArea;
};
