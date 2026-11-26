#include "ExternalTone.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#include "NAM/model_config.h"
#include "NAM/container.h"
#include "NAM/convnet.h"
#include "NAM/linear.h"
#include "NAM/lstm.h"
#include "NAM/sequential.h"
#include "NAM/wavenet/model.h"

#include <mutex>

namespace fusion
{
// ======================================================================== RateBridge
void RateBridge::prepare (double hostRate, double modelRate, int maxBlock)
{
    ratio = (modelRate > 0 && std::abs (hostRate - modelRate) > 0.5) ? hostRate / modelRate : 1.0;
    up.reset(); down.reset();
    const int cap = (int) std::ceil ((double) maxBlock * std::max (1.0, 1.0 / ratio)) + 1024;
    inQ.assign ((size_t) cap, 0.0f);
    mid.assign ((size_t) cap, 0.0f);
    outQ.assign ((size_t) cap * 2, 0.0f);
    inCount = 0;
    // model-rate samples queued ahead of the output resampler: covers the block-to-block
    // jitter of both resamplers, so the output never runs dry
    outCount = 48;
    latency = active() ? (int) std::lround (outCount * ratio) + 2 * (int) up.getBaseLatency() : 0;
}

template <typename Fn>
void RateBridge::process (float* io, int n, Fn&& modelProcess)
{
    if (! active()) { modelProcess (io, n); return; }

    // host -> model rate
    std::copy (io, io + n, inQ.begin() + inCount);
    inCount += n;
    const int margin = 8;
    int m = (int) std::floor ((inCount - margin) / ratio);
    m = juce::jlimit (0, (int) mid.size(), m);
    const int used = up.process (ratio, inQ.data(), mid.data(), m);
    std::move (inQ.begin() + used, inQ.begin() + inCount, inQ.begin());
    inCount -= used;

    modelProcess (mid.data(), m);

    // model -> host rate
    if (outCount + m > (int) outQ.size()) outCount = (int) outQ.size() - m;   // never in practice
    std::copy (mid.begin(), mid.begin() + m, outQ.begin() + outCount);
    outCount += m;
    const int need = (int) std::ceil (n / ratio) + margin;
    if (outCount < need)                                   // underrun guard: pad with silence
    {
        std::fill (outQ.begin() + outCount, outQ.begin() + need, 0.0f);
        outCount = need;
    }
    const int took = down.process (1.0 / ratio, outQ.data(), io, n);
    std::move (outQ.begin() + took, outQ.begin() + outCount, outQ.begin());
    outCount -= took;
}

// NAM registers its architectures with static initialisers. Inside a plug-in's static
// library those object files can be dropped by the linker, so register them explicitly
// (this also forces the linker to keep them).
static void ensureNamArchitectures()
{
    static std::once_flag once;
    std::call_once (once, []
    {
        auto& reg = nam::ConfigParserRegistry::instance();
        auto add = [&reg] (const char* name, nam::ConfigParserFunction f) { if (! reg.has (name)) reg.registerParser (name, std::move (f)); };
        add ("WaveNet", nam::wavenet::create_config);
        add ("SlimmableContainer", nam::container::create_config);
        add ("LSTM", nam::lstm::create_config);
        add ("ConvNet", nam::convnet::create_config);
        add ("Linear", nam::linear::create_config);
        add ("Sequential", nam::sequential::create_config);
    });
}

// ======================================================================= the NAM slot
struct ExternalTone::Slot
{
    std::unique_ptr<nam::DSP> dsp;
    double modelRate = 48000;
    float gain = 1.0f;          // loudness normalisation
    RateBridge bridge;
    std::vector<NAM_SAMPLE> in, out;
};

ExternalTone::ExternalTone() = default;

ExternalTone::~ExternalTone()
{
    loader.removeAllJobs (true, 10000);
}

void ExternalTone::prepare (double sampleRate, int block)
{
    fs = sampleRate;
    maxBlock = std::max (block, 256);

    const juce::SpinLock::ScopedLockType sl (swapLock);
    if (slot != nullptr)
    {
        // rebuilt with the new host rate (prepareToPlay: the audio thread is stopped)
        slot->dsp->ResetAndPrewarm (slot->modelRate, std::max (maxBlock * 4, 4096));
        slot->bridge.prepare (fs, slot->modelRate, maxBlock);
    }
}

std::unique_ptr<ExternalTone::Slot> ExternalTone::buildSlot (const juce::File& file, NamInfo& info) const
{
    ensureNamArchitectures();
    auto s = std::make_unique<Slot>();
    try
    {
        s->dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
    }
    catch (const std::exception& e)
    {
        info.error = "Couldn't load the NAM file: " + juce::String (e.what());
        return {};
    }
    if (s->dsp == nullptr) { info.error = "Couldn't load the NAM file."; return {}; }

    auto* slim = dynamic_cast<nam::SlimmableModel*> (s->dsp.get());
    info.slimmable = slim != nullptr;
    if (slim != nullptr) slim->SetSlimmableSize (lite.load() ? 0.0 : 1.0);

    const double expected = s->dsp->GetExpectedSampleRate();
    s->modelRate = expected > 0 ? expected : 48000.0;
    info.sampleRate = s->modelRate;
    info.hasLoudness = s->dsp->HasLoudness();
    info.loudnessDb = info.hasLoudness ? s->dsp->GetLoudness() : 0.0;
    info.name = file.getFileNameWithoutExtension();

    const int big = std::max (maxBlock * 4, 4096);   // covers the resampler's model-rate blocks
    s->dsp->ResetAndPrewarm (s->modelRate, big);
    s->bridge.prepare (fs, s->modelRate, maxBlock);
    s->in.assign ((size_t) big, 0); s->out.assign ((size_t) big, 0);
    // NAM's convention: bring the model to -18 dB (its "typical" output loudness),
    // close to where the FG 250's own channels sit.
    s->gain = info.hasLoudness ? (float) juce::Decibels::decibelsToGain (juce::jlimit (-24.0, 24.0, -18.0 - info.loudnessDb)) : 1.0f;
    return s;
}

void ExternalTone::loadNam (const juce::File& file, std::function<void (NamInfo)> done)
{
    const int gen = ++loadGeneration;
    loader.addJob ([this, file, done, gen]
    {
        NamInfo info;
        auto s = file.existsAsFile() ? buildSlot (file, info) : nullptr;
        if (! file.existsAsFile()) info.error = "File not found: " + file.getFullPathName();

        if (s != nullptr && gen == loadGeneration.load())
        {
            {
                const juce::SpinLock::ScopedLockType sl (swapLock);
                std::swap (slot, s);
                normGain = slot->gain;
                hasModel = true;
            }
            const juce::ScopedLock pl (pathLock);
            namPath = file;
            currentInfo = info;
        }
        s.reset();     // the old model is freed here, off the audio thread
        if (done)
            juce::MessageManager::callAsync ([done, info] { done (info); });
    });
}

void ExternalTone::clearNam()
{
    ++loadGeneration;
    std::unique_ptr<Slot> old;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        hasModel = false;
        std::swap (slot, old);
    }
    const juce::ScopedLock pl (pathLock);
    namPath = juce::File();
    currentInfo = {};
}

void ExternalTone::setLite (bool b)
{
    if (lite.exchange (b) == b) return;
    const auto f = namFile();
    if (f.existsAsFile()) loadNam (f);     // re-built at the new size
}

juce::File ExternalTone::namFile() const { const juce::ScopedLock pl (pathLock); return namPath; }
ExternalTone::NamInfo ExternalTone::namInfo() const { const juce::ScopedLock pl (pathLock); return currentInfo; }

// ========================================================================= audio thread
void ExternalTone::processAmp (float* x, int n)
{
    const juce::SpinLock::ScopedTryLockType tl (swapLock);
    if (! tl.isLocked() || slot == nullptr) { std::fill (x, x + n, 0.0f); return; }   // mid-swap: one silent block
    auto& s = *slot;
    const float g = normalise.load() ? s.gain : 1.0f;
    s.bridge.process (x, n, [&] (float* buf, int m)
    {
        for (int off = 0; off < m; )
        {
            const int k = std::min ((int) s.in.size(), m - off);
            for (int i = 0; i < k; ++i) s.in[(size_t) i] = (NAM_SAMPLE) buf[off + i];
            NAM_SAMPLE* ip = s.in.data();
            NAM_SAMPLE* op = s.out.data();
            s.dsp->process (&ip, &op, k);
            for (int i = 0; i < k; ++i) buf[off + i] = (float) s.out[(size_t) i] * g;
            off += k;
        }
    });
}

} // namespace fusion
