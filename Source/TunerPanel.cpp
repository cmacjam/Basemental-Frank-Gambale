#include "TunerPanel.h"

namespace
{
    const juce::Colour bg       { 0xff0d0f11 };
    const juce::Colour panel    { 0xff16191d };
    const juce::Colour ivory    { 0xfff1e8d6 };
    const juce::Colour brass    { 0xffd6a84f };
    const juce::Colour amber    { 0xffffa53a };
    const juce::Colour inTune   { 0xff58f28a };
    const juce::Colour redFlat  { 0xffff5a4a };
    constexpr double kInTuneCents = 0.5;   // green light window
}

TunerPanel::TunerPanel (Fusion250Processor& p) : proc (p)
{
    setOpaque (true);

    refLabel.setText ("A =", juce::dontSendNotification);
    refLabel.setColour (juce::Label::textColourId, ivory.withAlpha (0.8f));
    refLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (refLabel);
    refSlider.setTextValueSuffix (" Hz");
    refSlider.setNumDecimalPlacesToDisplay (1);
    refSlider.setColour (juce::Slider::textBoxTextColourId, ivory);
    refSlider.setColour (juce::Slider::textBoxOutlineColourId, ivory.withAlpha (0.25f));
    addAndMakeVisible (refSlider);
    refAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "tuner_ref", refSlider);

    muteBtn.setColour (juce::ToggleButton::textColourId, ivory.withAlpha (0.85f));
    muteBtn.setColour (juce::ToggleButton::tickColourId, brass);
    addAndMakeVisible (muteBtn);
    muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "tuner_mute", muteBtn);

    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("tuner_tuning")))
        tuningBox.addItemList (c->choices, 1);
    addAndMakeVisible (tuningBox);
    tuningAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "tuner_tuning", tuningBox);
    tuningBox.onChange = [this] { repaint(); };
    tuningLabel.setText ("STRING GUIDE", juce::dontSendNotification);
    tuningLabel.setColour (juce::Label::textColourId, ivory.withAlpha (0.6f));
    tuningLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (tuningLabel);
}

TunerPanel::~TunerPanel() { proc.setTunerOpen (false); }

void TunerPanel::visibilityChanged()
{
    const bool on = isVisible() && getParentComponent() != nullptr;
    proc.setTunerOpen (on);
    if (on) { lastTime = juce::Time::getMillisecondCounterHiRes(); startTimerHz (60); }
    else stopTimer();
}

std::vector<int> TunerPanel::currentTuningNotes() const
{
    switch (tuningBox.getSelectedItemIndex())
    {
        case 1: return { 40, 45, 50, 55, 59, 64 };          // E2 A2 D3 G3 B3 E4
        case 2: return { 45, 50, 55, 60, 52, 57 };          // A2 D3 G3 C4 E3 A3 (Gambale)
        case 3: return { 39, 44, 49, 54, 58, 63 };
        case 4: return { 38, 45, 50, 55, 59, 64 };
        case 5: return { 35, 40, 45, 50, 55, 59, 64 };
        default: return {};
    }
}

void TunerPanel::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.1, (now - lastTime) * 0.001);
    lastTime = now;

    const auto r = proc.getTunerResult();
    if (r.valid)
    {
        if (! hasReading || r.midiNote != shown.midiNote) { smoothCents = r.cents; smoothDelta = r.deltaHz; }
        const double a = 1.0 - std::exp (-dt / 0.12);
        smoothCents += a * (r.cents - smoothCents);
        smoothDelta += a * (r.deltaHz - smoothDelta);
        shown = r;
        hasReading = true;
        lastValidTime = now;
    }
    else if (now - lastValidTime > 600.0)
    {
        hasReading = false;
    }

    // strobe: one pattern period per beat cycle (sharp drifts right)
    if (hasReading) strobePhase += smoothDelta * dt;
    strobePhase -= std::floor (strobePhase);
    repaint();
}

void TunerPanel::resized()
{
    const float s = (float) getWidth() / 960.0f;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    auto r = getLocalBounds();
    readoutArea = r.removeFromTop (S (118));
    guideArea = r.removeFromBottom (S (62));
    strobeArea = r.reduced (S (24), S (6));

    const int cx = getWidth() - S (250);
    refLabel.setBounds (cx, S (14), S (40), S (26));
    refSlider.setBounds (cx + S (42), S (14), S (190), S (26));
    muteBtn.setBounds (cx, S (46), S (232), S (24));
    tuningLabel.setBounds (cx, S (74), S (232), S (14));
    tuningBox.setBounds (cx, S (88), S (232), S (24));
}

void TunerPanel::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / 960.0f;
    g.fillAll (bg);

    const bool live = hasReading;
    const double c = smoothCents;
    const bool locked = live && std::fabs (c) < kInTuneCents;
    const juce::Colour accent = ! live ? ivory.withAlpha (0.25f) : (locked ? inTune : amber);

    // ------------------------------------------------------------ readout
    auto ra = readoutArea.reduced ((int) (24 * s), (int) (10 * s));
    auto noteBox = ra.removeFromLeft ((int) (150 * s));
    g.setColour (panel);
    g.fillRoundedRectangle (noteBox.toFloat(), 8.0f * s);
    if (live)
    {
        g.setColour (accent);
        g.setFont (juce::FontOptions (64.0f * s, juce::Font::bold));
        g.drawText (fusion::StrobeTunerCore::noteName (shown.midiNote), noteBox.withTrimmedRight ((int) (34 * s)), juce::Justification::centredRight);
        g.setFont (juce::FontOptions (22.0f * s, juce::Font::bold));
        g.drawText (juce::String (fusion::StrobeTunerCore::octaveOf (shown.midiNote)),
                    noteBox.withTrimmedLeft (noteBox.getWidth() - (int) (34 * s)).withTrimmedTop ((int) (40 * s)), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (ivory.withAlpha (0.3f));
        g.setFont (juce::FontOptions (48.0f * s, juce::Font::bold));
        g.drawText ("--", noteBox, juce::Justification::centred);
    }

    ra.removeFromLeft ((int) (18 * s));
    auto centsBox = ra.removeFromLeft ((int) (300 * s));
    g.setFont (juce::FontOptions (46.0f * s, juce::Font::bold));
    g.setColour (live ? accent : ivory.withAlpha (0.3f));
    const juce::String centsText = live ? ((c >= 0 ? "+" : "") + juce::String (c, 1) + juce::String::fromUTF8 (" \xc2\xa2")) : juce::String::fromUTF8 ("-- \xc2\xa2");
    g.drawText (centsText, centsBox.removeFromTop ((int) (56 * s)), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (16.0f * s));
    g.setColour (ivory.withAlpha (0.75f));
    g.drawText (live ? juce::String (shown.freq, 2) + " Hz   (target " + juce::String (shown.refFreq, 2) + " Hz)" : "Play a single note",
                centsBox.removeFromTop ((int) (22 * s)), juce::Justification::centredLeft);

    // flat / in tune / sharp lights
    auto lights = ra.removeFromLeft ((int) (150 * s)).withSizeKeepingCentre ((int) (150 * s), (int) (40 * s));
    const float d = 18.0f * s;
    const juce::Colour lc[3] = { redFlat, inTune, redFlat };
    const bool on[3] = { live && c <= -kInTuneCents, locked, live && c >= kInTuneCents };
    const char* lt[3] = { "FLAT", "IN TUNE", "SHARP" };
    for (int i = 0; i < 3; ++i)
    {
        const float x = lights.getX() + (i + 0.5f) * lights.getWidth() / 3.0f;
        if (on[i]) { g.setColour (lc[i].withAlpha (0.3f)); g.fillEllipse (x - d, lights.getY() + 2 * s - d * 0.5f, d * 2, d * 2); }
        g.setColour (on[i] ? lc[i] : lc[i].darker (2.8f));
        g.fillEllipse (x - d * 0.5f, lights.getY() + 2 * s, d, d);
        g.setColour (ivory.withAlpha (on[i] ? 0.9f : 0.4f));
        g.setFont (juce::FontOptions (10.0f * s, juce::Font::bold));
        g.drawText (lt[i], juce::Rectangle<float> (x - 30 * s, lights.getY() + 24 * s, 60 * s, 14 * s), juce::Justification::centred);
    }

    // ------------------------------------------------------------ strobe
    const int bands = 3;
    const int mult[bands] = { 1, 2, 4 };
    const float gap = 6.0f * s;
    const float bh = (strobeArea.getHeight() - gap * (bands - 1)) / (float) bands;
    const float period = 48.0f * s;
    for (int b = 0; b < bands; ++b)
    {
        auto band = juce::Rectangle<float> ((float) strobeArea.getX() + 34 * s, strobeArea.getY() + b * (bh + gap),
                                            strobeArea.getWidth() - 34 * s, bh);
        g.setColour (ivory.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f * s, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("\xc3\x97") + juce::String (mult[b]), juce::Rectangle<float> ((float) strobeArea.getX(), band.getY(), 30 * s, bh), juce::Justification::centredLeft);

        g.setColour (panel);
        g.fillRoundedRectangle (band, 5.0f * s);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (band.toNearestInt());
        double ph = strobePhase * mult[b];
        ph -= std::floor (ph);
        const float off = (float) (ph * period);
        const juce::Colour seg = accent.withAlpha (live ? 0.95f : 0.35f);
        for (float x = band.getX() - period + off; x < band.getRight(); x += period)
        {
            juce::ColourGradient grad (seg.withAlpha (0.0f), x, 0, seg.withAlpha (0.0f), x + period * 0.5f, 0, false);
            grad.addColour (0.2, seg);
            grad.addColour (0.8, seg);
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> (x, band.getY() + 3 * s, period * 0.5f, band.getHeight() - 6 * s));
        }
    }
    // centre mark
    g.setColour (ivory.withAlpha (0.35f));
    const float midX = strobeArea.getX() + 34 * s + (strobeArea.getWidth() - 34 * s) * 0.5f;
    g.drawLine (midX, (float) strobeArea.getY() - 3 * s, midX, (float) strobeArea.getBottom() + 3 * s, 1.0f);

    // ------------------------------------------------------- string guide
    const auto notes = currentTuningNotes();
    auto ga = guideArea.reduced ((int) (24 * s), (int) (10 * s));
    if (notes.empty())
    {
        g.setColour (ivory.withAlpha (0.5f));
        g.setFont (juce::FontOptions (13.0f * s));
        g.drawText ("Chromatic mode: tunes any note. Pick a string guide for Gambale tuning, standard, drop D...", ga, juce::Justification::centredLeft);
        return;
    }
    const int count = (int) notes.size();
    const float w = (ga.getWidth() - 8 * s * (count - 1)) / (float) count;
    for (int i = 0; i < count; ++i)
    {
        auto box = juce::Rectangle<float> (ga.getX() + i * (w + 8 * s), (float) ga.getY(), w, (float) ga.getHeight());
        const bool match = live && shown.midiNote == notes[(size_t) i];
        g.setColour (match ? accent.withAlpha (0.18f) : panel);
        g.fillRoundedRectangle (box, 6.0f * s);
        g.setColour (match ? accent : ivory.withAlpha (0.15f));
        g.drawRoundedRectangle (box, 6.0f * s, match ? 2.0f : 1.0f);
        const juce::String nm = juce::String (fusion::StrobeTunerCore::noteName (notes[(size_t) i])) + juce::String (fusion::StrobeTunerCore::octaveOf (notes[(size_t) i]));
        g.setColour (match ? accent : ivory.withAlpha (0.85f));
        g.setFont (juce::FontOptions (18.0f * s, juce::Font::bold));
        g.drawText (nm, box.withTrimmedBottom (12 * s), juce::Justification::centred);
        g.setColour (ivory.withAlpha (0.45f));
        g.setFont (juce::FontOptions (10.0f * s));
        g.drawText ("string " + juce::String (count - i), box.withTrimmedTop (box.getHeight() - 16 * s), juce::Justification::centred);
    }
}
