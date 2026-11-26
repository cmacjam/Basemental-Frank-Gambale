// NAM captures and IR files inside the FG 250 (TONE3000 tones, or files from disk).
//
// The amp slot runs a Neural Amp Modeler model (A1 or A2, incl. A2 Full/Lite) in place of
// the FG 250 channels (IRs use the plug-in's User IR). Loading happens off the audio thread; the audio thread only ever try-locks a
// spin lock for the pointer swap, so a load never blocks it.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "AmpCore.h"

#include <atomic>
#include <memory>

namespace nam { class DSP; }

namespace fusion
{
// Streams mono audio through a fixed-rate process at another sample rate
// (host 44.1/88.2/96 kHz <-> NAM model 48 kHz). Fixed latency, no allocation per block.
class RateBridge
{
public:
    void prepare (double hostRate, double modelRate, int maxBlock);
    bool active() const { return ratio != 1.0; }
    template <typename Fn> void process (float* io, int n, Fn&& modelProcess);
    int latencyHostSamples() const { return latency; }

private:
    double ratio = 1.0;                  // host / model
    juce::WindowedSincInterpolator up, down;
    std::vector<float> inQ, mid, outQ;
    int inCount = 0, outCount = 0, latency = 0;
};

class ExternalTone : public AmpCore::ExternalStages
{
public:
    ExternalTone();
    ~ExternalTone() override;

    // ---- message thread / any non-audio thread ---------------------------------------
    void prepare (double sampleRate, int maxBlock);

    struct NamInfo { juce::String name, error; double sampleRate = 0, loudnessDb = 0; bool hasLoudness = false, slimmable = false; };
    /// Load a .nam file on a background thread; `done` runs on the message thread.
    void loadNam (const juce::File& file, std::function<void (NamInfo)> done = {});
    void clearNam();
    void setAmpWanted (bool b) { ampOn = b; }
    void setNormalise (bool b) { normalise = b; }
    /// A2 models hold a Full and a Lite size; Lite uses much less CPU.
    void setLite (bool b);

    juce::File namFile() const;
    NamInfo namInfo() const;

    // ---- audio thread (AmpCore::ExternalStages) ----------------------------------------
    bool ampLoaded() override { return hasModel.load(); }
    bool ampWanted() override { return ampOn.load(); }
    void processAmp (float* x, int n) override;
    // the cabinet replacement is the plug-in's User IR (Output = User IR), not this class
    bool cabLoaded() override { return false; }
    bool cabWanted() override { return false; }
    void processCab (float*, float*, int) override {}

private:
    struct Slot;
    std::unique_ptr<Slot> buildSlot (const juce::File& file, NamInfo& info) const;

    juce::SpinLock swapLock;
    std::unique_ptr<Slot> slot;                    // guarded by swapLock
    std::atomic<bool> hasModel { false }, ampOn { false },
                      normalise { true }, lite { false };
    std::atomic<float> normGain { 1.0f };

    mutable juce::CriticalSection pathLock;
    juce::File namPath;
    NamInfo currentInfo;

    double fs = 48000.0;
    int maxBlock = 512;
    juce::ThreadPool loader { 1 };
    std::atomic<int> loadGeneration { 0 };
};
} // namespace fusion
