#pragma once
#include "PluginProcessor.h"

class VarispeedDelayEditor : public juce::AudioProcessorEditor
{
public:
    explicit VarispeedDelayEditor(VarispeedDelayProcessor&);
    ~VarispeedDelayEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    VarispeedDelayProcessor& proc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VarispeedDelayEditor)
};
