#include "PluginEditor.h"
#include "BuildDate.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_devices/juce_audio_devices.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using namespace vspd;

namespace
{
constexpr int kFooterH = 20;
constexpr int kHeaderH = 32;
constexpr float kSpeedPresets[] { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
constexpr const char* kSpeedPresetNames[] { "1/4", "1/2", "1", "2", "4" };

void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (juce::Colour (col::panel));
    g.fillRoundedRectangle (r.toFloat(), 4.0f);
    g.setColour (juce::Colour (col::panelEdge));
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);

    if (title.isNotEmpty())
    {
        g.setColour (juce::Colour (col::accent));
        g.fillRect (r.getX() + 10, r.getY() + 9, 3, 10);
        g.setColour (juce::Colour (col::dim));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (title, r.getX() + 18, r.getY() + 7, r.getWidth() - 24, 14,
                    juce::Justification::centredLeft, false);
    }
}
} // namespace

//==============================================================================
ChoiceSwitch::ChoiceSwitch (juce::RangedAudioParameter& p, juce::StringArray labels, juce::String helpText)
    : items (std::move (labels)),
      attachment (p, [this] (float v) { index = (int) std::round (v); repaint(); })
{
    getProperties().set ("help", helpText);
    attachment.sendInitialUpdate();
}

void ChoiceSwitch::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (col::panelEdge));
    g.fillRoundedRectangle (b, 3.0f);

    const float segW = b.getWidth() / (float) juce::jmax (1, items.size());
    for (int i = 0; i < items.size(); ++i)
    {
        const auto seg = juce::Rectangle<float> (b.getX() + i * segW, b.getY(), segW, b.getHeight());
        if (i == index)
        {
            g.setColour (juce::Colour (col::accentDim));
            g.fillRoundedRectangle (seg.reduced (1.0f), 2.0f);
        }
        g.setColour (juce::Colour (i == index ? col::text : col::dim));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (items[i], seg.toNearestInt(), juce::Justification::centred, false);
    }
}

void ChoiceSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (items.isEmpty() || getWidth() <= 0) return;
    const int seg = juce::jlimit (0, items.size() - 1, e.x * items.size() / getWidth());
    attachment.setValueAsCompleteGesture ((float) seg);
}

//==============================================================================
LabeledKnob::LabeledKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                          const juce::String& caption, juce::String helpText)
{
    param = state.getParameter (paramId);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    slider.onValueChange = [this] { refreshValue(); };
    addAndMakeVisible (slider);

    captionLabel.setText (caption, juce::dontSendNotification);
    captionLabel.setJustificationType (juce::Justification::centred);
    captionLabel.setFont (juce::FontOptions (10.0f));
    captionLabel.setColour (juce::Label::textColourId, juce::Colour (col::dim));
    captionLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (captionLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (juce::FontOptions (12.0f));
    valueLabel.setColour (juce::Label::textColourId, juce::Colour (col::text));
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    getProperties().set ("help", helpText);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, paramId, slider);
    refreshValue();
}

void LabeledKnob::resized()
{
    auto b = getLocalBounds();
    captionLabel.setBounds (b.removeFromTop (12));
    valueLabel.setBounds (b.removeFromBottom (15));
    slider.setBounds (b);
}

void LabeledKnob::setValueOverride (const juce::String& text)
{
    if (override == text) return;
    override = text;
    refreshValue();
}

void LabeledKnob::refreshValue()
{
    if (override.isNotEmpty()) { valueLabel.setText (override, juce::dontSendNotification); return; }
    if (param != nullptr)
        valueLabel.setText (param->getCurrentValueAsText(), juce::dontSendNotification);
}

//==============================================================================
void VarispeedDelayEditor::setHelp (juce::Component& c, const juce::String& text)
{
    c.getProperties().set ("help", text);
}

VarispeedDelayEditor::VarispeedDelayEditor (VarispeedDelayProcessor& p)
    : AudioProcessorEditor (&p), proc (p), repView (p.getEngine())
{
    setLookAndFeel (&lnf);
    buildControls();

#if JucePlugin_Build_Standalone
    showPresetRow = juce::JUCEApplicationBase::isStandaloneApp();
#endif

    setSize (800, 500);
    addMouseListener (this, true);
    startTimerHz (30);
}

VarispeedDelayEditor::~VarispeedDelayEditor()
{
    setLookAndFeel (nullptr);
}

void VarispeedDelayEditor::buildControls()
{
    auto& s = proc.getAPVTS();

    timeKnob = std::make_unique<LabeledKnob> (s, pid::timeMs, "TIME",
        "Delay time — the period between repetitions");
    speedKnob = std::make_unique<LabeledKnob> (s, pid::speed, "SPEED",
        "Tape speed of every repetition — pitch and length change together");
    feedbackKnob = std::make_unique<LabeledKnob> (s, pid::feedback, "FEEDBACK",
        "Recycle gain. Above 1.0 the loop runs away — the soft clip keeps it musical");
    dryKnob = std::make_unique<LabeledKnob> (s, pid::dry, "DRY", "Dry signal level");
    wetKnob = std::make_unique<LabeledKnob> (s, pid::wet, "WET",
        "Wet level — the trim for overlapping repetitions, which sum without compensation");

    for (auto* k : { timeKnob.get(), speedKnob.get(), feedbackKnob.get(), dryKnob.get(), wetKnob.get() })
        addAndMakeVisible (*k);

    auto* pSync    = s.getParameter (pid::timeSync);
    auto* pMode    = s.getParameter (pid::timeMode);
    auto* pSpacing = s.getParameter (pid::spacing);
    auto* pFb      = s.getParameter (pid::fbType);
    auto* pClip    = s.getParameter (pid::clipOn);
    auto* pEq      = s.getParameter (pid::eqOn);

    syncSwitch = std::make_unique<ChoiceSwitch> (*pSync, juce::StringArray { "FREE", "SYNC" },
        "FREE reads the time knob; SYNC locks the period to the host grid");
    timeModeSwitch = std::make_unique<ChoiceSwitch> (*pMode, juce::StringArray { "REGRID", "BEND" },
        "REGRID snaps to the new time and leaves the tail alone; "
        "BEND slews the time and doppler-bends everything sounding");
    spacingSwitch = std::make_unique<ChoiceSwitch> (*pSpacing, juce::StringArray { "GRID", "TAPE" },
        "GRID keeps repetitions on the delay grid; "
        "TAPE starts the next one when the previous ends, so the grid runs away by the speed");
    fbTypeSwitch = std::make_unique<ChoiceSwitch> (*pFb, juce::StringArray { "RAW", "STABLE" },
        "RAW recycles the varispeed signal, so pitch compounds each repetition; "
        "STABLE recycles a unity copy, so every repetition plays at the same speed");
    clipSwitch = std::make_unique<ChoiceSwitch> (*pClip, juce::StringArray { "NO CLIP", "CLIP" },
        "Soft clip in the recycle path — transparent below -6 dBFS, bounds runaway above it");
    eqOnSwitch = std::make_unique<ChoiceSwitch> (*pEq, juce::StringArray { "OFF", "ON" },
        "7-band EQ on the repetition path — repetition N carries the curve N times");

    for (auto* c : { syncSwitch.get(), timeModeSwitch.get(), spacingSwitch.get(),
                     fbTypeSwitch.get(), clipSwitch.get(), eqOnSwitch.get() })
        addAndMakeVisible (*c);

    for (int i = 0; i < numDivisions(); ++i)
        divBox.addItem (division (i).name, i + 1);
    setHelp (divBox, "Note division used in SYNC mode, anchored to the host's ppq grid");
    addAndMakeVisible (divBox);
    divAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        s, pid::timeDiv, divBox);

    auto* speedParam = s.getParameter (pid::speed);
    for (int i = 0; i < (int) std::size (kSpeedPresets); ++i)
    {
        auto* b = new juce::TextButton (kSpeedPresetNames[i]);
        b->setClickingTogglesState (false);
        setHelp (*b, juce::String ("Set speed to ") + kSpeedPresetNames[i]
                     + "x — integer rates read at integer positions, so they stay lossless");
        addAndMakeVisible (b);
        speedPresets.add (b);

        auto* att = new juce::ParameterAttachment (*speedParam, [] (float) {});
        speedPresetAttachments.add (att);
        const float target = kSpeedPresets[i];
        b->onClick = [att, target] { att->setValueAsCompleteGesture (target); };
    }

    for (int i = 0; i < kNumEqBands; ++i)
    {
        auto* sl = new juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
        const auto f = GraphicEQ::bandFreq[i];
        const auto label = f >= 1000.0f ? juce::String (f / 1000.0f, f == 16000.0f ? 0 : 1) + "k"
                                        : juce::String ((int) f);
        setHelp (*sl, label + " Hz band, +/-12 dB — applied once per repetition, so it accumulates");
        addAndMakeVisible (sl);
        eqSliders.add (sl);
        eqAttachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (
            s, pid::eqBand (i), *sl));

        auto* lb = new juce::Label ({}, label);
        lb->setJustificationType (juce::Justification::centred);
        lb->setFont (juce::FontOptions (9.0f));
        lb->setColour (juce::Label::textColourId, juce::Colour (col::dim));
        lb->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (lb);
        eqLabels.add (lb);
    }

    addAndMakeVisible (repView);

    helpButton.setConnectedEdges (0);
    setHelp (helpButton, "Hover over controls for help");
    addAndMakeVisible (helpButton);

    contextHelpLabel.setFont (juce::FontOptions (10.0f));
    contextHelpLabel.setColour (juce::Label::textColourId, juce::Colour (col::dim));
    contextHelpLabel.setMinimumHorizontalScale (1.0f);
    contextHelpLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (contextHelpLabel);

    footerLabel.setFont (juce::FontOptions (10.0f));
    footerLabel.setColour (juce::Label::textColourId, juce::Colour (col::dim));
    footerLabel.setJustificationType (juce::Justification::centredRight);
    footerLabel.setMinimumHorizontalScale (1.0f);
    footerLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (footerLabel);

    setHelp (presetBox, "Factory and user presets — user presets come from VSPD_PRESET_DIR");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) proc.getPresets().apply (idx);
    };
    addChildComponent (presetBox);

    setHelp (saveButton, "Save the current settings as a preset file in the preset directory");
    saveButton.onClick = [this] { showSaveDialog(); };
    addChildComponent (saveButton);

    refreshPresetCombo();
}

void VarispeedDelayEditor::refreshPresetCombo()
{
    presetBox.clear (juce::dontSendNotification);
    auto& pm = proc.getPresets();
    for (int i = 0; i < pm.numPresets(); ++i)
        presetBox.addItem (pm.getName (i), i + 1);
    if (pm.numPresets() == 0) presetBox.setTextWhenNoChoicesAvailable ("no presets");
}

void VarispeedDelayEditor::showSaveDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Preset name:",
                                                      juce::MessageBoxIconType::NoIcon, this);
    saveWindow->addTextEditor ("name", "New Preset");
    saveWindow->addButton ("Save", 1);
    saveWindow->addButton ("Cancel", 0);
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr)
        {
            const auto name = saveWindow->getTextEditorContents ("name");
            if (proc.getPresets().save (name)) refreshPresetCombo();
        }
        saveWindow.reset();
    }), false);
}

//==============================================================================
void VarispeedDelayEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (col::background));

    g.setColour (juce::Colour (col::text));
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("VARISPEED DELAY", headerArea.withTrimmedLeft (12),
                juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (col::accent));
    g.fillRect (headerArea.getX() + 12, headerArea.getBottom() - 3, getWidth() - 24, 1);

    drawPanel (g, controlArea, {});
    drawPanel (g, eqArea, "GRAPHIC EQ");
    drawPanel (g, bottomArea, {});

    const int thirds = controlArea.getWidth() / 3;
    g.setColour (juce::Colour (col::panelEdge));
    g.drawVerticalLine (controlArea.getX() + thirds, (float) controlArea.getY() + 8.0f,
                        (float) controlArea.getBottom() - 8.0f);
    g.drawVerticalLine (controlArea.getX() + 2 * thirds, (float) controlArea.getY() + 8.0f,
                        (float) controlArea.getBottom() - 8.0f);
}

void VarispeedDelayEditor::resized()
{
    auto area = getLocalBounds();

    footerArea = area.removeFromBottom (kFooterH);
    headerArea = area.removeFromTop (kHeaderH);
    area.reduce (10, 6);

    if (showPresetRow)
    {
        presetArea = area.removeFromBottom (24);
        area.removeFromBottom (6);
        presetBox.setVisible (true);
        saveButton.setVisible (true);

        auto pr = presetArea;
        presetBox.setBounds (pr.removeFromLeft (240).reduced (0, 1));
        pr.removeFromLeft (6);
        saveButton.setBounds (pr.removeFromLeft (70).reduced (0, 1));
    }

    controlArea = area.removeFromTop (160);
    area.removeFromTop (6);
    eqArea = area.removeFromTop (118);
    area.removeFromTop (6);
    bottomArea = area;

    // ---- controls: TIME | SPEED | FEEDBACK -------------------------------
    auto ca = controlArea.reduced (10, 10);
    const int colW = ca.getWidth() / 3;

    auto timeCol = ca.removeFromLeft (colW).reduced (6, 0);
    timeKnob->setBounds (timeCol.removeFromLeft (86));
    timeCol.removeFromLeft (8);
    {
        auto rows = timeCol.withTrimmedTop (14);
        syncSwitch->setBounds (rows.removeFromTop (22));
        rows.removeFromTop (5);
        divBox.setBounds (rows.removeFromTop (22));
        rows.removeFromTop (5);
        timeModeSwitch->setBounds (rows.removeFromTop (22));
        rows.removeFromTop (5);
        spacingSwitch->setBounds (rows.removeFromTop (22));
    }

    auto speedCol = ca.removeFromLeft (colW).reduced (6, 0);
    speedKnob->setBounds (speedCol.removeFromLeft (86));
    speedCol.removeFromLeft (8);
    {
        auto rows = speedCol.withTrimmedTop (46);
        auto row = rows.removeFromTop (24);
        const int bw = row.getWidth() / speedPresets.size();
        for (auto* b : speedPresets)
            b->setBounds (row.removeFromLeft (bw).reduced (2, 0));
    }

    auto fbCol = ca.reduced (6, 0);
    feedbackKnob->setBounds (fbCol.removeFromLeft (86));
    fbCol.removeFromLeft (8);
    {
        auto rows = fbCol.withTrimmedTop (36);
        fbTypeSwitch->setBounds (rows.removeFromTop (24));
        rows.removeFromTop (6);
        clipSwitch->setBounds (rows.removeFromTop (24));
    }

    // ---- EQ ---------------------------------------------------------------
    auto ea = eqArea.reduced (10, 0).withTrimmedTop (24).withTrimmedBottom (8);
    eqOnSwitch->setBounds (ea.removeFromLeft (62).withSizeKeepingCentre (62, 24));
    ea.removeFromLeft (14);

    const int bandW = ea.getWidth() / kNumEqBands;
    for (int i = 0; i < kNumEqBands; ++i)
    {
        auto band = ea.removeFromLeft (bandW);
        eqLabels[i]->setBounds (band.removeFromBottom (12));
        eqSliders[i]->setBounds (band.reduced (bandW / 4, 0));
    }

    // ---- dry / wet + repetition view --------------------------------------
    auto ba = bottomArea.reduced (10, 6);
    dryKnob->setBounds (ba.removeFromLeft (78));
    ba.removeFromLeft (4);
    wetKnob->setBounds (ba.removeFromLeft (78));
    ba.removeFromLeft (12);
    repView.setBounds (ba);

    // ---- footer -----------------------------------------------------------
    auto fa = footerArea.reduced (10, 2);
    helpButton.setBounds (fa.removeFromLeft (20));
    fa.removeFromLeft (4);
    contextHelpLabel.setBounds (fa.removeFromLeft (juce::jmin (330, fa.getWidth() / 2)));
    footerLabel.setBounds (fa);
}

//==============================================================================
void VarispeedDelayEditor::mouseEnter (const juce::MouseEvent& e)
{
    for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
    {
        const auto help = c->getProperties()["help"].toString();
        if (help.isNotEmpty()) { contextHelpLabel.setText (help, juce::dontSendNotification); return; }
        if (c == this) break;
    }
}

void VarispeedDelayEditor::mouseExit (const juce::MouseEvent& e)
{
    if (e.eventComponent == this) contextHelpLabel.setText ({}, juce::dontSendNotification);
}

//==============================================================================
void VarispeedDelayEditor::timerCallback()
{
    repView.refresh();
    updateFooter();
    updateDynamicHelp();
    updateSpeedPresets();

    const bool sync = proc.getAPVTS().getRawParameterValue (pid::timeSync)->load() > 0.5f;
    const bool bend = proc.getAPVTS().getRawParameterValue (pid::timeMode)->load() > 0.5f;
    divBox.setEnabled (sync);

    if (sync)
    {
        const int d = (int) proc.getAPVTS().getRawParameterValue (pid::timeDiv)->load();
        timeKnob->setValueOverride (juce::String (division (d).name) + "  "
                                    + juce::String (proc.getEngine().getPeriodMs(), 0) + " ms");
    }
    else if (bend)
    {
        const double ms = proc.getEngine().getPeriodMs();
        timeKnob->setValueOverride (ms < 1000.0 ? juce::String (ms, 1) + " ms"
                                                : juce::String (ms / 1000.0, 2) + " s");
    }
    else
    {
        timeKnob->setValueOverride ({});
    }
    timeKnob->slider.setEnabled (! sync);
}

void VarispeedDelayEditor::updateSpeedPresets()
{
    const float speed = proc.getAPVTS().getRawParameterValue (pid::speed)->load();
    for (int i = 0; i < speedPresets.size(); ++i)
    {
        const float cents = 1200.0f * std::log2 (juce::jmax (1.0e-6f, speed) / kSpeedPresets[i]);
        const bool on = std::abs (cents) < 1.0f;
        if (speedPresets[i]->getToggleState() != on)
            speedPresets[i]->setToggleState (on, juce::dontSendNotification);
    }
}

void VarispeedDelayEditor::updateDynamicHelp()
{
    auto& e = proc.getEngine();
    const int voices = e.getActiveVoices();
    const double periodMs = e.getPeriodMs();
    const double repMs = e.getRepetitionMs();

    auto fmt = [] (double ms) {
        return ms < 1000.0 ? juce::String (ms, 0) + " ms" : juce::String (ms / 1000.0, 2) + " s";
    };

    setHelp (*timeKnob, "Delay time — " + fmt (periodMs) + " period, repetition lasts "
                        + fmt (repMs) + ", " + juce::String (voices) + " sounding");
    setHelp (*speedKnob, "Tape speed — repetition lasts " + fmt (repMs)
                         + " on a " + fmt (periodMs) + " grid; below 1x repeats overlap");
}

void VarispeedDelayEditor::updateFooter()
{
    const double sr = proc.getSampleRate();
    const int blockSize = proc.getBlockSize();

    juce::String text;
    text << juce::String (sr, 0) << " Hz | " << blockSize << " smp";
    if (sr > 0.0) text << " | ~" << juce::String (blockSize * 1000.0 / sr, 1) << " ms";

#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        if (auto* device = holder->deviceManager.getCurrentAudioDevice())
            text << " | In: " << device->getName() << " | Out: " << device->getName();
#endif

    text << " | v" << JucePlugin_VersionString << " (" << VARISPEEDDELAY_BUILD_DATE << ")";

    if (footerLabel.getText() != text) footerLabel.setText (text, juce::dontSendNotification);
}
