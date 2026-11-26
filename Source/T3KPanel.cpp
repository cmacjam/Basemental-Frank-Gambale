#include "T3KPanel.h"
#include "T3KConfig.h"

namespace
{
    const juce::Colour panelBg  { 0xff1b1d21 };
    const juce::Colour panelBg2 { 0xff0f1013 };
    const juce::Colour silver   { 0xffd6dade };
    const juce::Colour lilac    { 0xffb79ae8 };
    const juce::Colour warn     { 0xffff8a7a };
    constexpr float kRefW = 936.0f;

    juce::Font serifBold (float size, bool italic = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), size,
                                              juce::Font::bold | (italic ? juce::Font::italic : 0)));
    }

    void setParam (Fusion250Processor& p, const char* id, float plainValue)
    {
        if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
    }
}

bool T3KPanel::browseAvailable()
{
    return juce::String (t3k::config::publishableKey).startsWith ("t3k_pub_");
}

T3KPanel::T3KPanel (Fusion250Processor& p) : proc (p)
{
    setOpaque (true);
    proc.tone3000().addChangeListener (this);

    for (auto* b : { &ampFg, &ampNam, &cabFg, &cabIr })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    ampFg.setTooltip ("Use the FG 250's own Clean/Lead channels");
    ampNam.setTooltip ("Use the loaded NAM capture as the amp (the FG 250 channel knobs are bypassed)");
    cabFg.setTooltip ("Use the built-in cabinet (Speaker > Cabinet)");
    cabIr.setTooltip ("Use the loaded IR as the cabinet");
    ampFg.onClick  = [this] { setParam (proc, "amp_source", 0.0f); };
    ampNam.onClick = [this]
    {
        if (! proc.externalTone().ampLoaded()) { setStatus ("Load a NAM capture first (Browse TONE3000 or Load .nam).", true); return; }
        setParam (proc, "amp_source", 1.0f);
    };
    cabFg.onClick = [this] { setParam (proc, "output", 1.0f); };
    cabIr.onClick = [this]
    {
        if (proc.getIrName().isEmpty()) { setStatus ("Load an IR first (Browse TONE3000 or Load IR).", true); return; }
        setParam (proc, "output", 2.0f);
    };

    ampBrowse.onClick = [this] { browse (Slot::amp); };
    cabBrowse.onClick = [this] { browse (Slot::cab); };
    ampFile.onClick = [this] { loadFile (Slot::amp); };
    cabFile.onClick = [this] { loadFile (Slot::cab); };
    ampClear.onClick = [this] { proc.clearNam(); setStatus ("NAM capture cleared: the FG 250 channels are back."); };
    cabClear.onClick = [this] { proc.clearImpulseResponse(); setStatus ("IR cleared: the built-in cabinet is back."); };
    for (auto* b : { &ampBrowse, &cabBrowse })
    {
        b->setEnabled (browseAvailable());
        b->setTooltip (browseAvailable() ? "Pick a tone on tone3000.com (opens your browser) and load it here"
                                         : "This build has no TONE3000 key yet. Load files from disk instead (see the manual)");
        addAndMakeVisible (*b);
    }
    ampFile.setTooltip ("Load a .nam file from disk (e.g. the FG 250 NAM pack)");
    cabFile.setTooltip ("Load a .wav impulse response from disk");
    for (auto* b : { &ampFile, &cabFile, &ampClear, &cabClear }) addAndMakeVisible (*b);

    liteBtn.setClickingTogglesState (false);
    liteBtn.setTooltip ("A2 captures hold a Full and a Lite size. Lite uses far less CPU, with a little less detail");
    liteBtn.onClick = [this] { setParam (proc, "nam_size", proc.apvts.getRawParameterValue ("nam_size")->load() > 0.5f ? 0.0f : 1.0f); };
    addAndMakeVisible (liteBtn);
    normBtn.setClickingTogglesState (true);
    normBtn.setTooltip ("Bring every capture to the same loudness (NAM's -18 dB standard)");
    addAndMakeVisible (normBtn);
    normAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "nam_normalize", normBtn);

    accountBtn.onClick = [this]
    {
        if (proc.tone3000().isConnected()) { proc.tone3000().clearTokens(); accountName.clear(); setStatus ("Disconnected from TONE3000."); return; }
        if (! browseAvailable()) { setStatus ("This build has no TONE3000 key, so it can't connect. You can still load files.", true); return; }
        flow = t3k::makeAuthFlow (t3k::FlowMode::systemBrowser);
        setStatus ("Sign in to TONE3000 in your browser, then come back here.");
        t3k::FlowRequest req;
        flow->start (req, scope.wrap ([this] (t3k::FlowResult r)
        {
            if (r.connected()) { setStatus ("Connected to TONE3000."); refreshAccount(); }
            else if (! r.canceled()) setStatus ("TONE3000 sign-in failed: " + r.error, true);
        }));
    };
    addAndMakeVisible (accountBtn);

    for (auto* l : { &ampLink, &cabLink })
    {
        l->setFont (serifBold (12.0f, true), false, juce::Justification::centredLeft);
        l->setColour (juce::HyperlinkButton::textColourId, lilac);
        addChildComponent (*l);
    }
    refreshAccount();
}

T3KPanel::~T3KPanel()
{
    proc.tone3000().removeChangeListener (this);
    if (flow != nullptr && flow->isActive()) flow->cancel();
}

void T3KPanel::changeListenerCallback (juce::ChangeBroadcaster*) { refreshAccount(); }

void T3KPanel::refreshAccount()
{
    auto& c = proc.tone3000();
    accountBtn.setButtonText (c.isConnected() ? "DISCONNECT" : "CONNECT");
    if (c.isConnected() && accountName.isEmpty())
        c.getUser (scope.wrap ([this] (t3k::Result<t3k::User> u) { if (u.ok()) { accountName = u->creatorName(); repaint(); } }));
    if (! c.isConnected()) accountName.clear();
    repaint();
}

void T3KPanel::visibilityChanged()
{
    if (isVisible()) { toFront (false); startTimerHz (10); timerCallback(); } else stopTimer();
}

void T3KPanel::timerCallback()
{
    const bool namOn = proc.apvts.getRawParameterValue ("amp_source")->load() > 0.5f && proc.externalTone().ampLoaded();
    const bool irOn  = (int) proc.apvts.getRawParameterValue ("output")->load() == 2 && proc.getIrName().isNotEmpty();
    ampFg.setToggleState (! namOn, juce::dontSendNotification);
    ampNam.setToggleState (namOn, juce::dontSendNotification);
    cabFg.setToggleState (! irOn && (int) proc.apvts.getRawParameterValue ("output")->load() == 1, juce::dontSendNotification);
    cabIr.setToggleState (irOn, juce::dontSendNotification);
    liteBtn.setToggleState (proc.apvts.getRawParameterValue ("nam_size")->load() > 0.5f, juce::dontSendNotification);
    ampClear.setEnabled (proc.externalTone().ampLoaded());
    cabClear.setEnabled (proc.getIrName().isNotEmpty());

    auto link = [] (juce::HyperlinkButton& l, const Fusion250Processor::ToneSource& s, bool loaded)
    {
        const bool show = loaded && s.url.isNotEmpty();
        if (show)
        {
            l.setButtonText (s.creator.isNotEmpty() ? "by " + s.creator + " on TONE3000" : "on TONE3000");
            l.setURL (juce::URL (s.url));
        }
        l.setVisible (show);
    };
    link (ampLink, proc.namSource, proc.externalTone().ampLoaded());
    link (cabLink, proc.irSource, proc.getIrName().isNotEmpty());
    repaint();
}

void T3KPanel::setStatus (const juce::String& s, bool error)
{
    status = s; statusIsError = error;
    repaint();
}

// ------------------------------------------------------------------ TONE3000 browse
void T3KPanel::browse (Slot slot)
{
    if (! browseAvailable()) { setStatus ("This build has no TONE3000 key. Use Load .nam / Load IR instead.", true); return; }
    if (busy) return;
    if (flow != nullptr && flow->isActive()) flow->cancel();

    t3k::FlowRequest req;
    req.kind = t3k::FlowRequest::Kind::selectTone;
    req.catalog.preview = true;
    req.catalog.architecture = 2;
    if (slot == Slot::amp) { req.catalog.gears = "amp_amp-cab"; req.catalog.format = "nam"; }
    else                   { req.catalog.gears = "cab";         req.catalog.format = "ir"; }

    flow = t3k::makeAuthFlow (t3k::FlowMode::systemBrowser);
    setStatus (slot == Slot::amp ? "Pick an amp capture on TONE3000 in your browser..." : "Pick a cabinet IR on TONE3000 in your browser...");
    flow->start (req, scope.wrap ([this, slot] (t3k::FlowResult r)
    {
        refreshAccount();
        if (r.canceled() || (r.connected() && r.toneId.isEmpty())) { setStatus ("No tone picked."); return; }
        if (! r.connected()) { setStatus ("TONE3000: " + r.error, true); return; }
        loadTone (slot, r.toneId.getIntValue());
    }));
}

void T3KPanel::loadTone (Slot slot, int toneId)
{
    busy = true;
    setStatus ("Getting the tone from TONE3000...");
    proc.tone3000().getToneWithModels (toneId, scope.wrap ([this, slot] (t3k::Result<t3k::ToneWithModels> r)
    {
        busy = false;
        if (! r.ok()) { setStatus (r.error.userMessage ("Loading the tone"), true); return; }
        pickModel (slot, *r);
    }));
}

void T3KPanel::pickModel (Slot slot, const t3k::ToneWithModels& t)
{
    const juce::String ext = slot == Slot::amp ? ".nam" : ".wav";
    juce::Array<t3k::Model> usable;
    for (auto& m : t.models)
        if (m.extension().equalsIgnoreCase (ext)) usable.add (m);

    if (usable.isEmpty())
    {
        setStatus ("\"" + t.tone.title + "\" has no " + (slot == Slot::amp ? juce::String ("A2 NAM captures") : juce::String ("WAV IRs")) + " this plug-in can load.", true);
        return;
    }
    if (usable.size() == 1) { downloadAndLoad (slot, t.tone, usable.getFirst()); return; }

    // a tone often holds several captures (gain settings, channels) or mic positions
    juce::PopupMenu menu;
    menu.addSectionHeader (t.tone.title);
    for (int i = 0; i < usable.size(); ++i)
        menu.addItem (i + 1, usable[i].name + (usable[i].size.isNotEmpty() ? "  (" + usable[i].size + ")" : juce::String()));
    auto* target = slot == Slot::amp ? &ampBrowse : &cabBrowse;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
        scope.wrap ([this, slot, tone = t.tone, usable] (int choice)
        {
            if (choice > 0) downloadAndLoad (slot, tone, usable[choice - 1]);
            else setStatus ("No capture picked.");
        }));
}

void T3KPanel::downloadAndLoad (Slot slot, const t3k::Tone& tone, const t3k::Model& model)
{
    busy = true;
    setStatus ("Downloading \"" + model.name + "\"...");
    proc.tone3000().downloadModelFile (model, scope.wrap ([this, slot, tone, model] (t3k::Result<juce::File> f)
    {
        busy = false;
        if (! f.ok()) { setStatus (f.error.userMessage ("Download"), true); return; }

        Fusion250Processor::ToneSource src;
        src.title = tone.title + (model.name.isNotEmpty() && model.name != tone.title ? "  -  " + model.name : juce::String());
        src.creator = tone.user.creatorName();
        src.url = tone.url;
        src.toneId = tone.id;

        if (slot == Slot::cab)
        {
            if (proc.loadIrFile (*f, src)) setStatus ("Loaded IR \"" + model.name + "\" by " + src.creator + ".");
            else setStatus ("Couldn't load that IR.", true);
            return;
        }
        const bool fullRig = tone.gear == "amp-cab";
        setStatus ("Loading the capture...");
        proc.loadNamFile (*f, src, scope.wrap ([this, fullRig, src] (fusion::ExternalTone::NamInfo info)
        {
            if (info.error.isNotEmpty()) { setStatus (info.error, true); return; }
            if (fullRig)
            {
                // an amp + cab capture already has its cabinet: run it without one
                setParam (proc, "output", 0.0f);
                setStatus ("Loaded \"" + src.title + "\". It's a full-rig capture, so the cabinet is switched off (Output: Pre Out).");
            }
            else setStatus ("Loaded \"" + src.title + "\" by " + src.creator + ".");
        }));
    }));
}

// ------------------------------------------------------------------- files from disk
void T3KPanel::loadFile (Slot slot)
{
    const bool amp = slot == Slot::amp;
    chooser = std::make_unique<juce::FileChooser> (amp ? "Choose a NAM capture" : "Choose a cabinet impulse response",
                                                   juce::File(), amp ? "*.nam" : "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, amp] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile()) return;
            if (! amp)
            {
                if (proc.loadIrFile (file, {})) setStatus ("Loaded IR \"" + file.getFileNameWithoutExtension() + "\".");
                else setStatus ("Couldn't load that IR.", true);
                return;
            }
            setStatus ("Loading \"" + file.getFileNameWithoutExtension() + "\"...");
            proc.loadNamFile (file, {}, scope.wrap ([this] (fusion::ExternalTone::NamInfo info)
            {
                if (info.error.isNotEmpty()) setStatus (info.error, true);
                else setStatus ("Loaded \"" + info.name + "\".");
            }));
        });
}

// ----------------------------------------------------------------------------- layout
void T3KPanel::resized()
{
    const float s = (float) getWidth() / kRefW;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    accountBtn.setBounds (S (790), S (14), S (122), S (26));

    auto column = [&] (int x, juce::TextButton& a, juce::TextButton& b, juce::TextButton& br,
                       juce::TextButton& file, juce::TextButton& clear, juce::HyperlinkButton& link)
    {
        a.setBounds (x, S (86), S (120), S (28));
        b.setBounds (x + S (126), S (86), S (140), S (28));
        link.setBounds (x, S (150), S (420), S (18));
        br.setBounds (x, S (178), S (176), S (30));
        file.setBounds (x + S (184), S (178), S (124), S (30));
        clear.setBounds (x + S (316), S (178), S (90), S (30));
    };
    column (S (26), ampFg, ampNam, ampBrowse, ampFile, ampClear, ampLink);
    column (S (490), cabFg, cabIr, cabBrowse, cabFile, cabClear, cabLink);
    liteBtn.setBounds (S (26), S (218), S (80), S (24));
    normBtn.setBounds (S (112), S (218), S (120), S (24));
}

void T3KPanel::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / kRefW;
    juce::ColourGradient bg (panelBg, 0, 0, panelBg2, 0, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();
    g.setColour (silver.withAlpha (0.25f));
    g.drawRect (getLocalBounds().reduced ((int) (6 * s)), 1);
    g.drawVerticalLine ((int) (468 * s), 70 * s, 250 * s);

    auto text = [&] (const juce::String& t, juce::Rectangle<float> r, float size, bool italic = false,
                     juce::Justification j = juce::Justification::centredLeft, juce::Colour c = silver, float alpha = 1.0f)
    {
        g.setFont (serifBold (size * s, italic));
        g.setColour (c.withAlpha (alpha));
        g.drawFittedText (t, r.toNearestInt(), j, 1, 0.8f);
    };
    text ("TONE3000", { 24 * s, 12 * s, 200 * s, 30 * s }, 24, true);
    text ("NAM CAPTURES  /  IMPULSE RESPONSES", { 190 * s, 16 * s, 360 * s, 24 * s }, 11, false, juce::Justification::centredLeft, silver, 0.6f);

    auto& c = proc.tone3000();
    const juce::String acct = ! browseAvailable() ? "Files only (no TONE3000 key in this build)"
                            : c.isConnected() ? "Signed in" + (accountName.isNotEmpty() ? " as " + accountName : juce::String())
                                              : "Not signed in";
    text (acct, { 470 * s, 16 * s, 312 * s, 22 * s }, 11.5f, false, juce::Justification::centredRight, silver, 0.75f);

    auto slotInfo = [&] (float x, const char* title, const juce::String& name, const juce::String& detail)
    {
        text (title, { x, 58 * s, 300 * s, 22 * s }, 14);
        text (name.isNotEmpty() ? name : juce::String ("Nothing loaded"), { x, 120 * s, 420 * s, 26 * s }, 15, ! name.isNotEmpty(),
              juce::Justification::centredLeft, silver, name.isNotEmpty() ? 1.0f : 0.45f);
        if (detail.isNotEmpty())
            text (detail, { x + 250 * s, 58 * s, 170 * s, 22 * s }, 10.5f, false, juce::Justification::centredRight, silver, 0.55f);
    };

    juce::String namDetail;
    if (proc.externalTone().ampLoaded())
    {
        const auto i = proc.externalTone().namInfo();
        namDetail << juce::String (i.sampleRate / 1000.0, 1) << " kHz";
        if (i.slimmable) namDetail << "  /  A2 " << (proc.apvts.getRawParameterValue ("nam_size")->load() > 0.5f ? "Lite" : "Full");
        if (i.hasLoudness) namDetail << "  /  " << juce::String (i.loudnessDb, 1) << " dB";
    }
    slotInfo (26 * s, "AMP", proc.externalTone().ampLoaded() ? proc.namSource.title : juce::String(), namDetail);
    slotInfo (490 * s, "CABINET", proc.getIrName().isNotEmpty() ? (proc.irSource.title.isNotEmpty() ? proc.irSource.title : proc.getIrName()) : juce::String(), {});

    if (status.isNotEmpty())
        text (status, { 26 * s, 254 * s, 886 * s, 22 * s }, 12, false, juce::Justification::centredLeft, statusIsError ? warn : lilac);
    text ("Tones from tone3000.com, made by their creators. NAM: Neural Amp Modeler.", { 490 * s, 220 * s, 420 * s, 20 * s },
          10, true, juce::Justification::centredLeft, silver, 0.45f);
}
