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

    // FET Stack presets: compressor settings only (the amp is left alone)
    struct CompSettings { float input, attackUs, releaseMs; int ratioIdx; float knee; bool stacked, autoRelease, lineDrive; float thru, drive, dry, output; };
    struct CompPreset { const char* name; CompSettings s; };
    std::vector<CompPreset> compPresets;
    void applyCompPreset (int index);
    int currentCompPreset = -1;

private:
    class TunerThread;
    friend class TunerThread;

    fusion::AmpCore amp;
    juce::dsp::Convolution convolution;
    juce::File irFile;
    juce::AudioBuffer<float> monoIn, stereoTmp;
    int currentPreset = 0;
    float powerGain = 1.0f;
public:
    // level meters: peak since the GUI last read (linear)
    float takeInputPeak()  { return inPeak.exchange (0.0f); }
    float takeOutputPeak() { return outPeak.exchange (0.0f); }
    // mean-square level since the GUI last read (for VU ballistics)
    float takeInputRms()  { return takeRms (inSum, inCount); }
    // deepest FET Stack gain reduction (dB, <= 0) since the GUI last read
    float takeCompGr1() { return compGr1.exchange (0.0f); }
    float takeCompGr2() { return compGr2.exchange (0.0f); }
    float takeOutputRms() { return takeRms (outSum, outCount); }
private:
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, inSum { 0.0f }, outSum { 0.0f };
    std::atomic<int> inCount { 0 }, outCount { 0 };
    std::atomic<float> compGr1 { 0.0f }, compGr2 { 0.0f };
    static void atomicMin (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v < cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
    static void atomicAdd (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (! a.compare_exchange_weak (cur, cur + v, std::memory_order_relaxed)) {}
    }
    static float takeRms (std::atomic<float>& sum, std::atomic<int>& count)
    {
        const int c = count.exchange (0);
        const float s = sum.exchange (0.0f);
        return c > 0 ? std::sqrt (s / (float) c) : 0.0f;
    }
    static void atomicMax (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }

    fusion::SampleRing tunerRing { 18 };
    std::atomic<bool> tunerOpen { false };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::unique_ptr<TunerThread> tunerThread;
    mutable juce::SpinLock tunerLock;
    fusion::TunerResult tunerResult;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fusion250Processor)
};
