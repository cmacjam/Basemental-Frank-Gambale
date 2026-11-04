#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ID
{
    static const char* chan = "channel";
    static const char* clean[5] = { "c_gain", "c_low", "c_mid", "c_high", "c_level" };
    static const char* lead[5]  = { "l_gain", "l_low", "l_mid", "l_high", "l_level" };
    static const char* reverb = "reverb";
    static const char* revOn = "reverb_on";
    static const char* guitar = "guitar";
    static const char* output = "output";
    static const char* pad = "preout_pad";
    static const char* inTrim = "input_trim";
    static const char* outLvl = "output_level";
    static const char* tunerRef = "tuner_ref";
    static const char* tunerMute = "tuner_mute";
    static const char* tunerTuning = "tuner_tuning";
}

// =========================================================== tuner thread
class Fusion250Processor::TunerThread : public juce::Thread
{
public:
    explicit TunerThread (Fusion250Processor& p) : juce::Thread ("Fusion250 Strobe Tuner"), owner (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            const double fs = owner.currentSampleRate.load();
            const int want = std::min (owner.tunerRing.capacity(), (int) (fs * 1.3));
            if ((int) scratch.size() < want) scratch.resize ((size_t) want);
            const int got = owner.tunerRing.readLatest (scratch.data(), want);
            const double refA = owner.apvts.getRawParameterValue (ID::tunerRef)->load();
            const auto r = core.analyse (scratch.data(), got, fs, refA);
            {
                const juce::SpinLock::ScopedLockType sl (owner.tunerLock);
                owner.tunerResult = r;
            }
            wait (40);   // ~25 readings per second
        }
    }

private:
    Fusion250Processor& owner;
    fusion::StrobeTunerCore core;
    std::vector<float> scratch;
};

// ============================================================ parameters
juce::AudioProcessorValueTreeState::ParameterLayout Fusion250Processor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const NormalisableRange<float> knob (0.0f, 10.0f, 0.01f);
    const char* names[5] = { "Gain", "Low", "Mid", "High", "Level" };
    const float cleanDef[5] = { 4, 5, 5, 5, 5 };
    const float leadDef[5]  = { 6, 4.5f, 6.5f, 5, 5 };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::chan, 1 }, "Channel", StringArray { "Clean", "Lead" }, 0));
    for (int i = 0; i < 5; ++i)
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::clean[i], 1 }, String ("Clean ") + names[i], knob, cleanDef[i]));
    for (int i = 0; i < 5; ++i)
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::lead[i], 1 }, String ("Lead ") + names[i], knob, leadDef[i]));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::reverb, 1 }, "Reverb", knob, 2.5f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ID::revOn, 1 }, "Reverb On", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::guitar, 1 }, "Guitar Voice",
                 StringArray { "Off (your guitar)", "FG1 Neck", "FG1 Bridge", "FG2" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::output, 1 }, "Output",
                 StringArray { "Pre Out (no cab)", "Built-in Cab", "User IR" }, 1));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ID::pad, 1 }, "Pre Out -6 dB", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::inTrim, 1 }, "Input Trim",
                 NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::outLvl, 1 }, "Master",
                 NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    // tuner
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::tunerRef, 1 }, "Tuner Reference A",
                 NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ID::tunerMute, 1 }, "Tuner Mute", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::tunerTuning, 1 }, "Tuner String Guide",
                 StringArray { "Chromatic", "Standard (E A D G B E)", "Gambale (A D G C E A)", "Half Step Down", "Drop D", "7-String (B E A D G B E)" }, 2));
    return { p.begin(), p.end() };
}

// ================================================================ presets
// Gambale-style presets: interpretations of the tones he describes ("glassy"
// cleans; a lead tone he calls "fat, round and warm" with long sustain).
// Frank has not published his amp settings, so these are starting points.
// Every preset sets both channels so the footswitch gives a matched pair.
namespace
{
    using V = std::vector<std::pair<const char*, float>>;
    V chans (int ch, std::array<float, 5> c, std::array<float, 5> l, float rev, bool revOn = true)
    {
        V v { { ID::chan, (float) ch }, { ID::reverb, rev }, { ID::revOn, revOn ? 1.0f : 0.0f } };
        for (int i = 0; i < 5; ++i) { v.push_back ({ ID::clean[i], c[(size_t) i] }); v.push_back ({ ID::lead[i], l[(size_t) i] }); }
        return v;
    }
    const std::array<float, 5> glassyClean { 3.5f, 4.5f, 5.0f, 6.2f, 5.0f };
}

Fusion250Processor::Fusion250Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      convolution (juce::dsp::Convolution::Latency { 0 })
{
    presets = {
        //                                      ch   clean: gain low  mid  high level      lead: gain low  mid  high level   reverb
        { "Init",                                chans (0, { 4.0f, 5.0f, 5.0f, 5.0f, 5.0f }, { 6.0f, 4.5f, 6.5f, 5.0f, 5.0f }, 2.5f) },
        { "Gambale - Glassy Clean",              chans (0, glassyClean,                      { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 3.0f) },
        { "Gambale - Jazz Neck Clean",           chans (0, { 4.5f, 5.0f, 5.5f, 4.0f, 5.0f }, { 6.5f, 4.5f, 6.5f, 4.5f, 5.0f }, 2.5f) },
        { "Gambale - Edge of Breakup",           chans (0, { 7.5f, 4.5f, 6.0f, 5.5f, 3.0f }, { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 2.0f) },
        { "Gambale - Sweep Lead",                chans (1, glassyClean,                      { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 2.5f) },
        { "Gambale - Fat Round Lead (neck)",     chans (1, glassyClean,                      { 7.0f, 5.0f, 7.0f, 3.8f, 5.0f }, 3.0f) },
        { "Gambale - Singing Sustain",           chans (1, glassyClean,                      { 8.5f, 4.5f, 7.5f, 4.2f, 5.0f }, 3.5f) },
        { "Gambale - Fusion Crunch Rhythm",      chans (1, glassyClean,                      { 3.5f, 4.5f, 6.0f, 5.5f, 5.4f }, 1.5f) },
        { "Gambale - Ballad Lead (wet)",         chans (1, glassyClean,                      { 6.5f, 4.5f, 6.5f, 4.5f, 5.0f }, 5.5f) },
    };
}

Fusion250Processor::~Fusion250Processor()
{
    if (tunerThread) tunerThread->stopThread (1000);
}

bool Fusion250Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void Fusion250Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    amp.prepare (sampleRate);
    convolution.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
    monoIn.setSize (1, samplesPerBlock);
    stereoTmp.setSize (2, samplesPerBlock);
}

void Fusion250Processor::setTunerOpen (bool open)
{
    if (open && ! tunerOpen.load()) tunerRing.clear();   // no stale notes from last time
    tunerOpen.store (open);
    if (open)
    {
        if (! tunerThread) tunerThread = std::make_unique<TunerThread> (*this);
        if (! tunerThread->isThreadRunning()) tunerThread->startThread (juce::Thread::Priority::low);
    }
    else if (tunerThread)
    {
        tunerThread->stopThread (500);
        const juce::SpinLock::ScopedLockType sl (tunerLock);
        tunerResult = {};
    }
}

fusion::TunerResult Fusion250Processor::getTunerResult() const
{
    const juce::SpinLock::ScopedLockType sl (tunerLock);
    return tunerResult;
}

void Fusion250Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    if (monoIn.getNumSamples() < n) monoIn.setSize (1, n, false, false, true);

    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    fusion::Params p;
    p.channel = (int) v (ID::chan);
    for (int i = 0; i < 5; ++i) { p.clean[i] = v (ID::clean[i]); p.lead[i] = v (ID::lead[i]); }
    p.reverb = v (ID::reverb);
    p.reverbOn = v (ID::revOn) > 0.5f;
    p.guitar = (int) v (ID::guitar);
    const int outputMode = (int) v (ID::output);
    const bool useIr = outputMode == 2 && convolution.getCurrentIRSize() > 0;
    p.output = outputMode == 0 ? 0 : (useIr ? 2 : 1);   // no IR loaded -> built-in cab
    p.pad6 = v (ID::pad) > 0.5f;
    p.inDb = v (ID::inTrim);
    p.outDb = v (ID::outLvl);
    amp.setParams (p);

    // sum input to mono (guitar is a mono source)
    auto* m = monoIn.getWritePointer (0);
    if (numIn == 0) juce::FloatVectorOperations::clear (m, n);
    else
    {
        juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
        if (numIn > 1) { juce::FloatVectorOperations::add (m, buffer.getReadPointer (1), n); juce::FloatVectorOperations::multiply (m, 0.5f, n); }
    }

    // tuner taps the raw input, ahead of the amp (like a pedal at the front)
    const bool tuning = tunerOpen.load (std::memory_order_relaxed);
    if (tuning) tunerRing.push (m, n);

    if (numOut >= 2)
    {
        amp.process (m, buffer.getWritePointer (0), buffer.getWritePointer (1), n);
        if (useIr)
        {
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) n);
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        for (int c = 2; c < numOut; ++c) buffer.clear (c, 0, n);
    }
    else if (numOut == 1)
    {
        if (stereoTmp.getNumSamples() < n) stereoTmp.setSize (2, n, false, false, true);
        juce::AudioBuffer<float> tmp (stereoTmp.getArrayOfWritePointers(), 2, n);
        amp.process (m, tmp.getWritePointer (0), tmp.getWritePointer (1), n);
        if (useIr)
        {
            juce::dsp::AudioBlock<float> block (tmp);
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        auto* o = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) o[i] = 0.5f * (tmp.getSample (0, i) + tmp.getSample (1, i));
    }

    if (tuning && v (ID::tunerMute) > 0.5f) buffer.clear();
}

bool Fusion250Processor::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    convolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::yes, 0,
                                     juce::dsp::Convolution::Normalise::yes);
    irFile = file;
    return true;
}

void Fusion250Processor::setCurrentProgram (int index)
{
    if (index < 0 || index >= (int) presets.size()) return;
    currentPreset = index;
    for (auto& [id, val] : presets[(size_t) index].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (val));
}

const juce::String Fusion250Processor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? presets[(size_t) index].name : "";
}

void Fusion250Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("irPath", irFile.getFullPathName(), nullptr);
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void Fusion250Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            const juce::String ir = state.getProperty ("irPath").toString();
            currentPreset = (int) state.getProperty ("preset", 0);
            apvts.replaceState (state);
            if (ir.isNotEmpty()) loadImpulseResponse (juce::File (ir));
        }
}

juce::AudioProcessorEditor* Fusion250Processor::createEditor() { return new Fusion250Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Fusion250Processor(); }
