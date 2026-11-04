#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Strobe tuner overlay: an eight-octave strobe display (the band for the
// note's octave shows a crisp pattern, finer patterns above it, blur below),
// readout to 0.1 cent, auto/manual note selection, transposing keys,
// global cent offset, historical temperaments and mute.
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
    int keyShift() const;
    bool manualMode() const;

    Fusion250Processor& proc;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::Slider refSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Slider offsetSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label refLabel, offsetLabel;
    juce::ComboBox modeBox, keyBox, tempBox, rootBox, tuningBox;
    juce::ToggleButton muteBtn { "Mute" };
    juce::TextButton noteBtns[12];
    std::unique_ptr<SA> refAtt, offsetAtt;
    std::unique_ptr<BA> muteAtt;
    std::unique_ptr<CA> modeAtt, keyAtt, tempAtt, rootAtt, tuningAtt;

    fusion::TunerResult shown;
    double smoothCents = 0, smoothDelta = 0, strobePhase = 0;
    double lastTime = 0, lastValidTime = -1e9;
    bool hasReading = false;
    juce::Rectangle<int> readoutArea, strobeArea, bottomLeft;
};
