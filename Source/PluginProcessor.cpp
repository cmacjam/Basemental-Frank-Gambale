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
    static const char* cabinet = "cabinet";
    static const char* mic = "mic";
    static const char* output = "output";
    static const char* pad = "preout_pad";
    static const char* inTrim = "input_trim";
    static const char* outLvl = "output_level";
    static const char* tunerRef = "tuner_ref";
    static const char* tunerMute = "tuner_mute";
    static const char* tunerTuning = "tuner_tuning";
    static const char* tunerMode = "tuner_mode";
    static const char* tunerNote = "tuner_note";
    static const char* tunerKey = "tuner_key";
    static const char* tunerOffset = "tuner_offset";
    static const char* tunerTemp = "tuner_temperament";
    static const char* tunerRoot = "tuner_root";
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
            auto pv = [this] (const char* id) { return (double) owner.apvts.getRawParameterValue (id)->load(); };
            fusion::TunerSettings st;
            st.refA = pv (ID::tunerRef);
            st.offsetCents = pv (ID::tunerOffset);
            st.temperament = (int) pv (ID::tunerTemp);
            st.root = (int) pv (ID::tunerRoot);
            st.manual = pv (ID::tunerMode) > 0.5;
            // manual note buttons are in the selected key: convert to concert pitch
            static const int keyShift[4] = { 0, 2, 7, 9 };   // C, Bb, F, Eb instruments
            st.manualNote = (((int) pv (ID::tunerNote) - keyShift[(int) pv (ID::tunerKey) & 3]) % 12 + 12) % 12;
            const auto r = core.analyse (scratch.data(), got, fs, st);
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
    const float leadDef[5]  = { 0.51f, 4.5f, 6.5f, 5, 5 };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::chan, 1 }, "Channel", StringArray { "Clean", "Lead" }, 0));
    for (int i = 0; i < 5; ++i)
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::clean[i], 1 }, String ("Clean ") + names[i], knob, cleanDef[i]));
    for (int i = 0; i < 5; ++i)
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::lead[i], 1 }, String ("Lead ") + names[i], knob, leadDef[i]));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::reverb, 1 }, "Reverb", knob, 2.5f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ID::revOn, 1 }, "Reverb On", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "power", 1 }, "Power", true));
    // FET Stack: two 1176-style FET compressors in series + Class-A drive, ahead of the amp
    auto skew = [] (float lo, float hi, float mid) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (mid); return r; };
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "comp_on", 1 }, "Comp On", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "comp_stacked", 1 }, "Comp Stacked", true));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp_input", 1 }, "Comp Input",
                 NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp_attack", 1 }, "Comp Attack",
                 skew (20.0f, 800.0f, 150.0f), 250.0f, AudioParameterFloatAttributes().withLabel ("us")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp_release", 1 }, "Comp Release",
                 skew (50.0f, 1100.0f, 300.0f), 250.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "comp_ratio", 1 }, "Comp Ratio",
                 StringArray { "4:1", "8:1", "12:1", "20:1" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp_thru", 1 }, "Comp Thru",
                 NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp_drive", 1 }, "Comp Drive",
                 NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp_dry", 1 }, "Comp Dry Mix",
                 NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("%")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp_output", 1 }, "Comp Output",
                 NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    StringArray voices, cabs;
    for (int i = 0; i < fusion::kNumVoices; ++i) voices.add (fusion::voiceName (i));
    for (int i = 0; i < fusion::kNumCabs; ++i) cabs.add (fusion::cabName (i));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::guitar, 1 }, "Guitar Voice", voices, 10));   // default: Semi-Hollow
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::cabinet, 1 }, "Cabinet", cabs, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::mic, 1 }, "Mic Position",
                 StringArray { "On-Axis", "Off-Axis", "Close (proximity)" }, 0));
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
    const StringArray notes { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    StringArray temps;
    for (int i = 0; i < fusion::kNumTemperaments; ++i) temps.add (fusion::temperamentName (i));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::tunerMode, 1 }, "Tuner Note Select", StringArray { "Auto", "Manual" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::tunerNote, 1 }, "Tuner Manual Note", notes, 9));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::tunerKey, 1 }, "Tuner Key",
                 StringArray { "C (concert)", "Bb", "F", "Eb" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::tunerOffset, 1 }, "Tuner Cent Offset",
                 NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("cents")));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::tunerTemp, 1 }, "Tuner Temperament", temps, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::tunerRoot, 1 }, "Tuner Temperament Root", notes, 0));
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
        V v { { ID::chan, (float) ch }, { ID::reverb, rev }, { ID::revOn, revOn ? 1.0f : 0.0f }, { "comp_on", 0.0f } };
        for (int i = 0; i < 5; ++i) { v.push_back ({ ID::clean[i], c[(size_t) i] }); v.push_back ({ ID::lead[i], l[(size_t) i] }); }
        return v;
    }
    V withVoice (V v, int voice) { v.push_back ({ ID::guitar, (float) voice }); return v; }
    // FET Stack settings: Input/Output were loudness-matched against the same preset
    // with the compressor off, on a test performance with uneven picking.
    struct CompSet { float input, attackUs, releaseMs; int ratioIdx; bool stacked; float thru, drive, dry, output; };
    V withComp (V v, CompSet c)
    {
        V extra { { "comp_on", 1.0f }, { "comp_input", c.input }, { "comp_attack", c.attackUs }, { "comp_release", c.releaseMs },
                  { "comp_ratio", (float) c.ratioIdx }, { "comp_stacked", c.stacked ? 1.0f : 0.0f }, { "comp_thru", c.thru },
                  { "comp_drive", c.drive }, { "comp_dry", c.dry }, { "comp_output", c.output } };
        v.insert (v.end(), extra.begin(), extra.end());
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
        { "Init",                                withVoice (chans (0, { 4.0f, 5.0f, 5.0f, 5.0f, 5.0f }, { 0.51f, 4.5f, 6.5f, 5.0f, 5.0f }, 2.5f), 10) },
        { "Gambale - Glassy Clean",              chans (0, glassyClean,                      { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 3.0f) },
        { "Gambale - Jazz Neck Clean",           chans (0, { 4.5f, 5.0f, 5.5f, 4.0f, 5.0f }, { 6.5f, 4.5f, 6.5f, 4.5f, 5.0f }, 2.5f) },
        { "Gambale - Edge of Breakup",           chans (0, { 7.5f, 4.5f, 6.0f, 5.5f, 3.0f }, { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 2.0f) },
        { "Gambale - Sweep Lead",                chans (1, glassyClean,                      { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 2.5f) },
        { "Gambale - Fat Round Lead (neck)",     chans (1, glassyClean,                      { 7.0f, 5.0f, 7.0f, 3.8f, 5.0f }, 3.0f) },
        { "Gambale - Singing Sustain",           chans (1, glassyClean,                      { 8.5f, 4.5f, 7.5f, 4.2f, 5.0f }, 3.5f) },
        { "Gambale - Fusion Crunch Rhythm",      chans (1, glassyClean,                      { 3.5f, 4.5f, 6.0f, 5.5f, 5.4f }, 1.5f) },
        { "Gambale - Ballad Lead (wet)",         chans (1, glassyClean,                      { 6.5f, 4.5f, 6.5f, 4.5f, 5.0f }, 5.5f) },
        // ---- compressor presets (FET Stack on). Typical gain reduction in brackets.
        //                                                                                                                           in   attack release ratio stack thru drive dry  out
        { "Comp - Gentle Always-On (3 dB)",      withComp (chans (0, { 4.0f, 5.0f, 5.0f, 5.0f, 5.0f }, { 6.0f, 4.5f, 6.5f, 5.0f, 5.0f }, 2.5f), { -5.5f, 600,  300, 0, false,  0, 0.5f, 20, 7.0f }) },
        { "Comp - Jazz Smooth Clean (5 dB)",     withComp (chans (0, { 4.5f, 5.0f, 5.5f, 4.0f, 5.0f }, { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 2.5f), { -3.5f, 800,  600, 0, false,  0, 0.8f, 30, 5.5f }) },
        { "Comp - Clean Arpeggio Shimmer (6 dB)",withComp (chans (0, { 3.5f, 4.5f, 5.0f, 6.2f, 5.0f }, { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 4.5f), { -5.5f, 500,  800, 0, true,  -2, 0.0f, 25, 8.5f }) },
        { "Comp - Country Snap (8 dB)",          withComp (chans (0, { 4.5f, 4.5f, 5.0f, 6.5f, 5.0f }, { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 1.5f), {  1.5f, 400,  150, 1, false,  0, 1.0f, 15, 6.5f }) },
        { "Comp - Funk Rhythm Squash (12 dB)",   withComp (chans (0, { 4.0f, 4.0f, 5.5f, 6.0f, 5.0f }, { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 1.0f), {  1.5f,  30,  100, 2, true,   3, 0.5f,  0, 8.0f }) },
        { "Comp - Parallel Punch (15 dB)",       withComp (chans (0, { 6.0f, 4.5f, 6.0f, 5.5f, 4.5f }, { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f }, 1.5f), {  2.5f,  20,   50, 3, true,   6, 3.0f, 50, 3.0f }) },
        { "Comp - Sweep Picking Evener (8 dB)",  withComp (chans (1, glassyClean,                      { 5.0f, 4.0f, 6.5f, 5.0f, 5.0f }, 2.0f), {  1.0f,  60,  120, 1, true,  -3, 0.5f,  0, 2.0f }) },
        { "Comp - Fusion Legato Sustain (10 dB)",withComp (chans (1, glassyClean,                      { 6.5f, 4.5f, 7.0f, 4.2f, 5.0f }, 3.0f), { -0.5f, 250,  400, 0, true,   2, 1.5f, 10, 0.5f }) },
        { "Comp - Singing Lead Sustain (16 dB)", withComp (chans (1, glassyClean,                      { 7.5f, 4.5f, 7.5f, 4.2f, 5.0f }, 3.5f), {  0.0f, 400, 1100, 2, true,   4, 2.5f,  0, 0.5f }) },
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
    p.cabinet = (int) v (ID::cabinet);
    p.mic = (int) v (ID::mic);
    const int outputMode = (int) v (ID::output);
    const bool useIr = outputMode == 2 && convolution.getCurrentIRSize() > 0;
    p.output = outputMode == 0 ? 0 : (useIr ? 2 : 1);   // no IR loaded -> built-in cab
    p.pad6 = v (ID::pad) > 0.5f;
    p.inDb = v (ID::inTrim);
    {
        static const float ratios[4] = { 4.0f, 8.0f, 12.0f, 20.0f };
        auto& c = p.comp;
        c.on = v ("comp_on") > 0.5f;
        c.stacked = v ("comp_stacked") > 0.5f;
        c.inputDb = v ("comp_input");
        c.attackUs = v ("comp_attack");
        c.releaseMs = v ("comp_release");
        c.ratio = ratios[juce::jlimit (0, 3, (int) v ("comp_ratio"))];
        c.thruDb = v ("comp_thru");
        c.drive = v ("comp_drive") / 10.0f;
        c.dryMix = v ("comp_dry") / 100.0f;
        c.outputDb = v ("comp_output");
    }
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

    // input meter: what reaches the amp (after the Input trim)
    atomicMax (inPeak, juce::FloatVectorOperations::findMaximum (m, n) * juce::Decibels::decibelsToGain (p.inDb));
    atomicMax (inPeak, -juce::FloatVectorOperations::findMinimum (m, n) * juce::Decibels::decibelsToGain (p.inDb));
    {
        const float g = juce::Decibels::decibelsToGain (p.inDb);
        float ss = 0; for (int i = 0; i < n; ++i) ss += m[i] * m[i];
        atomicAdd (inSum, ss * g * g); inCount.fetch_add (n);
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

    {   // gain-reduction readout for the COMP panel
        float g1, g2; amp.takeCompGainReduction (g1, g2);
        atomicMin (compGr1, g1); atomicMin (compGr2, g2);
    }

    // power switch: smooth fade so switching never clicks (tuner still listens)
    const float powerTarget = v ("power") > 0.5f ? 1.0f : 0.0f;
    if (powerGain != powerTarget || powerTarget < 1.0f)
        for (int c = 0; c < numOut; ++c) buffer.applyGainRamp (c, 0, n, powerGain, powerTarget);
    powerGain = powerTarget;

    if (tuning && v (ID::tunerMute) > 0.5f) buffer.clear();
    for (int c = 0; c < numOut; ++c) atomicMax (outPeak, buffer.getMagnitude (c, 0, n));
    if (numOut > 0)
    {
        float ss = 0;
        for (int c = 0; c < numOut; ++c) { const float r = buffer.getRMSLevel (c, 0, n); ss += r * r * n; }
        atomicAdd (outSum, ss / (float) numOut); outCount.fetch_add (n);
    }
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
