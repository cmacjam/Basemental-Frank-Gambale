#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "AmpCore.h"
#include "StrobeTuner.h"

class Fusion250Processor : public juce::AudioProcessor
{
public:
    Fusion250Processor();
    ~Fusion250Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return (int) presets.size(); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool loadImpulseResponse (const juce::File& file);
    juce::String getIrName() const { return irFile.existsAsFile() ? irFile.getFileNameWithoutExtension() : juce::String(); }

    // ---- tuner (driven by the editor) -----------------------------------
    void setTunerOpen (bool open);
    bool isTunerOpen() const { return tunerOpen.load(); }
    fusion::TunerResult getTunerResult() const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };
    std::vector<Preset> presets;

private:
    class TunerThread;
    friend class TunerThread;

    fusion::AmpCore amp;
    juce::dsp::Convolution convolution;
    juce::File irFile;
    juce::AudioBuffer<float> monoIn, stereoTmp;
    int currentPreset = 0;
    float powerGain = 1.0f;

    fusion::SampleRing tunerRing { 18 };
    std::atomic<bool> tunerOpen { false };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::unique_ptr<TunerThread> tunerThread;
    mutable juce::SpinLock tunerLock;
    fusion::TunerResult tunerResult;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fusion250Processor)
};
