#include "PluginEditor.h"

namespace
{
    constexpr int kW = 960, kH = 470;
    // black & white theme
    const juce::Colour oxblood   { 0xff151515 };   // black tolex
    const juce::Colour oxblood2  { 0xff0a0a0a };
    const juce::Colour piping    { 0xffc9cdd1 };   // silver trim
    const juce::Colour plateHi   { 0xffe4e7ea };   // brushed silver faceplate
    const juce::Colour plateLo   { 0xffa7acb1 };
    const juce::Colour greyText  { 0xff3a3d40 };
    const juce::Colour ink       { 0xff16181a };   // dark engraving on silver   // lettering: white on black
    const juce::Colour cream     { 0xffe2e5e8 };
    const juce::Colour blueLamp  { 0xff3d8bff };

    juce::String serif() { return juce::Font::getDefaultSerifFontName(); }

    juce::Font engraveFont (float size, bool italic = false)
    {
        return juce::Font (juce::FontOptions (serif(), size, (italic ? juce::Font::italic : 0) | juce::Font::bold));
    }

    // engraved text: light highlight below, dark ink on top
    void engrave (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size,
                  juce::Justification j = juce::Justification::centred, bool italic = false)
    {
        g.setFont (engraveFont (size, italic));
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.drawText (t, r.translated (0.0f, 1.0f), j);
        g.setColour (ink);
        g.drawText (t, r, j);
    }

    void jewel (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool on)
    {
        // chrome bezel
        juce::ColourGradient bez (juce::Colour (0xfff4f4f4), c.x - r * 1.5f, c.y - r * 1.5f, juce::Colour (0xff5a5a5a), c.x + r * 1.5f, c.y + r * 1.5f, false);
        g.setGradientFill (bez);
        g.fillEllipse (c.x - r * 1.45f, c.y - r * 1.45f, r * 2.9f, r * 2.9f);
        if (on) { g.setColour (col.withAlpha (0.35f)); g.fillEllipse (c.x - r * 2.6f, c.y - r * 2.6f, r * 5.2f, r * 5.2f); }
        // faceted glass
        const auto base = on ? col : col.darker (2.2f);
        juce::ColourGradient glass (base.brighter (on ? 0.8f : 0.2f), c.x - r * 0.4f, c.y - r * 0.5f, base.darker (0.6f), c.x + r, c.y + r, true);
        g.setGradientFill (glass);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.7f : 0.35f));
        g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.6f, r * 0.5f, r * 0.4f);
    }
}

// ============================================================== LookAndFeel
AmpLookAndFeel::AmpLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, cream);
    setColour (juce::ComboBox::arrowColourId, cream);
    setColour (juce::PopupMenu::backgroundColourId, oxblood2);
    setColour (juce::PopupMenu::textColourId, cream);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cream);
    setColour (juce::PopupMenu::highlightedTextColourId, oxblood2);
    setColour (juce::TextButton::textColourOffId, cream);
    setColour (juce::TextButton::textColourOnId, oxblood2);
    setColour (juce::BubbleComponent::backgroundColourId, oxblood2);
    setColour (juce::BubbleComponent::outlineColourId, greyText);
    setColour (juce::TooltipWindow::textColourId, cream);
}

void AmpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float a0, float a1, juce::Slider& s)
{
    using namespace juce;
    auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float r = jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const bool small = s.getProperties().getWithDefault ("small", false);

    // engraved scale 0..10 on the plate
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (float) i / 10.0f * (a1 - a0);
        const float r0 = r * 0.68f, r1 = r * (i % 5 == 0 ? 0.79f : 0.75f);
        auto line = [&] (float off, Colour col, float th)
        {
            g.setColour (col);
            g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a) + off, c.x + r1 * std::sin (a), c.y - r1 * std::cos (a) + off, th);
        };
        line (1.0f, Colours::white.withAlpha (0.6f), 1.4f);
        line (0.0f, ink, i % 5 == 0 ? 1.8f : 1.2f);
        if (! small && i % 2 == 0)
        {
            const float rn = r * 0.91f;
            g.setColour (ink.withAlpha (0.85f));
            g.setFont (engraveFont (r * 0.155f));
            g.drawText (String (i), Rectangle<float> (c.x + rn * std::sin (a) - 10, c.y - rn * std::cos (a) - 7, 20, 14), Justification::centred);
        }
    }

    // machined-aluminium knob: knurled skirt + brushed cap + engraved line
    const float kr = r * 0.56f;
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - kr + 1.5f, c.y - kr + 3.5f, kr * 2, kr * 2);
    ColourGradient skirt (Colour (0xff3a3c3f), c.x - kr, c.y - kr, Colour (0xff050506), c.x + kr, c.y + kr, false);
    g.setGradientFill (skirt);
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (Colours::white.withAlpha (0.10f));
    for (int i = 0; i < 60; ++i)   // knurling
    {
        const float a = (float) i * MathConstants<float>::twoPi / 60.0f;
        g.drawLine (c.x + kr * 0.84f * std::sin (a), c.y - kr * 0.84f * std::cos (a), c.x + kr * std::sin (a), c.y - kr * std::cos (a), 0.9f);
    }
    const float cr = kr * 0.80f;
    ColourGradient cap (Colour (0xfff4f6f8), c.x - cr * 0.7f, c.y - cr * 0.8f, Colour (0xff8a8f94), c.x + cr * 0.8f, c.y + cr, false);
    cap.addColour (0.5, Colour (0xffc4c8cc));
    g.setGradientFill (cap);
    g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
    for (int ring = 1; ring < 9; ++ring)   // concentric turning marks
    {
        const float rr = cr * (float) ring / 9.0f;
        g.setColour ((ring % 2 ? Colours::white : Colours::black).withAlpha (0.06f));
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 0.8f);
    }
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 0.8f);
    // engraved indicator line
    const float l0 = cr * 0.18f, l1 = cr * 0.95f;
    g.setColour (Colour (0xff101214));
    g.drawLine (c.x + l0 * std::sin (angle), c.y - l0 * std::cos (angle), c.x + l1 * std::sin (angle), c.y - l1 * std::cos (angle), jmax (2.0f, r * 0.055f));
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillEllipse (c.x - cr * 0.6f, c.y - cr * 0.75f, cr * 0.75f, cr * 0.35f);
}

void AmpLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    juce::ColourGradient grad (on ? juce::Colour (0xffeef0f2) : juce::Colour (0xff3a3c3e), r.getX(), r.getY(),
                               on ? juce::Colour (0xff9da2a7) : juce::Colour (0xff1a1b1c), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);
    if (over) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRoundedRectangle (r, 4.0f); }
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (r, 4.0f); }
    g.setColour (piping.withAlpha (on ? 0.9f : 0.35f));
    g.drawRoundedRectangle (r, 4.0f, 1.2f);
}

void AmpLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (oxblood2);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (piping.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = w - 16.0f, ay = h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 2.5f, ax + 5, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (cream);
    g.fillPath (arrow);
}

juce::Font AmpLookAndFeel::getComboBoxFont (juce::ComboBox& b) { return juce::Font (juce::FontOptions (serif(), juce::jmin (16.0f, b.getHeight() * 0.58f), juce::Font::plain)); }
juce::Font AmpLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return engraveFont (juce::jmin (15.0f, h * 0.5f)); }

// =================================================================== Editor
juce::Slider& Fusion250Editor::makeKnob (const char* paramId, const juce::String& text, bool small)
{
    Knob k;
    k.label = text;
    k.small = small;
    k.slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    k.slider->setRotaryParameters (juce::degreesToRadians (-145.0f), juce::degreesToRadians (145.0f), true);
    k.slider->setPopupDisplayEnabled (true, true, this);
    k.slider->setDoubleClickReturnValue (true, 5.0);
    k.slider->getProperties().set ("small", small);
    addAndMakeVisible (*k.slider);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, *k.slider);
    knobs.push_back (std::move (k));
    return *knobs.back().slider;
}

Fusion250Editor::Fusion250Editor (Fusion250Processor& p) : AudioProcessorEditor (&p), proc (p), tuner (p)
{
    setLookAndFeel (&lnf);
    const char* names[5] = { "Gain", "Low", "Mid", "High", "Level" };
    const char* lead[5]  = { "l_gain", "l_low", "l_mid", "l_high", "l_level" };
    const char* clean[5] = { "c_gain", "c_low", "c_mid", "c_high", "c_level" };
    for (int i = 0; i < 5; ++i) makeKnob (lead[i], names[i]);
    makeKnob ("reverb", "Reverb");
    for (int i = 0; i < 5; ++i) makeKnob (clean[i], names[i]);
    makeKnob ("input_trim", "Input", true).setDoubleClickReturnValue (true, 0.0);
    makeKnob ("output_level", "Master", true).setDoubleClickReturnValue (true, 0.0);

    channelBtn.onClick = [this]
    {
        if (auto* prm = proc.apvts.getParameter ("channel"))
            prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
    };
    addAndMakeVisible (channelBtn);

    reverbBtn.setTooltip ("Reverb on/off (footswitch)");
    addAndMakeVisible (reverbBtn);
    revAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "reverb_on", reverbBtn);

    padBtn.setClickingTogglesState (true);
    addAndMakeVisible (padBtn);
    padAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "preout_pad", padBtn);

    auto setupBox = [this] (juce::ComboBox& box, const char* id)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
            box.addItemList (c->choices, 1);
        addAndMakeVisible (box);
    };
    setupBox (guitarBox, "guitar");
    setupBox (outputBox, "output");
    setupBox (cabBox, "cabinet");
    setupBox (micBox, "mic");
    cabAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "cabinet", cabBox);
    micAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mic", micBox);
    guitarAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "guitar", guitarBox);
    outputAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "output", outputBox);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    tunerBtn.setClickingTogglesState (true);
    tunerBtn.setTooltip ("Strobe tuner (0.1 cent display)");
    tunerBtn.onClick = [this]
    {
        tuner.setVisible (tunerBtn.getToggleState());
        tuner.toFront (false);   // cover the power switch and footswitch
    };
    addAndMakeVisible (tunerBtn);
    addChildComponent (tuner);

    irBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a cabinet impulse response", juce::File(), "*.wav;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                if (proc.loadImpulseResponse (fc.getResult()))
                    if (auto* prm = proc.apvts.getParameter ("output")) prm->setValueNotifyingHost (prm->convertTo0to1 (2.0f));
            });
    };
    addAndMakeVisible (irBtn);
    irLabel.setColour (juce::Label::textColourId, cream.withAlpha (0.65f));
    irLabel.setFont (juce::FontOptions (serif(), 12.0f, juce::Font::italic));
    addAndMakeVisible (irLabel);

    powerBtn.setTooltip ("Power: off silences the amp (the tuner keeps working)");
    powerBtn.onStateChange = [this] { repaint(); };
    addAndMakeVisible (powerBtn);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    inMeter.setTooltip ("Input level (after the Input knob). Click to reset the clip light");
    outMeter.setTooltip ("Output level. Click to reset the clip light");
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "power", powerBtn);

    setResizable (true, true);
    if (auto* c = getConstrainer()) { c->setFixedAspectRatio ((double) kW / kH); c->setSizeLimits (kW * 2 / 3, kH * 2 / 3, kW * 2, kH * 2); }
    setSize (kW, kH);
    startTimerHz (30);
    timerCallback();
}

Fusion250Editor::~Fusion250Editor()
{
    tuner.setVisible (false);
    proc.setTunerOpen (false);
    setLookAndFeel (nullptr);
}

void Fusion250Editor::timerCallback()
{
    inMeter.push (proc.takeInputPeak(), 1.0 / 30.0);
    outMeter.push (proc.takeOutputPeak(), 1.0 / 30.0);
    const bool lead = proc.apvts.getRawParameterValue ("channel")->load() > 0.5f;
    if (lead != leadOn) { leadOn = lead; repaint(); }
    channelBtn.setButtonText (leadOn ? "LEAD" : "CLEAN");
    const auto ir = proc.getIrName();
    irLabel.setText (ir.isEmpty() ? "No IR loaded: Output > Built-in Cab uses the Cabinet menu" : "IR: " + ir, juce::dontSendNotification);
}

void Fusion250Editor::rebuildTextures()
{
    juce::Random rng (250);
    // brushed silver faceplate
    brushed = juce::Image (juce::Image::RGB, juce::jmax (1, plate.getWidth()), juce::jmax (1, plate.getHeight()), true);
    {
        juce::Graphics g (brushed);
        juce::ColourGradient grad (plateHi, 0, 0, plateLo, 0, (float) plate.getHeight(), false);
        grad.addColour (0.45, plateHi.interpolatedWith (plateLo, 0.35f));
        g.setGradientFill (grad);
        g.fillAll();
        // brushed aluminium: fine horizontal grain
        const int W = plate.getWidth(), H = plate.getHeight();
        for (int y = 0; y < H; ++y)
            for (int k = 0; k < 3; ++k)
            {
                g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (rng.nextFloat() * 0.07f));
                const float x0 = rng.nextFloat() * W * 0.6f;
                g.drawHorizontalLine (y, x0, x0 + W * (0.2f + rng.nextFloat() * 0.6f));
            }
    }
    // tolex grain
    tolex = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
    {
        juce::Graphics g (tolex);
        g.fillAll (oxblood);
        for (int i = 0; i < getWidth() * getHeight() / 9; ++i)
        {
            const int x = rng.nextInt (getWidth()), y = rng.nextInt (getHeight());
            g.setColour ((rng.nextBool() ? juce::Colours::black : juce::Colour (0xff2e2e2e)).withAlpha (0.3f + rng.nextFloat() * 0.4f));
            g.fillRect (x, y, 1 + rng.nextInt (2), 1);
        }
    }
}

void Fusion250Editor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / kW;
    g.drawImageAt (tolex, 0, 0);

    // nameplate
    auto np = juce::Rectangle<float> (16 * s, 9 * s, 340 * s, 34 * s);
    {   // brushed-aluminium badge
        juce::ColourGradient al (juce::Colour (0xff8d9297), np.getX(), np.getY(), juce::Colour (0xff4a4e52), np.getX(), np.getBottom(), false);
        al.addColour (0.5, juce::Colour (0xff6c7176));
        g.setGradientFill (al);
        g.fillRoundedRectangle (np, 5 * s);
        juce::Random rr (7);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (np.toNearestInt());
        for (float y = np.getY(); y < np.getBottom(); y += 1.0f)
        {
            g.setColour ((rr.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (rr.nextFloat() * 0.08f));
            g.drawHorizontalLine ((int) y, np.getX(), np.getRight());
        }
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (np.reduced (2.5f * s), 4 * s, 1.2f * s);
    g.setFont (engraveFont (24 * s, true));
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawText ("Basemental FG 250", np.withTrimmedLeft (12 * s).translated (0, 1.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff0c0d0e));
    g.drawText ("Basemental FG 250", np.withTrimmedLeft (12 * s), juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (serif(), 10.5f * s, juce::Font::bold)));
    g.setColour (juce::Colour (0xff141516));
    g.drawText ("TWO CHANNEL", np.withTrimmedRight (12 * s).withTrimmedTop (2 * s), juce::Justification::centredRight);

    // piping around the plate, then the brushed plate itself
    const auto pf = plate.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (pf.expanded (4 * s).translated (0, 2 * s), 7 * s);
    g.setColour (piping);
    g.drawRoundedRectangle (pf.expanded (4 * s), 7 * s, 2.0f * s);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (pf, 5 * s);
        g.reduceClipRegion (clip);
        g.drawImageAt (brushed, plate.getX(), plate.getY());
        // corner screws
        for (auto pt : { pf.getTopLeft(), pf.getTopRight(), pf.getBottomLeft(), pf.getBottomRight() })
        {
            const auto c = pt + juce::Point<float> (pt.x < pf.getCentreX() ? 11 * s : -11 * s, pt.y < pf.getCentreY() ? 11 * s : -11 * s);
            juce::ColourGradient sg (juce::Colour (0xfff2f2f2), c.x - 4 * s, c.y - 4 * s, juce::Colour (0xff5e5e5e), c.x + 4 * s, c.y + 4 * s, false);
            g.setGradientFill (sg);
            g.fillEllipse (c.x - 4.5f * s, c.y - 4.5f * s, 9 * s, 9 * s);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawLine (c.x - 3 * s, c.y - 1.5f * s, c.x + 3 * s, c.y + 1.5f * s, 1.1f * s);
        }
    }

    // engraved row divider
    const float divY = (float) cleanBand.getY();
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawHorizontalLine ((int) divY + 1, pf.getX() + 130 * s, pf.getX() + 826 * s);
    g.setColour (ink.withAlpha (0.55f));
    g.drawHorizontalLine ((int) divY, pf.getX() + 130 * s, pf.getX() + 826 * s);
    // power section: vertical divider, captions, blue ON lamp
    g.setColour (ink.withAlpha (0.55f));
    g.drawVerticalLine ((int) (pf.getX() + 836 * s), pf.getY() + 16 * s, pf.getBottom() - 16 * s);
    const auto pb = powerBtn.getBounds().toFloat();
    engrave (g, "POWER", { pb.getX() - 20 * s, pb.getY() - 22 * s, pb.getWidth() + 40 * s, 18 * s }, 14.5f * s);
    const juce::Point<float> lamp (pb.getCentreX(), (float) cleanBand.getCentreY() + 8 * s);
    engrave (g, "ON", { lamp.x - 30 * s, lamp.y - 34 * s, 60 * s, 18 * s }, 14.5f * s);
    jewel (g, lamp, 7.5f * s, blueLamp, powerBtn.getToggleState());

    // row titles + jewel lamps
    auto rowTitle = [&] (juce::Rectangle<int> band, bool isLead)
    {
        const float cy = (float) band.getCentreY();
        engrave (g, isLead ? "Lead" : "Clean", juce::Rectangle<float> (20 * s, cy - 40 * s, 110 * s, 26 * s), 22 * s, juce::Justification::centred, true);
        jewel (g, { 75 * s, cy - 2 * s }, 7.5f * s, isLead ? juce::Colour (0xffff3a2a) : juce::Colour (0xff4cff5a), isLead == leadOn);
    };
    rowTitle (leadBand, true);
    rowTitle (cleanBand, false);

    // knob captions
    for (auto& k : knobs)
    {
        if (k.small) continue;
        const auto b = k.slider->getBounds().toFloat();
        engrave (g, k.label.toUpperCase(), { b.getX() - 20 * s, b.getY() - 22 * s, b.getWidth() + 40 * s, 18 * s }, 14.5f * s);
    }

    // footer groups
    auto group = [&] (juce::Rectangle<int> r, const char* title)
    {
        const auto rf = r.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (rf, 6 * s);
        g.setColour (piping.withAlpha (0.3f));
        g.drawRoundedRectangle (rf, 6 * s, 1.0f);
        g.setColour (cream);
        g.setFont (engraveFont (11 * s, true));
        g.drawText (title, rf.withHeight (16 * s).translated (10 * s, 3 * s), juce::Justification::centredLeft);
    };
    group (guitarGroup, "Guitar");
    group (speakerGroup, "Speaker");
    group (levelGroup, "Levels");
    // reverb footswitch caption
    {
        const auto rb = reverbBtn.getBounds().toFloat();
        engrave (g, "REVERB", { rb.getX() - 20 * s, rb.getY() - 22 * s, rb.getWidth() + 40 * s, 18 * s }, 14.5f * s);
    }

    // footer captions (on tolex, so cream)
    g.setColour (cream.withAlpha (0.8f));
    g.setFont (engraveFont (11.5f * s));
    const std::pair<const char*, juce::Component*> caps[] = { { "GUITAR VOICE", &guitarBox }, { "CABINET", &cabBox },
                                                              { "MIC", &micBox }, { "OUTPUT", &outputBox } };
    for (auto& [text, comp] : caps)
        g.drawText (text, juce::Rectangle<float> ((float) comp->getX() + 2 * s, (float) comp->getY() - 16 * s, 200 * s, 14 * s), juce::Justification::centredLeft);
    for (int i = 11; i < 13; ++i)
    {
        const auto b = knobs[(size_t) i].slider->getBounds().toFloat();
        g.drawText (knobs[(size_t) i].label.toUpperCase(), juce::Rectangle<float> (b.getX() - 20 * s, b.getY() - 15 * s, b.getWidth() + 40 * s, 14 * s), juce::Justification::centred);
    }
}

void Fusion250Editor::resized()
{
    const float s = (float) getWidth() / kW;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };

    header = { 0, 0, getWidth(), S (52) };
    plate  = { S (12), S (56), getWidth() - S (24), S (296) };
    leadBand  = { plate.getX(), plate.getY(), plate.getWidth(), plate.getHeight() / 2 };
    cleanBand = { plate.getX(), plate.getY() + plate.getHeight() / 2, plate.getWidth(), plate.getHeight() - plate.getHeight() / 2 };
    footer = { 0, plate.getBottom() + S (6), getWidth(), getHeight() - plate.getBottom() - S (6) };
    rebuildTextures();

    presetBox.setBounds (getWidth() - S (300), S (12), S (280), S (28));
    tunerBtn.setBounds (getWidth() - S (400), S (12), S (86), S (28));
    tuner.setBounds (plate);
    channelBtn.setBounds (S (30), cleanBand.getY() - S (17), S (90), S (34));

    const int x0 = S (136), colW = S (115), knobSize = S (114);
    powerBtn.setBounds (plate.getX() + S (852), leadBand.getY() + S (40), S (62), S (82));
    auto placeKnob = [&] (Knob& k, juce::Rectangle<int> band, int col)
    {
        const int cx = x0 + col * colW + colW / 2;
        k.slider->setBounds (cx - knobSize / 2, band.getY() + S (30), knobSize, knobSize);
    };
    for (int i = 0; i < 6; ++i) placeKnob (knobs[(size_t) i], leadBand, i);
    for (int i = 0; i < 5; ++i) placeKnob (knobs[(size_t) (6 + i)], cleanBand, i);

    const int revCx = x0 + 5 * colW + colW / 2;
    reverbBtn.setBounds (revCx - S (34), cleanBand.getY() + S (30), S (68), S (100));

    // footer: three grouped panels
    const int gy = footer.getY() + S (4), gh = footer.getHeight() - S (10);
    guitarGroup  = { S (14), gy, S (226), gh };
    speakerGroup = { S (248), gy, S (478), gh };
    levelGroup   = { S (734), gy, S (212), gh };
    const int fy = gy + S (40), fy2 = gy + S (74);
    guitarBox.setBounds (guitarGroup.getX() + S (12), fy, S (202), S (26));
    cabBox.setBounds (speakerGroup.getX() + S (12), fy, S (150), S (26));
    micBox.setBounds (speakerGroup.getX() + S (170), fy, S (118), S (26));
    outputBox.setBounds (speakerGroup.getX() + S (296), fy, S (170), S (26));
    padBtn.setBounds (speakerGroup.getX() + S (296), fy2, S (58), S (24));
    irBtn.setBounds (speakerGroup.getX() + S (360), fy2, S (106), S (24));
    irLabel.setBounds (speakerGroup.getX() + S (10), fy2, S (280), S (24));
    inMeter.setBounds (levelGroup.getX() + S (152), gy + S (8), S (24), gh - S (14));
    outMeter.setBounds (levelGroup.getX() + S (180), gy + S (8), S (24), gh - S (14));

    auto smallKnob = [&] (Knob& k, int cx)
    {
        const int sz = S (62);
        k.slider->setBounds (cx - sz / 2, gy + S (38), sz, sz);
    };
    smallKnob (knobs[11], levelGroup.getX() + S (38));
    smallKnob (knobs[12], levelGroup.getX() + S (106));
}

// =================================================================== rocker
void RockerSwitch::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = getToggleState();
    // bezel
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0, 2.0f), 5.0f);
    juce::ColourGradient bez (juce::Colour (0xff5a5a5a), r.getX(), r.getY(), juce::Colour (0xff0c0c0c), r.getX(), r.getBottom(), false);
    g.setGradientFill (bez);
    g.fillRoundedRectangle (r, 5.0f);
    auto face = r.reduced (r.getWidth() * 0.12f, r.getHeight() * 0.09f);
    g.setColour (juce::Colour (0xff050505));
    g.fillRoundedRectangle (face, 3.0f);
    // rocker: the pressed half sits low and dark, the raised half catches the light
    auto top = face.withHeight (face.getHeight() * 0.5f), bottom = face.withTrimmedTop (face.getHeight() * 0.5f);
    auto raised = on ? top : bottom, pressed = on ? bottom : top;
    juce::ColourGradient rg (juce::Colour (0xff3e3e3e), raised.getX(), on ? raised.getY() : raised.getBottom(),
                             juce::Colour (0xff1a1a1a), raised.getX(), on ? raised.getBottom() : raised.getY(), false);
    g.setGradientFill (rg);
    g.fillRoundedRectangle (raised.reduced (2.0f, 1.0f), 2.5f);
    g.setColour (juce::Colour (0xff101010));
    g.fillRoundedRectangle (pressed.reduced (2.0f, 1.0f), 2.5f);
    if (over) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRoundedRectangle (face, 3.0f); }
    // I / O marks
    g.setColour (juce::Colour (0xfff7f7f7).withAlpha (0.85f));
    const float m = face.getWidth() * 0.12f;
    g.fillRoundedRectangle (juce::Rectangle<float> (m * 0.35f, face.getHeight() * 0.18f).withCentre (top.getCentre()), 1.0f);
    g.drawEllipse (juce::Rectangle<float> (face.getWidth() * 0.22f, face.getWidth() * 0.22f).withCentre (bottom.getCentre()), 1.6f);
}

// =============================================================== footswitch
void FootswitchButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto b = getLocalBounds().toFloat();
    const bool on = getToggleState();
    // status LED above the switch
    const juce::Point<float> led (b.getCentreX(), b.getY() + b.getHeight() * 0.12f);
    const float lr = b.getWidth() * 0.09f;
    if (on) { g.setColour (juce::Colour (0xffff3a2a).withAlpha (0.35f)); g.fillEllipse (led.x - lr * 2.4f, led.y - lr * 2.4f, lr * 4.8f, lr * 4.8f); }
    g.setColour (on ? juce::Colour (0xffff4a3a) : juce::Colour (0xff3a1210));
    g.fillEllipse (led.x - lr, led.y - lr, lr * 2, lr * 2);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.6f : 0.2f));
    g.fillEllipse (led.x - lr * 0.5f, led.y - lr * 0.6f, lr * 0.6f, lr * 0.45f);

    // hex nut + chrome stomp cap
    const juce::Point<float> c (b.getCentreX(), b.getY() + b.getHeight() * 0.62f);
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.42f;
    juce::Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const float a = juce::MathConstants<float>::pi / 3.0f * (float) i;
        const auto p = c + juce::Point<float> (R * std::cos (a), R * std::sin (a));
        if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
    }
    hex.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (hex, juce::AffineTransform::translation (0, 3.0f));
    juce::ColourGradient nut (juce::Colour (0xffdedede), c.x - R, c.y - R, juce::Colour (0xff4a4a4a), c.x + R, c.y + R, false);
    g.setGradientFill (nut);
    g.fillPath (hex);
    const float cr = R * (down ? 0.60f : 0.64f);
    juce::ColourGradient cap (juce::Colour (0xffffffff), c.x - cr * 0.5f, c.y - cr * 0.7f, juce::Colour (0xff6a6a6a), c.x + cr * 0.6f, c.y + cr, true);
    g.setGradientFill (cap);
    g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawEllipse (c.x - cr * 0.62f, c.y - cr * 0.62f, cr * 1.24f, cr * 1.24f, 1.2f);
    if (over) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2); }
}

// ================================================================== meters
void LevelMeter::push (float peak, double dt)
{
    const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - (float) (dt * 26.0));   // ~26 dB/s fall
    if (db >= holdDb) { holdDb = db; holdAge = 0; }
    else if ((holdAge += dt) > 1.5) holdDb = juce::jmax (levelDb, holdDb - (float) (dt * 40.0));
    if (db >= -0.1f) clipped = true;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float labelH = 12.0f, clipH = 7.0f;
    auto clip = r.removeFromTop (clipH).reduced (2.0f, 0.5f);
    g.setColour (clipped ? juce::Colour (0xffff3a2a) : juce::Colour (0xff3a1210));
    g.fillRoundedRectangle (clip, 2.0f);
    auto lab = r.removeFromBottom (labelH);
    g.setColour (juce::Colour (0xffe2e5e8).withAlpha (0.8f));
    g.setFont (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 9.5f, juce::Font::bold));
    g.drawText (label, lab, juce::Justification::centred);

    auto bar = r.reduced (3.0f, 2.0f);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (bar.expanded (1.5f), 2.0f);
    const int segs = 21;
    const float gap = 1.0f, sh = (bar.getHeight() - gap * (segs - 1)) / segs;
    auto toFrac = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f); };   // -60..+3 dB
    for (int i = 0; i < segs; ++i)
    {
        const float lo = (float) i / segs;                     // segment from the bottom
        const float segDb = lo * 63.0f - 60.0f;
        const juce::Colour col = segDb >= -3.0f ? juce::Colour (0xffff3a2a) : segDb >= -12.0f ? juce::Colour (0xffffc23a) : juce::Colour (0xff4cff5a);
        const bool lit = toFrac (levelDb) > lo;
        const bool hold = std::abs (toFrac (holdDb) - lo) < 0.5f / segs && holdDb > -60.0f;
        const auto seg = juce::Rectangle<float> (bar.getX(), bar.getBottom() - (i + 1) * sh - i * gap, bar.getWidth(), sh);
        g.setColour (lit || hold ? col : col.darker (3.0f).withAlpha (0.6f));
        g.fillRect (seg);
    }
}
