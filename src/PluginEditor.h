#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "RepetitionView.h"

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

/** Rotary knob with a caption and a value readout that can be overridden. */
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
                 const juce::String& caption, juce::String helpText);

    void resized() override;
    void refreshValue();
    void setValueOverride (const juce::String& text);   // empty clears

    juce::Slider slider;

private:
    juce::RangedAudioParameter* param = nullptr;
    juce::Label captionLabel, valueLabel;
    juce::String override;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class VarispeedDelayEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit VarispeedDelayEditor (VarispeedDelayProcessor&);
    ~VarispeedDelayEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void buildControls();
    void updateFooter();
    void updateDynamicHelp();
    void updateSpeedPresets();
    void showSaveDialog();
    void refreshPresetCombo();

    static void setHelp (juce::Component& c, const juce::String& text);

    VarispeedDelayProcessor& proc;
    VarispeedLookAndFeel lnf;

    std::unique_ptr<LabeledKnob> timeKnob, speedKnob, feedbackKnob, dryKnob, wetKnob;
    std::unique_ptr<ChoiceSwitch> syncSwitch, timeModeSwitch, spacingSwitch, fbTypeSwitch,
                                  clipSwitch, eqOnSwitch;
    juce::ComboBox divBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divAttachment;

    juce::OwnedArray<juce::TextButton> speedPresets;
    juce::OwnedArray<juce::ParameterAttachment> speedPresetAttachments;

    juce::OwnedArray<juce::Slider> eqSliders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> eqAttachments;
    juce::OwnedArray<juce::Label> eqLabels;

    RepetitionView repView;

    juce::TextButton helpButton { "?" };
    juce::Label contextHelpLabel, footerLabel;

    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save..." };
    bool showPresetRow = false;

    juce::Rectangle<int> headerArea, controlArea, eqArea, bottomArea, presetArea, footerArea;
    std::unique_ptr<juce::AlertWindow> saveWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VarispeedDelayEditor)
};
