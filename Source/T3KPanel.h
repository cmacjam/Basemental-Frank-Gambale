#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "T3K/AuthFlow.h"

// TONE3000 panel (opens over the amp like COMP and TUNER):
//   AMP      FG 250 channels, or a NAM capture (Browse TONE3000 / Load .nam)
//   CABINET  FG 250 cabinet, or an IR (Browse TONE3000 / Load IR)
// plus the TONE3000 account (connect / disconnect).
class T3KPanel : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit T3KPanel (Fusion250Processor&);
    ~T3KPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    /// True when this build has a TONE3000 publishable key (Browse needs one).
    static bool browseAvailable();

private:
    enum class Slot { amp, cab };
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void browse (Slot);
    void loadTone (Slot, int toneId);
    void pickModel (Slot, const t3k::ToneWithModels&);
    void downloadAndLoad (Slot, const t3k::Tone&, const t3k::Model&);
    void loadFile (Slot);
    void setStatus (const juce::String&, bool error = false);
    void refreshAccount();

    Fusion250Processor& proc;
    t3k::AsyncScope scope;
    std::unique_ptr<t3k::AuthFlow> flow;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton accountBtn { "CONNECT" };
    juce::TextButton ampFg { "FG 250" }, ampNam { "NAM CAPTURE" }, cabFg { "FG 250 CAB" }, cabIr { "IR" };
    juce::TextButton ampBrowse { "BROWSE TONE3000" }, ampFile { "LOAD .NAM" }, ampClear { "CLEAR" };
    juce::TextButton cabBrowse { "BROWSE TONE3000" }, cabFile { "LOAD IR" }, cabClear { "CLEAR" };
    juce::TextButton liteBtn { "LITE" }, normBtn { "NORMALIZE" };
    juce::HyperlinkButton ampLink, cabLink;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> normAtt;

    juce::String status, accountName;
    bool statusIsError = false, busy = false;
};
