// End-to-end check of the real plug-in code: feeds a known tone with the tuner
// open, checks the tuner's reading, and renders PNG snapshots of the editor.
// Built only with -DFUSION_GUI_TEST=ON.
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "TunerPanel.h"
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
    snapshot (*ed, outDir.getChildFile ("amp.png"));

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
