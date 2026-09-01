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
constexpr int kDesignW = 800, kDesignH = 500;
constexpr float kSwitchGap = 3.0f;

const char* const kDefaultHint = "shift-drag fine  .  double-click reset  .  type values";

// 3x3 grid, ascending pitch in reading order: octaves where the user expects them,
// fourths and fifths filling the gaps.
constexpr int kSpeedGridCols = 3;
constexpr float kSpeedPresetSemis[] { -24.0f, -12.0f, -7.0f,
                                       -5.0f,   0.0f,  5.0f,
                                        7.0f,  12.0f, 24.0f };
constexpr const char* kSpeedPresetNames[] { "1/4", "1/2", "5dn",
                                            "4dn", "1",   "4up",
                                            "5up", "2",   "4" };
constexpr const char* kSpeedPresetDesc[] { "two octaves down", "one octave down", "a fifth down",
                                           "a fourth down",    "unison",          "a fourth up",
                                           "a fifth up",       "one octave up",   "two octaves up" };
constexpr int kZoomPercents[] { 75, 100, 125, 150, 200 };

float speedForPreset (int i) { return std::pow (2.0f, kSpeedPresetSemis[i] / 12.0f); }

std::pair<juce::String, juce::String> msParts (double ms)
{
    if (ms >= 1000.0) return { juce::String (ms / 1000.0, 2), "s" };
    return { juce::String (ms, ms < 10.0 ? 2 : (ms < 100.0 ? 1 : 0)), "ms" };
}

juce::String msText (double ms)
{
    const auto p = msParts (ms);
    return p.first + " " + p.second;
}

juce::String bandLabel (float f)
{
    return f >= 1000.0f ? juce::String (f / 1000.0f, f == 16000.0f ? 0 : 1) + "k"
                        : juce::String ((int) f);
}

juce::String signedDb (float db)
{
    return (db > 0.0f ? "+" : "") + juce::String (db, 1);
}

/** Straight divisions and bars get a label on the time ring; triplets and dots a tick. */
bool isMajorDivision (const juce::String& name)
{
    return ! name.containsChar ('T') && ! name.containsChar ('D');
}

void drawCaption (juce::Graphics& g, const juce::String& t, float centreX, float baseline)
{
    g.setFont (uiFont (9.5f));
    g.setColour (juce::Colour (col::mid));
    g.drawSingleLineText (t, juce::roundToInt (centreX), juce::roundToInt (baseline),
                          juce::Justification::horizontallyCentred);
}

void drawMono (juce::Graphics& g, const juce::String& t, float centreX, float baseline,
               float pt, juce::Colour c)
{
    g.setFont (monoFont (pt));
    g.setColour (c);
    g.drawSingleLineText (t, juce::roundToInt (centreX), juce::roundToInt (baseline),
                          juce::Justification::horizontallyCentred);
}

void drawSectionTitle (juce::Graphics& g, const juce::String& t, float centreX, float baseline)
{
    g.setColour (juce::Colour (col::heading));
    drawTrackedCentred (g, uiFont (14.0f), t, centreX, baseline, 2.6f);
}

void drawSwitchSegment (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t,
                        bool on, bool enabled)
{
    g.setColour (juce::Colour (on ? col::accent : col::panel)
                   .withAlpha (enabled ? 1.0f : 0.45f));
    g.fillRoundedRectangle (r, 3.0f);
    if (! on)
    {
        g.setColour (juce::Colour (col::panelEdge).withAlpha (enabled ? 1.0f : 0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
    if (t.isEmpty()) return;
    g.setFont (uiFont (8.5f, on));
    g.setColour (juce::Colour (on ? col::onAccent : col::mid).withAlpha (enabled ? 1.0f : 0.45f));
    g.drawText (t, r.toNearestInt(), juce::Justification::centred, false);
}

/** An eighth note, drawn rather than typed - the string literals here stay ASCII. */
void drawNoteIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
{
    const float h = juce::jmin (area.getHeight(), 13.0f);
    const auto box = area.withSizeKeepingCentre (h * 0.72f, h);
    g.setColour (c);
    g.fillEllipse (box.getX(), box.getBottom() - h * 0.32f, h * 0.44f, h * 0.32f);
    g.fillRect (box.getX() + h * 0.38f, box.getY(), h * 0.09f, h * 0.80f);
    juce::Path flag;
    flag.startNewSubPath (box.getX() + h * 0.46f, box.getY() + h * 0.04f);
    flag.quadraticTo (box.getRight(), box.getY() + h * 0.16f,
                      box.getX() + h * 0.52f, box.getY() + h * 0.44f);
    g.strokePath (flag, juce::PathStrokeType (h * 0.11f));
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
    const int n = juce::jmax (1, items.size());
    const float segW = (b.getWidth() - kSwitchGap * (float) (n - 1)) / (float) n;

    for (int i = 0; i < items.size(); ++i)
        drawSwitchSegment (g, { b.getX() + (float) i * (segW + kSwitchGap), b.getY(),
                                segW, b.getHeight() },
                           items[i], i == index, isEnabled());
}

void ChoiceSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (items.isEmpty() || getWidth() <= 0 || ! isEnabled()) return;
    const int seg = juce::jlimit (0, items.size() - 1, e.x * items.size() / getWidth());
    attachment.setValueAsCompleteGesture ((float) seg);
}

//==============================================================================
ToggleChip::ToggleChip (juce::RangedAudioParameter& p, juce::String label, juce::String helpText,
                        Icon iconToDraw)
    : text (std::move (label)), icon (std::move (iconToDraw)),
      attachment (p, [this] (float v) { on = v > 0.5f; repaint(); })
{
    getProperties().set ("help", helpText);
    attachment.sendInitialUpdate();
}

void ToggleChip::paint (juce::Graphics& g)
{
    drawSwitchSegment (g, getLocalBounds().toFloat(), icon ? juce::String() : text, on, isEnabled());
    if (icon) icon (g, getLocalBounds().toFloat(), juce::Colour (on ? col::onAccent : col::mid));
}

void ToggleChip::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

//==============================================================================
void ParamSlider::init (const juce::String& helpText)
{
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kRotaryStart, kRotaryEnd, true);
    getProperties().set ("help", helpText);
}

ParamSlider::ParamSlider (juce::String helpText)
{
    init (helpText);
}

ParamSlider::ParamSlider (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                          juce::String helpText)
{
    init (helpText);

    if (auto* p = state.getParameter (paramId))
        setDoubleClickReturnValue (true, p->getNormalisableRange()
                                            .convertFrom0to1 (p->getDefaultValue()));

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, paramId, *this);
}

void ParamSlider::mouseDown (const juce::MouseEvent& e)
{
    // rotaries take a longer drag; the linear ones have no sensitivity of their own, so
    // fine mode there means switching to velocity tracking for the duration of the drag
    const bool fine = e.mods.isShiftDown();
    if (getSliderStyle() == juce::Slider::LinearVertical)
    {
        setVelocityBasedMode (fine);
        if (fine) setVelocityModeParameters (0.2, 1, 0.0, false);
    }
    else
    {
        setMouseDragSensitivity (fine ? 1400 : 250);
    }
    juce::Slider::mouseDown (e);
}

void ParamSlider::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    setVelocityBasedMode (false);
}

double ParamSlider::snapValue (double attemptedValue, DragMode)
{
    return snapFn ? snapFn (attemptedValue) : attemptedValue;
}

//==============================================================================
ValueField::ValueField (juce::RangedAudioParameter& p, float numberPt, float unitPt,
                        juce::String helpText)
    : param (p), numPt (numberPt), uPt (unitPt)
{
    setJustificationType (juce::Justification::centred);
    setEditable (true, true, false);
    getProperties().set ("help", helpText);

    onEdit = [this] (const juce::String& t)
    {
        param.beginChangeGesture();
        param.setValueNotifyingHost (param.getValueForText (t));
        param.endChangeGesture();
    };
    onTextChange = [this] { if (onEdit) onEdit (getText()); };
}

void ValueField::setDisplay (const juce::String& number, const juce::String& unit)
{
    if (isBeingEdited() || (numberText == number && unitText == unit)) return;
    numberText = number;
    unitText = unit;
    // the label's own text seeds the editor, so it carries the unit - typing into a field
    // reading "20.00 s" has to mean seconds, not twenty milliseconds
    setText (unit.isEmpty() ? number : number + " " + unit, juce::dontSendNotification);
    repaint();
}

void ValueField::setNumberColour (juce::Colour c)
{
    if (numberColour == c) return;
    numberColour = c;
    repaint();
}

void ValueField::setMono (bool m) { mono = m; repaint(); }

void ValueField::setActive (bool active)
{
    setEnabled (active);
    setEditable (active, active, false);
    repaint();
}

void ValueField::editorShown (juce::TextEditor* ed)
{
    // the default label editor font is 15 px, which overflows the shorter fields
    auto f = mono ? monoFont (numPt) : uiFont (numPt);
    const float maxH = (float) getHeight() - 2.0f;
    if (f.getHeight() > maxH) f = f.withHeight (maxH);

    ed->setBorder ({ 1, 1, 1, 1 });
    ed->setIndents (2, juce::roundToInt (((float) getHeight() - 2.0f - f.getHeight()) * 0.5f));
    ed->setJustification (juce::Justification::centred);
    ed->applyFontToAllText (f);
}

void ValueField::paint (juce::Graphics& g)
{
    if (isBeingEdited()) return;   // the text editor carries the design's boxed treatment

    const auto nf = mono ? monoFont (numPt) : uiFont (numPt);
    const auto uf = mono ? monoFont (uPt)   : uiFont (uPt);
    const auto number = numberText;
    const auto unit = unitText.isEmpty() ? juce::String() : " " + unitText;
    const float nw = juce::GlyphArrangement::getStringWidth (nf, number);
    const float uw = unit.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (uf, unit);
    const float x = ((float) getWidth() - nw - uw) * 0.5f;
    const float baseline = ((float) getHeight() - nf.getHeight()) * 0.5f + nf.getAscent();
    const float alpha = isEnabled() ? 1.0f : 0.6f;

    g.setFont (nf);
    g.setColour (numberColour.withMultipliedAlpha (alpha));
    g.drawSingleLineText (number, juce::roundToInt (x), juce::roundToInt (baseline));

    if (uw > 0.0f)
    {
        g.setFont (uf);
        g.setColour (juce::Colour (col::dim).withMultipliedAlpha (alpha));
        g.drawSingleLineText (unit, juce::roundToInt (x + nw), juce::roundToInt (baseline));
    }
}

//==============================================================================
void EditorContent::setHelp (juce::Component& c, const juce::String& text)
{
    c.getProperties().set ("help", text);
}

EditorContent::EditorContent (VarispeedDelayProcessor& p)
    : proc (p)
{
    buildControls();

    // Debug builds get the authoring row in every format, so presets can be dialled in
    // from a DAW as well as the standalone. Release plugin builds leave preset management
    // to the host unless VSPD_PRESET_AUTHORING is set.
#if JUCE_DEBUG || defined (VSPD_PRESET_AUTHORING)
    showPresetRow = true;
#elif JucePlugin_Build_Standalone
    showPresetRow = juce::JUCEApplicationBase::isStandaloneApp();
#endif
    presetBox.setVisible (showPresetRow);
    saveButton.setVisible (showPresetRow);

    setSize (kDesignW, kDesignH);
    addMouseListener (this, true);
    startTimerHz (30);
}

void EditorContent::buildControls()
{
    auto& s = proc.getAPVTS();

    timeKnob = std::make_unique<ParamSlider> (
        juce::String ("Delay time - the period between repetitions"));
    timeKnob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    wireTimeKnob();
    speedKnob = std::make_unique<ParamSlider> (s, pid::speed,
        "Tape speed of every repetition - pitch and length change together");
    speedKnob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    feedbackKnob = std::make_unique<ParamSlider> (s, pid::feedback,
        "Recycle gain. Above 1.0 the loop runs away - the soft clip keeps it musical");
    feedbackKnob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    clipKnob = std::make_unique<ParamSlider> (s, pid::clipThr,
        "Soft clip threshold - the recycle path is transparent below it, tanh above it "
        "up to the 0 dBFS ceiling. The arc turns red while the clip is working");
    clipKnob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    drySlider = std::make_unique<ParamSlider> (s, pid::dry, "Dry signal level");
    drySlider->setSliderStyle (juce::Slider::LinearVertical);
    wetSlider = std::make_unique<ParamSlider> (s, pid::wet,
        "Wet level. Overlapping repeats sum without compensation, so this is the trim");
    wetSlider->setSliderStyle (juce::Slider::LinearVertical);

    for (auto* k : { timeKnob.get(), speedKnob.get(), feedbackKnob.get(), clipKnob.get(),
                     drySlider.get(), wetSlider.get() })
        addAndMakeVisible (*k);

    auto field = [&s] (const char* id, float numPt, float unitPt, const juce::String& help)
    {
        return std::make_unique<ValueField> (*s.getParameter (id), numPt, unitPt, help);
    };

    timeField     = field (pid::timeMs,   15.0f, 8.5f,
        "Delay time - click to type a value, in ms or with an s suffix for seconds");
    speedField    = field (pid::speed,    15.0f, 8.5f,
        "Tape speed - type a ratio like 1.5, or semitones like +7s");
    feedbackField = field (pid::feedback, 15.0f, 8.5f, "Recycle gain - click to type a value");
    clipField     = field (pid::clipThr,  14.0f, 8.5f, "Soft clip threshold in dBFS");
    dryField      = field (pid::dry,      14.0f, 8.5f, "Dry signal level");
    wetField      = field (pid::wet,      14.0f, 8.5f, "Wet signal level");

    timeField->onEdit  = [this] (const juce::String& t) { setTimeFromText (t); };
    speedField->onEdit = [this] (const juce::String& t) { setSpeedFromText (t); };
    // the syntax hint is only useful while the field is open, so it comes and goes with it
    speedField->onEditorShow = [this] { repaint(); };
    speedField->onEditorHide = [this] { repaint(); };

    for (auto* f : { timeField.get(), speedField.get(), feedbackField.get(), clipField.get(),
                     dryField.get(), wetField.get() })
        addAndMakeVisible (*f);

    auto* pSync    = s.getParameter (pid::timeSync);
    auto* pMode    = s.getParameter (pid::timeMode);
    auto* pSpacing = s.getParameter (pid::spacing);
    auto* pDir     = s.getParameter (pid::direction);
    auto* pFb      = s.getParameter (pid::fbType);
    auto* pClip    = s.getParameter (pid::clipOn);
    auto* pEq      = s.getParameter (pid::eqOn);

    syncChip = std::make_unique<ToggleChip> (*pSync, "SYNC",
        "Lock the period to the host grid instead of the time knob", &drawNoteIcon);
    timeModeSwitch = std::make_unique<ChoiceSwitch> (*pMode, juce::StringArray { "REGRID", "BEND" },
        "REGRID snaps to the new time and leaves the tail alone. BEND bends everything sounding");
    spacingSwitch = std::make_unique<ChoiceSwitch> (*pSpacing, juce::StringArray { "GRID", "TAPE" },
        "GRID keeps repetitions on the delay grid. TAPE starts each one when the last ends");
    directionSwitch = std::make_unique<ChoiceSwitch> (
        *pDir, juce::StringArray { "FWD", "REV", "ALT" },
        "FWD plays each repetition forward. REV plays every one backwards. ALT flips every other one");
    fbTypeSwitch = std::make_unique<ChoiceSwitch> (*pFb, juce::StringArray { "RAW", "STABLE" },
        "RAW: pitch compounds each repetition. STABLE: every repetition plays at the same speed");
    clipSwitch = std::make_unique<ChoiceSwitch> (*pClip, juce::StringArray { "NO CLIP", "CLIP" },
        "Soft clip in the recycle path - transparent below the threshold, bounds runaway above it");
    eqOnSwitch = std::make_unique<ChoiceSwitch> (*pEq, juce::StringArray { "OFF", "ON" },
        "7-band EQ on the repetition path - repetition N carries the curve N times");

    addAndMakeVisible (*syncChip);
    for (auto* c : { timeModeSwitch.get(), spacingSwitch.get(), fbTypeSwitch.get(),
                     clipSwitch.get(), eqOnSwitch.get(), directionSwitch.get() })
        addAndMakeVisible (*c);

    setHelp (tapButton, "Tap a tempo - sets the delay time when free, the sync tempo when the "
                        "host has none");
    tapButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible (tapButton);

    for (int i = 0; i < numDivisions(); ++i)
        divBox.addItem (division (i).name, i + 1);
    setHelp (divBox, "Note division used in SYNC mode, anchored to the host's ppq grid");
    addAndMakeVisible (divBox);
    divAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        s, pid::timeDiv, divBox);

    auto* speedParam = s.getParameter (pid::speed);
    for (int i = 0; i < (int) std::size (kSpeedPresetSemis); ++i)
    {
        auto* b = new juce::TextButton (kSpeedPresetNames[i]);
        b->setClickingTogglesState (false);

        const float target = speedForPreset (i);
        // only unity and the octaves up read at integer positions
        const bool lossless = kSpeedPresetSemis[i] >= 0.0f
                              && std::abs (std::fmod (kSpeedPresetSemis[i], 12.0f)) < 0.01f;
        setHelp (*b, "Speed " + juce::String (target, 3) + "x - " + kSpeedPresetDesc[i]
                     + (lossless ? ". Integer read positions, so no generational loss"
                                 : ". Interpolated, so the tail darkens a little each repetition"));
        addAndMakeVisible (b);
        speedPresets.add (b);

        auto* att = new juce::ParameterAttachment (*speedParam, [] (float) {});
        speedPresetAttachments.add (att);
        b->onClick = [att, target] { att->setValueAsCompleteGesture (target); };
    }

    for (int i = 0; i < kNumEqBands; ++i)
    {
        const auto label = bandLabel (GraphicEQ::bandFreq[i]);
        const auto help = label + " Hz band, +/-12 dB - applied once per repetition, so it accumulates";

        auto* sl = new ParamSlider (s, pid::eqBand (i), help);
        sl->setSliderStyle (juce::Slider::LinearVertical);
        addAndMakeVisible (sl);
        eqSliders.add (sl);

        auto* f = new ValueField (*s.getParameter (pid::eqBand (i)), 8.5f, 8.5f, help);
        f->setMono (true);
        addAndMakeVisible (f);
        eqFields.add (f);
    }

    helpMark.setText ("?", juce::dontSendNotification);
    helpMark.setFont (monoFont (9.0f));
    helpMark.setJustificationType (juce::Justification::centred);
    helpMark.setColour (juce::Label::textColourId, juce::Colour (col::dim));
    setHelp (helpMark, "Hover any control for a one-line description here in the footer");
    addAndMakeVisible (helpMark);

    contextHelpLabel.setBorderSize ({});
    contextHelpLabel.setFont (monoFont (8.0f));
    contextHelpLabel.setColour (juce::Label::textColourId, juce::Colour (col::dim).withAlpha (0.8f));
    contextHelpLabel.setJustificationType (juce::Justification::centredLeft);
    contextHelpLabel.setMinimumHorizontalScale (0.8f);
    contextHelpLabel.setInterceptsMouseClicks (false, false);
    contextHelpLabel.setText (kDefaultHint, juce::dontSendNotification);
    addAndMakeVisible (contextHelpLabel);

    for (int i = 0; i < (int) std::size (kZoomPercents); ++i)
        zoomBox.addItem (juce::String (kZoomPercents[i]) + "%", i + 1);
    setHelp (zoomBox, "Editor size - drag the corner for anything in between");
    zoomBox.onChange = [this]
    {
        const int idx = zoomBox.getSelectedId() - 1;
        if (idx >= 0 && onZoomRequest) onZoomRequest ((float) kZoomPercents[idx] / 100.0f);
    };
    addAndMakeVisible (zoomBox);

    setHelp (presetBox, "Factory and user presets - user presets come from VSPD_PRESET_DIR");
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

void EditorContent::refreshPresetCombo()
{
    presetBox.clear (juce::dontSendNotification);
    auto& pm = proc.getPresets();
    for (int i = 0; i < pm.numPresets(); ++i)
        presetBox.addItem (pm.getName (i), i + 1);
    if (pm.numPresets() == 0) presetBox.setTextWhenNoChoicesAvailable ("no presets");
}

void EditorContent::showSaveDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> (
        "Save preset",
        "Saving to " + PresetManager::presetDir().getFullPathName() + "\n\nPreset name:",
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

/** The knob rides the millisecond scale in both modes; in sync it lands on the ring's
    ticks and writes the division, so the pointer and the labels never disagree. */
void EditorContent::wireTimeKnob()
{
    auto& s = proc.getAPVTS();
    auto* pMs = s.getParameter (pid::timeMs);
    auto* pDiv = s.getParameter (pid::timeDiv);
    if (pMs == nullptr || pDiv == nullptr) return;

    const auto& r = pMs->getNormalisableRange();
    timeKnob->setNormalisableRange ({ (double) r.start, (double) r.end,
                                     (double) r.interval, (double) r.skew, r.symmetricSkew });
    timeKnob->setDoubleClickReturnValue (true, (double) r.convertFrom0to1 (pMs->getDefaultValue()));

    // never write back into a knob the user is holding - that is what turns a float
    // round trip into a drag that fights itself
    timeMsAttachment = std::make_unique<juce::ParameterAttachment> (*pMs,
        [this] (float v) { if (! syncOn && ! draggingTime) setTimeKnobValue ((double) v); });
    timeDivAttachment = std::make_unique<juce::ParameterAttachment> (*pDiv,
        [this] (float v)
        {
            divIndex = (int) std::round (v);
            if (syncOn && ! draggingTime) setTimeKnobValue (divisionMs (divIndex));
        });
    timeKnob->snapFn = [this] (double v) { return syncOn ? divisionMs (nearestDivision (v)) : v; };
    timeMsAttachment->sendInitialUpdate();
    timeDivAttachment->sendInitialUpdate();

    timeKnob->onDragStart = [this]
    {
        draggingTime = true;
        (syncOn ? *timeDivAttachment : *timeMsAttachment).beginGesture();
    };
    timeKnob->onDragEnd = [this]
    {
        (syncOn ? *timeDivAttachment : *timeMsAttachment).endGesture();
        draggingTime = false;
    };
    timeKnob->onValueChange = [this] { pushTimeFromKnob(); };
}

void EditorContent::pushTimeFromKnob()
{
    const double v = timeKnob->getValue();
    if (syncOn)
    {
        const int d = nearestDivision (v);
        auto& att = *timeDivAttachment;
        if (d != divIndex)
        {
            divIndex = d;
            if (draggingTime) att.setValueAsPartOfGesture ((float) d);
            else              att.setValueAsCompleteGesture ((float) d);
        }
    }
    else
    {
        auto& att = *timeMsAttachment;
        if (draggingTime) att.setValueAsPartOfGesture ((float) v);
        else              att.setValueAsCompleteGesture ((float) v);
    }
}

void EditorContent::setTimeKnobValue (double ms)
{
    timeKnob->setValue (ms, juce::dontSendNotification);
}

double EditorContent::divisionMs (int index) const
{
    return 60000.0 / juce::jmax (1.0, bpm) * division (index).quarters;
}

int EditorContent::nearestDivision (double ms) const
{
    int best = 0;
    double bestDistance = 1.0e30;
    for (int i = 0; i < numDivisions(); ++i)
    {
        // ratios, not differences: 1/32 and 1/16 are as far apart as 1 bar and 2 bars
        const double d = std::abs (std::log (juce::jmax (1.0e-6, divisionMs (i)))
                                   - std::log (juce::jmax (1.0e-6, ms)));
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

void EditorContent::tapTempo()
{
    const auto now = juce::Time::currentTimeMillis();
    if (! tapTimes.isEmpty() && now - tapTimes.getLast() > 2500) tapTimes.clearQuick();
    tapTimes.add (now);
    while (tapTimes.size() > 5) tapTimes.remove (0);
    if (tapTimes.size() < 2) return;

    const double interval = (double) (tapTimes.getLast() - tapTimes.getFirst())
                            / (double) (tapTimes.size() - 1);
    if (interval < 50.0 || interval > 4000.0) return;

    proc.setFallbackBpm (juce::jlimit (20.0, 300.0, 60000.0 / interval));

    // free running, the tap is the delay time itself; in sync it only feeds the tempo
    if (! syncOn)
        if (auto* p = proc.getAPVTS().getParameter (pid::timeMs))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 ((float) interval));
            p->endChangeGesture();
        }
}

void EditorContent::setTimeFromText (const juce::String& t)
{
    auto* p = proc.getAPVTS().getParameter (pid::timeMs);
    if (p == nullptr) return;

    const auto text = t.trim().toLowerCase();
    double ms = text.getDoubleValue();
    if (text.endsWithChar ('s') && ! text.endsWith ("ms")) ms *= 1000.0;

    const auto& range = p->getNormalisableRange();
    p->beginChangeGesture();
    p->setValueNotifyingHost (range.convertTo0to1 (juce::jlimit (range.start, range.end, (float) ms)));
    p->endChangeGesture();
}

void EditorContent::setSpeedFromText (const juce::String& t)
{
    auto* p = proc.getAPVTS().getParameter (pid::speed);
    if (p == nullptr) return;

    const auto trimmed = t.trim().toLowerCase();
    const double number = trimmed.getDoubleValue();
    const bool semitones = trimmed.endsWithChar ('s') || trimmed.contains ("semi");
    const double value = semitones ? std::pow (2.0, number / 12.0) : number;

    const auto& range = p->getNormalisableRange();
    p->beginChangeGesture();
    p->setValueNotifyingHost (range.convertTo0to1 (juce::jlimit (range.start, range.end,
                                                                 (float) value)));
    p->endChangeGesture();
}

//==============================================================================
void EditorContent::resized()
{
    presetBox.setBounds (600, 10, 130, 20);
    saveButton.setBounds (738, 10, 42, 20);

    // ---- DELAY ------------------------------------------------------------
    syncChip->setBounds (30, 98, 30, 22);
    tapButton.setBounds (64, 98, 38, 22);
    timeField->setBounds (126, 96, 80, 20);
    divBox.setBounds (216, 98, 58, 20);
    timeKnob->setBounds (112, 142, 80, 80);
    spacingSwitch->setBounds (24, 242, 83, 16);
    timeModeSwitch->setBounds (156, 242, 105, 16);

    // ---- SPEED ------------------------------------------------------------
    speedField->setBounds (356, 96, 130, 26);
    speedKnob->setBounds (301, 143, 94, 94);
    for (int i = 0; i < speedPresets.size(); ++i)
        speedPresets[i]->setBounds (400 + (i % kSpeedGridCols) * 52,
                                    156 + (i / kSpeedGridCols) * 24, 48, 20);
    directionSwitch->setBounds (400, 242, 152, 16);   // on the speed grid's columns

    // ---- FEEDBACK ---------------------------------------------------------
    feedbackField->setBounds (638, 96, 80, 20);
    feedbackKnob->setBounds (631, 129, 94, 94);
    fbTypeSwitch->setBounds (620, 244, 116, 16);   // centred on the knob at 678

    // ---- GRAPHIC EQ -------------------------------------------------------
    eqOnSwitch->setBounds (300, 276, 79, 16);
    for (int i = 0; i < kNumEqBands; ++i)
    {
        const int cx = 52 + 48 * i;
        eqFields[i]->setBounds (cx - 24, 297, 48, 18);   // taller than the glyphs so the editor fits
        eqSliders[i]->setBounds (cx - 10, 318, 20, 84);
    }

    // ---- OUTPUT -----------------------------------------------------------
    clipField->setBounds (422, 320, 80, 20);
    clipKnob->setBounds (420, 338, 84, 84);
    clipSwitch->setBounds (412, 424, 100, 16);   // centred on the knob at 462
    dryField->setBounds (610, 320, 60, 20);
    drySlider->setBounds (630, 348, 20, 76);
    wetField->setBounds (686, 320, 60, 20);
    wetSlider->setBounds (706, 348, 20, 76);

    // ---- footer -----------------------------------------------------------
    zoomBox.setBounds (690, 476, 58, 18);
    helpMark.setBounds (752, 476, 16, 18);
    contextHelpLabel.setBounds (20, 476, 660, 18);
}

// the status line carries one message at a time: the standing hint, the hovered control's
// help, or - over the "?" - that help plus the technical readout
void EditorContent::setStatus (const juce::String& help, bool withTechInfo)
{
    auto text = help.isNotEmpty() ? help : juce::String (kDefaultHint);
    if (withTechInfo) text << "  |  " << techInfo;
    if (contextHelpLabel.getText() != text)
        contextHelpLabel.setText (text, juce::dontSendNotification);
}

//==============================================================================
void EditorContent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (col::background));

    g.setColour (juce::Colour (col::mid));
    drawTracked (g, uiFont (10.0f, true), "VARISPEED DELAY", 20.0f, 24.0f, 3.4f);

    g.setColour (juce::Colour (col::divider).withAlpha (0.6f));
    g.drawHorizontalLine (38, 0.0f, (float) kDesignW);
    g.drawHorizontalLine (262, 0.0f, (float) kDesignW);
    g.drawHorizontalLine (468, 0.0f, (float) kDesignW);
    g.drawVerticalLine (286, 44.0f, 258.0f);
    g.drawVerticalLine (556, 44.0f, 258.0f);
    g.drawVerticalLine (392, 268.0f, 462.0f);

    drawSectionTitle (g, "DELAY", 149.0f, 64.0f);
    drawSectionTitle (g, "SPEED", 421.0f, 64.0f);
    drawSectionTitle (g, "FEEDBACK", 678.0f, 64.0f);
    drawSectionTitle (g, "OUTPUT", 596.0f, 288.0f);
    g.setColour (juce::Colour (col::heading));
    drawTracked (g, uiFont (10.0f), "GRAPHIC EQ", 24.0f, 288.0f, 2.4f);

    // ---- DELAY ------------------------------------------------------------
    drawCaption (g, "Tempo Sync", 66.0f, 90.0f);
    drawCaption (g, "Delay Time", 166.0f, 90.0f);
    drawCaption (g, "Note", 245.0f, 90.0f);
    drawMono (g, juce::String (bpm, 1) + " BPM", 83.0f, 134.0f, 7.0f, juce::Colour (col::dim));
    drawDivisionRing (g);
    drawMono (g, "period " + msText (periodMs), 152.0f, 230.0f, 7.0f, juce::Colour (col::dim));

    // ---- SPEED ------------------------------------------------------------
    drawCaption (g, "Speed", 421.0f, 88.0f);
    if (speedField->isBeingEdited())
        drawMono (g, "type  1.5  |  +7s  |  -1.5s", 421.0f, 134.0f, 7.5f, juce::Colour (col::dim));
    drawMono (g, juce::String (12.0 * std::log2 (juce::jmax (1.0e-6, speedShown)), 2) + " semitones",
              348.0f, 234.0f, 7.0f, juce::Colour (col::dim));

    // ---- FEEDBACK ---------------------------------------------------------
    drawCaption (g, "Feedback", 678.0f, 92.0f);
    const bool runaway = loopGain >= 1.0;
    g.setColour (juce::Colour (runaway ? col::warn : col::dim));
    drawTrackedCentred (g, monoFont (9.5f).boldened(),
                        "LOOP GAIN " + juce::String (loopGain, 2) + "x", 678.0f, 216.0f, 0.6f);
    if (eqOn && eqPeakDb > 0.05f)
        drawMono (g, "EQ " + signedDb (eqPeakDb) + " dB at " + bandLabel (GraphicEQ::bandFreq[eqPeakBand])
                     + " - safe below " + juce::String (safeFeedback, 2),
                  678.0f, 230.0f, 7.0f, juce::Colour (col::dim));

    // ---- GRAPHIC EQ -------------------------------------------------------
    g.setColour (juce::Colour (col::dim).withAlpha (0.22f));
    g.drawHorizontalLine (360, 40.0f, 372.0f);
    for (int i = 0; i < kNumEqBands; ++i)
        drawMono (g, bandLabel (GraphicEQ::bandFreq[i]), (float) (52 + 48 * i), 418.0f, 7.5f,
                  juce::Colour (col::dim));
    g.setFont (monoFont (7.5f));
    g.setColour (juce::Colour (col::dim).withAlpha (0.7f));
    g.drawSingleLineText ("dB", 24, 440);
    g.drawSingleLineText ("Hz", 360, 440, juce::Justification::right);

    // ---- OUTPUT -----------------------------------------------------------
    drawCaption (g, "Clip Threshold", 462.0f, 314.0f);
    drawCaption (g, "Dry", 640.0f, 314.0f);
    drawCaption (g, "Wet", 716.0f, 314.0f);
}

void EditorContent::drawDivisionRing (juce::Graphics& g) const
{
    auto* p = proc.getAPVTS().getParameter (pid::timeMs);
    if (p == nullptr) return;

    const auto bounds = timeKnob->getBounds();
    const auto centre = bounds.toFloat().getCentre();
    const float r = knobArcRadius (bounds);
    const auto& range = p->getNormalisableRange();
    const float sweep = kRotaryEnd - kRotaryStart;

    for (int i = 0; i < numDivisions(); ++i)
    {
        const juce::String name (division (i).name);
        const double ms = 60000.0 / juce::jmax (1.0, bpm) * division (i).quarters;
        if (ms < range.start || ms > range.end) continue;

        const float angle = kRotaryStart + range.convertTo0to1 ((float) ms) * sweep;
        const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
        const bool major = isMajorDivision (name);
        const bool current = syncOn && i == divIndex;

        g.setColour (current ? juce::Colour (col::accent)
                             : juce::Colour (col::dim).withAlpha (major ? 0.5f : 0.3f));
        g.drawLine ({ centre + dir * (r + 4.0f), centre + dir * (r + (major ? 9.0f : 7.0f)) },
                    major || current ? 1.6f : 1.0f);

        if (! major) continue;
        const auto at = centre + dir * (r + 22.0f);
        drawMono (g, name, at.x, at.y + 2.4f, 6.5f,
                  current ? juce::Colour (col::accent) : juce::Colour (col::dim).withAlpha (0.7f));
    }
}

//==============================================================================
void EditorContent::mouseEnter (const juce::MouseEvent& e)
{
    // walk up from the hovered component so a child inherits its panel's help; fall back to
    // the standing hint when nothing in the chain carries one
    juce::String help;
    auto* source = e.eventComponent;
    for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
    {
        help = c->getProperties()["help"].toString();
        source = c;
        if (help.isNotEmpty() || c == this) break;
    }
    setStatus (help, source == &helpMark);
}

void EditorContent::mouseExit (const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
        setStatus ({});
}

void EditorContent::setZoomDisplay (float scale)
{
    const int pct = juce::roundToInt (scale * 100.0f);
    for (int i = 0; i < zoomBox.getNumItems(); ++i)
        if (kZoomPercents[i] == pct)
        {
            zoomBox.setSelectedId (zoomBox.getItemId (i), juce::dontSendNotification);
            return;
        }
    zoomBox.setTextWhenNothingSelected (juce::String (pct) + "%");
    zoomBox.setSelectedId (0, juce::dontSendNotification);
}

//==============================================================================
void EditorContent::timerCallback()
{
    updateFooter();
    updateReadouts();
    updateDynamicHelp();
    updateSpeedPresets();
}

void EditorContent::updateReadouts()
{
    auto& s = proc.getAPVTS();
    auto& engine = proc.getEngine();

    const bool wasSync = syncOn;
    syncOn   = s.getRawParameterValue (pid::timeSync)->load() > 0.5f;
    divIndex = (int) s.getRawParameterValue (pid::timeDiv)->load();
    eqOn     = s.getRawParameterValue (pid::eqOn)->load() > 0.5f;
    clipLit  = engine.isClipping();
    bpm      = engine.getBpm();
    periodMs = engine.getPeriodMs();

    const bool clipOn = s.getRawParameterValue (pid::clipOn)->load() > 0.5f;

    divBox.setEnabled (syncOn);
    timeField->setActive (! syncOn);
    // the sync knob reads the tempo, so a tempo change has to move it; leaving sync hands
    // the knob back to the millisecond parameter
    if (syncOn && ! draggingTime)   setTimeKnobValue (divisionMs (divIndex));
    else if (wasSync && ! syncOn)   setTimeKnobValue (s.getRawParameterValue (pid::timeMs)->load());
    clipKnob->setEnabled (clipOn);
    clipKnob->getProperties().set ("alert", clipLit);
    clipField->setActive (clipOn);

    // the target, so the readout tracks the knob instantly - what the engine is actually
    // running, glide and buffer floor included, is the period line under the knob
    const double shownMs = syncOn ? divisionMs (divIndex)
                                  : (double) s.getRawParameterValue (pid::timeMs)->load();
    const auto parts = msParts (shownMs);
    timeField->setDisplay (parts.first, parts.second);
    timeField->setNumberColour (juce::Colour (syncOn ? col::dim : col::text));

    speedShown = s.getRawParameterValue (pid::speed)->load();
    speedField->setDisplay (juce::String (speedShown, 3), "x");

    eqPeakDb = 0.0f;
    eqPeakBand = 0;
    for (int i = 0; i < kNumEqBands; ++i)
    {
        const float db = s.getRawParameterValue (pid::eqBand (i))->load();
        if (db > eqPeakDb) { eqPeakDb = db; eqPeakBand = i; }
        eqFields[i]->setDisplay (signedDb (db), {});
        eqFields[i]->setNumberColour (juce::Colour (std::abs (db) > 0.05f ? col::text : col::dim));
    }

    const float feedback = s.getRawParameterValue (pid::feedback)->load();
    const float eqGain = eqOn ? juce::Decibels::decibelsToGain (eqPeakDb) : 1.0f;
    loopGain = feedback * eqGain;
    safeFeedback = 1.0f / juce::jmax (1.0e-3f, eqGain);

    const auto& fbRange = proc.getAPVTS().getParameter (pid::feedback)->getNormalisableRange();
    feedbackKnob->getProperties().set ("split",
        fbRange.convertTo0to1 (juce::jlimit (fbRange.start, fbRange.end, safeFeedback)));
    feedbackField->setDisplay (juce::String (feedback, 2), {});
    feedbackField->setNumberColour (juce::Colour (loopGain >= 1.0 ? col::warn : col::text));

    clipField->setDisplay (juce::String (s.getRawParameterValue (pid::clipThr)->load(), 1), "dB");
    dryField->setDisplay (juce::String (s.getRawParameterValue (pid::dry)->load(), 2), {});
    wetField->setDisplay (juce::String (s.getRawParameterValue (pid::wet)->load(), 2), {});

    const auto now = std::make_tuple (bpm, periodMs, loopGain, speedShown, safeFeedback, eqPeakDb,
                                      eqPeakBand, divIndex, syncOn, eqOn, clipLit);
    if (now != painted) { painted = now; repaint(); }
}

void EditorContent::updateSpeedPresets()
{
    const float speed = proc.getAPVTS().getRawParameterValue (pid::speed)->load();
    for (int i = 0; i < speedPresets.size(); ++i)
    {
        const float cents = 1200.0f * std::log2 (juce::jmax (1.0e-6f, speed) / speedForPreset (i));
        const bool on = std::abs (cents) < 1.0f;
        if (speedPresets[i]->getToggleState() != on)
            speedPresets[i]->setToggleState (on, juce::dontSendNotification);
    }
}

void EditorContent::updateDynamicHelp()
{
    auto& e = proc.getEngine();
    const int voices = e.getActiveVoices();
    const double repMs = e.getRepetitionMs();

    // TAPE runs the grid away from the knob until it bottoms out here, so test the period
    // the engine actually uses rather than what was asked for
    const double minMs = e.getMinPeriodMs();
    const bool floored = periodMs < minMs + 0.01;

    setHelp (*timeKnob, juce::String (syncOn ? "Note division - " : "Delay time - ")
                        + msText (periodMs) + " period, repetition lasts "
                        + msText (repMs) + ", " + juce::String (voices) + " sounding"
                        + (floored ? ". Floored at the " + msText (minMs) + " audio buffer"
                                   : juce::String()));
    setHelp (*speedKnob, "Tape speed - repetition lasts " + msText (repMs)
                         + " on a " + msText (periodMs) + " grid; below 1x repeats overlap");
    setHelp (*feedbackKnob, "Recycle gain - loop gain " + juce::String (loopGain, 2) + "x"
                            + (loopGain >= 1.0 ? ", the tail grows without end"
                                               : ", the tail decays"));
}

void EditorContent::updateFooter()
{
    const double sr = proc.getSampleRate();
    const int blockSize = proc.getBlockSize();

    juce::String text;
    text << juce::String (sr, 0) << " Hz | " << blockSize << " smp";
    if (sr > 0.0) text << " | " << juce::String (blockSize * 1000.0 / sr, 1) << " ms";

#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        if (auto* device = holder->deviceManager.getCurrentAudioDevice())
            text << " | " << device->getName();
#endif

    text << " | v" << JucePlugin_VersionString << " (" << VARISPEEDDELAY_BUILD_DATE << ")";

    if (techInfo != text)
    {
        techInfo = text;
        if (helpMark.isMouseOver (true))
            setStatus (helpMark.getProperties()["help"].toString(), true);
    }
}

//==============================================================================
VarispeedDelayEditor::VarispeedDelayEditor (VarispeedDelayProcessor& p)
    : AudioProcessorEditor (&p), content (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.onZoomRequest = [this] (float z)
    {
        setSize (juce::roundToInt ((float) kDesignW * z), juce::roundToInt ((float) kDesignH * z));
    };

    setResizable (true, true);
    setResizeLimits (kDesignW / 2, kDesignH / 2, kDesignW * 2, kDesignH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);

    setSize (kDesignW, kDesignH);
}

VarispeedDelayEditor::~VarispeedDelayEditor()
{
    setLookAndFeel (nullptr);
}

void VarispeedDelayEditor::resized()
{
    const float scale = (float) getWidth() / (float) kDesignW;
    content.setBounds (0, 0, kDesignW, kDesignH);
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setZoomDisplay (scale);
}
