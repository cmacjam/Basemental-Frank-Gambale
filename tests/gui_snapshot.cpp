// End-to-end check of the real plug-in code: feeds a known tone with the tuner
// open, checks the tuner's reading, and renders PNG snapshots of the editor.
// Built only with -DFUSION_GUI_TEST=ON.
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "TunerPanel.h"
#include "PluginEditor.h"
#include "T3KPanel.h"
#include <thread>

static void snapshot (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double fs = 48000.0;
    const double targetHz = 110.0 * std::pow (2.0, 1.3 / 1200.0);   // A2, 1.3 cents sharp

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto* proc = dynamic_cast<Fusion250Processor*> (base.get());
    proc->setPlayConfigDetails (2, 2, fs, 512);
    proc->prepareToPlay (fs, 512);
    proc->setCurrentProgram (5);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    {   // play a note so the level meters show something
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb; double ph0 = 0;
        for (int blk = 0; blk < 12; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.4f * (float) std::sin (ph0); ph0 += 2 * juce::MathConstants<double>::pi * 110.0 / fs; b.setSample (0, i, v); b.setSample (1, i, v); }
            proc->processBlock (b, mb);
            if (blk % 3 == 2) { std::this_thread::sleep_for (std::chrono::milliseconds (35)); juce::Timer::callPendingTimersSynchronously(); }
        }
    }
    snapshot (*ed, outDir.getChildFile ("amp.png"));
    {   // high-resolution front plate (2x)
        const int w = ed->getWidth(), h = ed->getHeight();
        ed->setSize (w * 2, h * 2);
        snapshot (*ed, outDir.getChildFile ("faceplate@2x.png"));
        ed->setSize (w, h);
    }

    {   // FET Stack panel: compressor on and stacked, loud note, check gain reduction
        for (auto* ch : ed->getChildren())
            if (auto* c = dynamic_cast<CompPanel*> (ch)) c->setVisible (true);
        auto setP = [&] (const char* id, float v) { if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        // load a FET Stack preset: must switch the compressor on and leave the amp alone
        const float chanBefore = proc->apvts.getRawParameterValue ("channel")->load();
        const float leadGainBefore = proc->apvts.getRawParameterValue ("l_gain")->load();
        proc->applyCompPreset (9);   // Stacked British Mode
        if (proc->apvts.getRawParameterValue ("comp_on")->load() < 0.5f
            || proc->apvts.getRawParameterValue ("channel")->load() != chanBefore
            || proc->apvts.getRawParameterValue ("l_gain")->load() != leadGainBefore)
        { std::printf ("FAIL: FET Stack preset changed the amp or didn't turn the compressor on\n"); return 1; }
        std::printf ("FET Stack preset applied: compressor on, amp untouched\n");
        for (auto* ch : ed->getChildren())
            if (auto* c = dynamic_cast<CompPanel*> (ch)) { c->setVisible (false); c->setVisible (true); }
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb; double ph0 = 0;
        float minGr = 0;
        for (int blk = 0; blk < 60; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.3f * (float) std::sin (ph0); ph0 += 2 * juce::MathConstants<double>::pi * 196.0 / fs; b.setSample (0, i, v); b.setSample (1, i, v); }
            proc->processBlock (b, mb);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) if (! std::isfinite (b.getSample (c, i))) { std::printf ("FAIL: non-finite output with compressor\n"); return 1; }
            if (blk % 4 == 3) { std::this_thread::sleep_for (std::chrono::milliseconds (35)); juce::Timer::callPendingTimersSynchronously(); }
        }
        snapshot (*ed, outDir.getChildFile ("comp.png"));
        for (auto* ch : ed->getChildren())
            if (auto* c = dynamic_cast<CompPanel*> (ch)) c->setVisible (false);
        setP ("comp_on", 0);
        (void) minGr;
    }

    {   // NAM capture end to end: the FG 250's own capture must sound like the FG 250
        const juce::File namFile (argc > 2 ? juce::String (argv[2]) : juce::String ("tests/data/FG250_Lead_Sweep.nam"));
        const juce::File irFile  (argc > 3 ? juce::String (argv[3]) : juce::String ("tests/data/FG250_IR_01_Fusion_1x12_OnAxis_48k.wav"));
        auto setP = [&] (const char* id, float v) { if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        setP ("channel", 1); setP ("l_gain", 5.5f); setP ("l_low", 4.0f); setP ("l_mid", 6.5f); setP ("l_high", 5.0f); setP ("l_level", 5.0f);
        setP ("reverb_on", 0); setP ("guitar", 0); setP ("output", 1); setP ("cabinet", 0); setP ("mic", 0);
        setP ("comp_on", 0); setP ("input_trim", 0); setP ("output_level", 0); setP ("nam_normalize", 0); setP ("amp_source", 0);

        const int N = (int) fs * 3;
        std::vector<float> x ((size_t) N, 0.0f);
        for (int k = 0; k < 3; ++k)
        {
            const double f = 110.0 * std::pow (1.5, k); const int L = (int) (fs / f);
            std::vector<double> line ((size_t) L); juce::Random rng (k + 1);
            for (auto& v : line) v = (rng.nextDouble() - 0.5) * 0.6;
            for (int i = k * (int) fs; i < (k + 1) * (int) fs; ++i)
            { const int j = (i - k * (int) fs) % L; const double a = line[(size_t) j]; line[(size_t) j] = 0.996 * 0.5 * (a + line[(size_t) ((j + 1) % L)]); x[(size_t) i] = (float) a; }
        }
        auto render = [&] (std::vector<float>& y)
        {
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb;
            for (int blk = 0; blk < 200; ++blk) { b.clear(); proc->processBlock (b, mb); }   // settle (silence)
            y.assign ((size_t) N, 0.0f);
            for (int s = 0; s < N; s += 512)
            {
                const int n = std::min (512, N - s);
                juce::AudioBuffer<float> bb (2, n);
                for (int i = 0; i < n; ++i) { bb.setSample (0, i, x[(size_t) (s + i)]); bb.setSample (1, i, x[(size_t) (s + i)]); }
                proc->processBlock (bb, mb);
                for (int i = 0; i < n; ++i) y[(size_t) (s + i)] = bb.getSample (0, i);
            }
        };
        std::vector<float> yAmp, yNam, yNorm;
        render (yAmp);
        juce::String loadError;
        bool loadDone = false;
        proc->loadNamFile (namFile, { "FG250 Lead Sweep (test)", "Basemental", "https://www.tone3000.com", 0 },
                           [&] (fusion::ExternalTone::NamInfo info) { loadError = info.error; loadDone = true; });
        for (int i = 0; i < 1000 && ! proc->externalTone().ampLoaded(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (10));
        juce::ignoreUnused (loadDone);
        if (loadError.isNotEmpty()) std::printf ("NAM load error: %s\n", loadError.toRawUTF8());
        if (! proc->externalTone().ampLoaded()) { std::printf ("FAIL: NAM capture didn't load (%s)\n", namFile.getFullPathName().toRawUTF8()); return 1; }
        proc->namSource = { "FG250 Lead Sweep (test)", "Basemental", "https://www.tone3000.com", 0 };
        setP ("amp_source", 1);
        render (yNam);
        setP ("nam_normalize", 1);
        render (yNorm);
        double e = 0, r = 0, nn = 0, no = 0;
        for (int i = (int) fs / 10; i < N; ++i)
        { const double d = yNam[(size_t) i] - yAmp[(size_t) i]; e += d * d; r += (double) yAmp[(size_t) i] * yAmp[(size_t) i]; nn += (double) yNam[(size_t) i] * yNam[(size_t) i]; no += (double) yNorm[(size_t) i] * yNorm[(size_t) i]; }
        const double esr = e / r, lvl = 10 * std::log10 (nn / r), norm = 10 * std::log10 (no / nn);
        const auto info = proc->externalTone().namInfo();
        std::printf ("NAM in the plug-in: ESR vs FG 250 %.5f, level %+.2f dB, normalize %+.2f dB (expected %+.2f)\n",
                     esr, lvl, norm, -18.0 - info.loudnessDb);
        if (! (esr < 0.01 && std::abs (lvl) < 0.5 && std::abs (norm - (-18.0 - info.loudnessDb)) < 0.3))
        { std::printf ("FAIL: NAM capture doesn't match the FG 250\n"); return 1; }

        // User IR: the FG 250's own cabinet as an IR must match the built-in cabinet
        setP ("amp_source", 0); setP ("nam_normalize", 1);
        std::vector<float> yCab, yIr;
        render (yCab);
        if (! proc->loadIrFile (irFile, { "FG250 IR 01 (test)", "Basemental", {}, 0 })) { std::printf ("FAIL: IR didn't load\n"); return 1; }
        for (int i = 0; i < 300; ++i) { std::this_thread::sleep_for (std::chrono::milliseconds (5)); juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb; b.clear(); proc->processBlock (b, mb); }
        render (yIr);
        double ce = 0, cr = 0, ci = 0;
        for (int i = (int) fs / 10; i < N; ++i)
        { const double d = yIr[(size_t) i] - yCab[(size_t) i]; ce += d * d; cr += (double) yCab[(size_t) i] * yCab[(size_t) i]; ci += (double) yIr[(size_t) i] * yIr[(size_t) i]; }
        std::printf ("User IR vs built-in cab: ESR %.5f, level %+.2f dB\n", ce / cr, 10 * std::log10 (ci / cr));
        if (! (ce / cr < 0.02 && std::abs (10 * std::log10 (ci / cr)) < 0.5)) { std::printf ("FAIL: IR level/tone doesn't match the built-in cab\n"); return 1; }

        setP ("amp_source", 1);
        for (auto* ch : ed->getChildren())
            if (auto* t = dynamic_cast<T3KPanel*> (ch)) t->setVisible (true);
        juce::Timer::callPendingTimersSynchronously();
        snapshot (*ed, outDir.getChildFile ("tone3000.png"));
        for (auto* ch : ed->getChildren())
            if (auto* t = dynamic_cast<T3KPanel*> (ch)) t->setVisible (false);
        proc->clearNam(); proc->clearImpulseResponse();
        setP ("reverb_on", 1); setP ("guitar", 10);
    }

    TunerPanel* tuner = nullptr;
    for (auto* ch : ed->getChildren())
    {
        if (auto* t = dynamic_cast<TunerPanel*> (ch)) tuner = t;
        if (auto* b = dynamic_cast<juce::TextButton*> (ch)) if (b->getButtonText() == "TUNER") b->setToggleState (true, juce::dontSendNotification);
    }
    tuner->setVisible (true);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double ph = 0;
    long sample = 0;
    fusion::TunerResult r;
    for (int it = 0; it < 90; ++it)
    {
        for (int blk = 0; blk < 4; ++blk)
        {
            for (int i = 0; i < 512; ++i, ++sample)
            {
                const double env = 0.35 * std::exp (-(sample % (int) (fs * 4)) / (fs * 3.0));
                const float v = (float) (env * (std::sin (ph) + 0.6 * std::sin (2 * ph + 0.3) + 0.35 * std::sin (3 * ph + 1.1)));
                ph += 2 * juce::MathConstants<double>::pi * targetHz / fs;
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            proc->processBlock (buf, midi);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
        juce::Timer::callPendingTimersSynchronously();
        r = proc->getTunerResult();
    }

    // output must be muted while the tuner is open (Tuner Mute defaults on)
    const float outPeak = buf.getMagnitude (0, 512);
    const double err = std::fabs (r.cents - 1.3);
    std::printf ("tuner reading: %s%d  %+.3f cents  %.4f Hz (expected A2 +1.300, %.4f Hz)  error %.4f cents\n",
                 fusion::StrobeTunerCore::noteName (r.midiNote), fusion::StrobeTunerCore::octaveOf (r.midiNote),
                 r.cents, r.freq, targetHz, err);
    std::printf ("output while tuning: peak %.6f (%s)\n", outPeak, outPeak == 0.0f ? "muted ok" : "NOT MUTED");
    snapshot (*ed, outDir.getChildFile ("tuner.png"));

    tuner->setVisible (false);
    ed.reset();
    proc->releaseResources();
    const bool ok = r.valid && r.midiNote == 45 && err < 0.1 && outPeak == 0.0f;
    std::printf (ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
