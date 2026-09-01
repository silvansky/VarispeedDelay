#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

/** Segmented switch bound to any ranged parameter (choice or bool). */
class ChoiceSwitch : public juce::Component
{
public:
    ChoiceSwitch (juce::RangedAudioParameter& p, juce::StringArray labels, juce::String helpText);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray items;
    int index = 0;
    juce::ParameterAttachment attachment;
};

/** Single latching chip bound to a bool parameter, drawn as text or an icon. */
class ToggleChip : public juce::Component
{
public:
    using Icon = std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)>;

    ToggleChip (juce::RangedAudioParameter& p, juce::String label, juce::String helpText, Icon = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isOn() const { return on; }

private:
    juce::String text;
    Icon icon;
    bool on = false;
    juce::ParameterAttachment attachment;
};

/** Slider bound to a parameter, with shift for fine drags and double-click to default. */
class ParamSlider : public juce::Slider
{
public:
    ParamSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
                 juce::String helpText);
    /** Unattached: the owner drives the value, used where one knob writes two parameters. */
    explicit ParamSlider (juce::String helpText);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    double snapValue (double attemptedValue, DragMode) override;

    /** Quantiser applied to drags, so a stepped knob never needs a value written back. */
    std::function<double (double)> snapFn;

private:
    void init (const juce::String& helpText);

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Editable value readout: number in one size, unit in a smaller dim one. */
class ValueField : public juce::Label
{
public:
    ValueField (juce::RangedAudioParameter&, float numberPt, float unitPt, juce::String helpText);

    void setDisplay (const juce::String& number, const juce::String& unit);
    void setNumberColour (juce::Colour);
    void setMono (bool);
    void setActive (bool);
    void paint (juce::Graphics&) override;
    void editorShown (juce::TextEditor*) override;

    /** Replaces the default "parse with the parameter" behaviour. */
    std::function<void (const juce::String&)> onEdit;

private:
    juce::RangedAudioParameter& param;
    juce::String numberText, unitText;
    juce::Colour numberColour { vspd::col::text };
    float numPt, uPt;
    bool mono = false;
};

/** Everything at the design's 800x500 size; the editor scales this as a whole. */
class EditorContent : public juce::Component,
                      private juce::Timer
{
public:
    explicit EditorContent (VarispeedDelayProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setZoomDisplay (float scale);
    std::function<void (float)> onZoomRequest;

private:
    void timerCallback() override;
    void buildControls();
    void updateFooter();
    void setStatus (const juce::String& help, bool withTechInfo = false);
    void updateReadouts();
    void updateDynamicHelp();
    void updateSpeedPresets();
    void showSaveDialog();
    void refreshPresetCombo();
    void tapTempo();
    void setSpeedFromText (const juce::String&);
    void setTimeFromText (const juce::String&);
    void wireTimeKnob();
    void pushTimeFromKnob();
    void setTimeKnobValue (double ms);
    double divisionMs (int index) const;
    int    nearestDivision (double ms) const;
    void drawDivisionRing (juce::Graphics&) const;

    static void setHelp (juce::Component& c, const juce::String& text);

    VarispeedDelayProcessor& proc;

    std::unique_ptr<ParamSlider> timeKnob, speedKnob, feedbackKnob, clipKnob, drySlider, wetSlider;
    std::unique_ptr<ValueField>  timeField, speedField, feedbackField, clipField, dryField, wetField;
    std::unique_ptr<ToggleChip>  syncChip;
    std::unique_ptr<ChoiceSwitch> timeModeSwitch, spacingSwitch, fbTypeSwitch, clipSwitch,
                                  eqOnSwitch, directionSwitch;

    std::unique_ptr<juce::ParameterAttachment> timeMsAttachment, timeDivAttachment;
    bool draggingTime = false;

    juce::TextButton tapButton { "TAP" };
    juce::ComboBox divBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divAttachment;

    juce::OwnedArray<juce::TextButton> speedPresets;
    juce::OwnedArray<juce::ParameterAttachment> speedPresetAttachments;

    juce::OwnedArray<ParamSlider> eqSliders;
    juce::OwnedArray<ValueField> eqFields;

    juce::Label helpMark, contextHelpLabel;
    juce::String techInfo;
    juce::ComboBox zoomBox;

    juce::ComboBox presetBox;
    juce::TextButton saveButton { "SAVE" };
    bool showPresetRow = false;

    // painted state, refreshed on the timer; paint only runs when one of these moves
    double bpm = 120.0, periodMs = 500.0, loopGain = 0.5, speedShown = 1.0;
    float  safeFeedback = 1.0f, eqPeakDb = 0.0f;
    int    eqPeakBand = 0, divIndex = 8;
    bool   syncOn = false, eqOn = false, clipLit = false;
    std::tuple<double, double, double, double, float, float, int, int, bool, bool, bool> painted {};

    std::unique_ptr<juce::AlertWindow> saveWindow;
    juce::Array<juce::int64> tapTimes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorContent)
};

class VarispeedDelayEditor : public juce::AudioProcessorEditor
{
public:
    explicit VarispeedDelayEditor (VarispeedDelayProcessor&);
    ~VarispeedDelayEditor() override;

    void resized() override;

private:
    VarispeedLookAndFeel lnf;
    EditorContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VarispeedDelayEditor)
};
