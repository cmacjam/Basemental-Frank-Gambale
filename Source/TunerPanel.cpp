#include "TunerPanel.h"

namespace
{
    const juce::Colour bg       { 0xff0d0f11 };
    const juce::Colour panel    { 0xff16191d };
    const juce::Colour ivory    { 0xfff2f2f2 };
    const juce::Colour brass    { 0xffffffff };
    const juce::Colour amber    { 0xfff7f7f7 };   // black-and-white display
    const juce::Colour inTune   { 0xff58f28a };   // lamp only
    const juce::Colour redFlat  { 0xffff5a4a };
    constexpr double kInTuneCents = 0.5;   // green light window
    constexpr float kBase = 936.0f;        // panel design width
    const char* kNotes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

TunerPanel::TunerPanel (Fusion250Processor& p) : proc (p)
{
    setOpaque (true);
    auto& st = proc.apvts;

    auto setupLabel = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, ivory.withAlpha (0.75f));
        l.setJustificationType (juce::Justification::centredRight);
        l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    auto setupSlider = [this] (juce::Slider& sl, const juce::String& suffix, int decimals)
    {
        sl.setTextValueSuffix (suffix);
        sl.setNumDecimalPlacesToDisplay (decimals);
        sl.setColour (juce::Slider::textBoxTextColourId, ivory);
        sl.setColour (juce::Slider::textBoxOutlineColourId, ivory.withAlpha (0.25f));
        sl.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        addAndMakeVisible (sl);
    };
    setupLabel (refLabel, "A =");
    setupSlider (refSlider, " Hz", 1);
    refAtt = std::make_unique<SA> (st, "tuner_ref", refSlider);
    setupLabel (offsetLabel, "OFFSET");
    setupSlider (offsetSlider, juce::String::fromUTF8 (" \xc2\xa2"), 1);
    offsetSlider.setDoubleClickReturnValue (true, 0.0);
    offsetSlider.setTooltip ("Global cent offset, 0.1-cent steps (double-click to reset)");
    offsetAtt = std::make_unique<SA> (st, "tuner_offset", offsetSlider);

    auto setupBox = [this, &st] (juce::ComboBox& b, const char* id, std::unique_ptr<CA>& att, const juce::String& tip)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (st.getParameter (id))) b.addItemList (c->choices, 1);
        b.setTooltip (tip);
        addAndMakeVisible (b);
        att = std::make_unique<CA> (st, id, b);
        b.onChange = [this] { repaint(); resized(); };
    };
    setupBox (modeBox, "tuner_mode", modeAtt, "Auto: nearest note. Manual: tune to the note you pick below");
    setupBox (keyBox, "tuner_key", keyAtt, "Show notes for a transposing instrument (Bb, F, Eb)");
    setupBox (tempBox, "tuner_temperament", tempAtt, "Temperament");
    setupBox (rootBox, "tuner_root", rootAtt, "Temperament root (key)");
    setupBox (tuningBox, "tuner_tuning", tuningAtt, "String guide");

    muteBtn.setColour (juce::ToggleButton::textColourId, ivory.withAlpha (0.85f));
    muteBtn.setColour (juce::ToggleButton::tickColourId, brass);
    muteBtn.setTooltip ("Mute the output while the tuner is open");
    addAndMakeVisible (muteBtn);
    muteAtt = std::make_unique<BA> (st, "tuner_mute", muteBtn);

    for (int i = 0; i < 12; ++i)
    {
        auto& b = noteBtns[i];
        b.setButtonText (kNotes[i]);
        b.setClickingTogglesState (false);
        b.onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter ("tuner_note")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
        };
        addChildComponent (b);
    }
}

TunerPanel::~TunerPanel() { proc.setTunerOpen (false); }

void TunerPanel::visibilityChanged()
{
    const bool on = isVisible() && getParentComponent() != nullptr;
    proc.setTunerOpen (on);
    if (on) { toFront (false); lastTime = juce::Time::getMillisecondCounterHiRes(); startTimerHz (60); }
    else stopTimer();
}

int TunerPanel::keyShift() const
{
    static const int k[4] = { 0, 2, 7, 9 };   // written = concert + shift (C, Bb, F, Eb)
    return k[juce::jlimit (0, 3, keyBox.getSelectedItemIndex())];
}

bool TunerPanel::manualMode() const { return modeBox.getSelectedItemIndex() == 1; }

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
        hasReading = false;

    // strobe: one pattern cell per beat cycle (sharp drifts right, flat left)
    if (hasReading) strobePhase += smoothDelta * dt;
    strobePhase -= std::floor (strobePhase);

    const int sel = (int) proc.apvts.getRawParameterValue ("tuner_note")->load();
    for (int i = 0; i < 12; ++i) noteBtns[i].setToggleState (i == sel, juce::dontSendNotification);
    repaint();
}

void TunerPanel::resized()
{
    const float s = (float) getWidth() / kBase;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    readoutArea = { 0, 0, getWidth(), S (106) };
    strobeArea  = { S (24), S (110), getWidth() - S (48), S (112) };
    bottomLeft  = { S (24), S (230), S (566), S (56) };

    const int cx = S (600);
    refLabel.setBounds (cx, S (10), S (62), S (24));
    refSlider.setBounds (cx + S (66), S (10), S (246), S (24));
    offsetLabel.setBounds (cx, S (40), S (62), S (24));
    offsetSlider.setBounds (cx + S (66), S (40), S (246), S (24));
    modeBox.setBounds (cx + S (66), S (72), S (118), S (24));
    keyBox.setBounds (cx + S (194), S (72), S (118), S (24));

    tempBox.setBounds (cx + S (10), S (232), S (212), S (24));
    rootBox.setBounds (cx + S (230), S (232), S (82), S (24));
    tuningBox.setBounds (cx + S (10), S (262), S (212), S (24));
    muteBtn.setBounds (cx + S (230), S (262), S (82), S (24));

    const bool man = manualMode();
    tuningBox.setEnabled (! man);
    const float w = (bottomLeft.getWidth() - 5.0f * s * 11) / 12.0f;
    for (int i = 0; i < 12; ++i)
    {
        noteBtns[i].setVisible (man);
        noteBtns[i].setBounds (juce::Rectangle<float> (bottomLeft.getX() + i * (w + 5 * s), (float) bottomLeft.getY() + 8 * s, w, 40 * s).toNearestInt());
    }
}

void TunerPanel::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / kBase;
    g.fillAll (bg);

    const bool live = hasReading;
    const double c = smoothCents;
    const bool locked = live && std::fabs (c) < kInTuneCents;
    const juce::Colour accent = ! live ? ivory.withAlpha (0.25f) : amber;
    const int shift = keyShift();
    const bool man = manualMode();

    // ------------------------------------------------------------ readout
    auto ra = readoutArea.reduced ((int) (24 * s), (int) (10 * s));
    auto noteBox = ra.removeFromLeft ((int) (140 * s));
    g.setColour (panel);
    g.fillRoundedRectangle (noteBox.toFloat(), 8.0f * s);
    const int written = shown.midiNote + shift;
    if (live)
    {
        g.setColour (accent);
        g.setFont (juce::FontOptions (60.0f * s, juce::Font::bold));
        g.drawText (fusion::StrobeTunerCore::noteName (written), noteBox.withTrimmedRight ((int) (34 * s)), juce::Justification::centredRight);
        g.setFont (juce::FontOptions (20.0f * s, juce::Font::bold));
        g.drawText (juce::String (fusion::StrobeTunerCore::octaveOf (written)),
                    noteBox.withTrimmedLeft (noteBox.getWidth() - (int) (34 * s)).withTrimmedTop ((int) (36 * s)), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (ivory.withAlpha (0.3f));
        g.setFont (juce::FontOptions (44.0f * s, juce::Font::bold));
        g.drawText ("--", noteBox, juce::Justification::centred);
    }
    g.setColour (ivory.withAlpha (0.55f));
    g.setFont (juce::FontOptions (10.0f * s, juce::Font::bold));
    g.drawText (juce::String (man ? "MAN" : "AUTO") + "   KEY " + keyBox.getText().upToFirstOccurrenceOf (" ", false, false),
                noteBox.withTrimmedTop (noteBox.getHeight() - (int) (16 * s)), juce::Justification::centred);

    ra.removeFromLeft ((int) (16 * s));
    auto centsBox = ra.removeFromLeft ((int) (270 * s));
    g.setFont (juce::FontOptions (44.0f * s, juce::Font::bold));
    g.setColour (live ? accent : ivory.withAlpha (0.3f));
    const juce::String centsText = live ? ((c >= 0 ? "+" : "") + juce::String (c, 1) + juce::String::fromUTF8 (" \xc2\xa2")) : juce::String::fromUTF8 ("-- \xc2\xa2");
    g.drawText (centsText, centsBox.removeFromTop ((int) (52 * s)), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (14.0f * s));
    g.setColour (ivory.withAlpha (0.75f));
    g.drawText (live ? juce::String (shown.freq, 2) + " Hz   target " + juce::String (shown.refFreq, 2) + " Hz" : "Play a single note",
                centsBox.removeFromTop ((int) (20 * s)), juce::Justification::centredLeft);
    g.setColour (ivory.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f * s));
    g.drawText (tempBox.getText() + (tempBox.getSelectedItemIndex() > 0 ? " on " + rootBox.getText() : juce::String()),
                centsBox.removeFromTop ((int) (16 * s)), juce::Justification::centredLeft);

    // flat / in tune / sharp lights
    auto lights = ra.removeFromLeft ((int) (140 * s)).withSizeKeepingCentre ((int) (140 * s), (int) (40 * s));
    const float d = 16.0f * s;
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
        g.setFont (juce::FontOptions (9.5f * s, juce::Font::bold));
        g.drawText (lt[i], juce::Rectangle<float> (x - 30 * s, lights.getY() + 22 * s, 60 * s, 14 * s), juce::Justification::centred);
    }

    // ------------------------------------------- eight-octave strobe display
    const int bands = 8;
    const float gap = 2.0f * s;
    const float labelW = 26.0f * s;
    const float bh = (strobeArea.getHeight() - gap * (bands - 1)) / (float) bands;
    const float P0 = 64.0f * s;                         // pattern cell of the note's own octave
    const int oct = juce::jlimit (0, bands - 1, fusion::StrobeTunerCore::octaveOf (shown.midiNote));
    for (int b = 0; b < bands; ++b)
    {
        auto band = juce::Rectangle<float> (strobeArea.getX() + labelW, strobeArea.getY() + b * (bh + gap), strobeArea.getWidth() - labelW, bh);
        const bool active = live && b == oct;
        g.setColour (ivory.withAlpha (active ? 0.9f : 0.4f));
        g.setFont (juce::FontOptions (10.0f * s, juce::Font::bold));
        g.drawText (juce::String (b), juce::Rectangle<float> ((float) strobeArea.getX(), band.getY(), labelW - 6 * s, bh), juce::Justification::centredRight);

        g.setColour (panel);
        g.fillRoundedRectangle (band, 3.0f * s);
        if (! live) continue;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (band.toNearestInt());
        if (b < oct)
        {
            // below the note's octave a disc strobe just shows a blur
            g.setColour (accent.withAlpha (0.12f));
            g.fillRect (band);
            continue;
        }
        const float period = P0 / (float) (1 << juce::jmin (b - oct, 6));
        const float off = std::fmod ((float) (strobePhase * P0), period);
        const juce::Colour seg = accent.withAlpha (active ? 0.95f : 0.7f - 0.07f * (float) (b - oct));
        for (float x = band.getX() - period + off; x < band.getRight(); x += period)
        {
            juce::ColourGradient grad (seg.withAlpha (0.0f), x, 0, seg.withAlpha (0.0f), x + period * 0.5f, 0, false);
            grad.addColour (0.2, seg);
            grad.addColour (0.8, seg);
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> (x, band.getY() + 1.5f * s, period * 0.5f, band.getHeight() - 3 * s));
        }
    }
    g.setColour (ivory.withAlpha (0.35f));
    const float midX = strobeArea.getX() + labelW + (strobeArea.getWidth() - labelW) * 0.5f;
    g.drawLine (midX, (float) strobeArea.getY() - 2 * s, midX, (float) strobeArea.getBottom() + 2 * s, 1.0f);

    // ---------------------------------------- string guide / manual notes
    g.setColour (ivory.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.0f * s, juce::Font::bold));
    if (man)
    {
        g.drawText ("MANUAL NOTE" + juce::String (shift ? " (written, key " + keyBox.getText() + ")" : ""),
                    bottomLeft.toFloat().withHeight (10 * s).translated (0, -4 * s), juce::Justification::centredLeft);
        return;
    }
    const auto notes = currentTuningNotes();
    if (notes.empty())
    {
        g.setFont (juce::FontOptions (13.0f * s));
        g.drawText ("Chromatic: tunes any note. Pick a string guide (Gambale, standard, drop D...) on the right.", bottomLeft, juce::Justification::centredLeft);
        return;
    }
    const int count = (int) notes.size();
    const float w = (bottomLeft.getWidth() - 8 * s * (count - 1)) / (float) count;
    for (int i = 0; i < count; ++i)
    {
        auto box = juce::Rectangle<float> (bottomLeft.getX() + i * (w + 8 * s), (float) bottomLeft.getY() + 4 * s, w, (float) bottomLeft.getHeight() - 6 * s);
        const bool match = live && shown.midiNote == notes[(size_t) i];
        g.setColour (match ? accent.withAlpha (0.18f) : panel);
        g.fillRoundedRectangle (box, 6.0f * s);
        g.setColour (match ? accent : ivory.withAlpha (0.15f));
        g.drawRoundedRectangle (box, 6.0f * s, match ? 2.0f : 1.0f);
        const int wn = notes[(size_t) i] + shift;
        const juce::String nm = juce::String (fusion::StrobeTunerCore::noteName (wn)) + juce::String (fusion::StrobeTunerCore::octaveOf (wn));
        g.setColour (match ? accent : ivory.withAlpha (0.85f));
        g.setFont (juce::FontOptions (17.0f * s, juce::Font::bold));
        g.drawText (nm, box.withTrimmedBottom (12 * s), juce::Justification::centred);
        g.setColour (ivory.withAlpha (0.45f));
        g.setFont (juce::FontOptions (9.5f * s));
        g.drawText ("string " + juce::String (count - i), box.withTrimmedTop (box.getHeight() - 15 * s), juce::Justification::centred);
    }
}
