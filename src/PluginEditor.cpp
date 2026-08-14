#include "PluginEditor.h"
#include "BuildDate.h"

VarispeedDelayEditor::VarispeedDelayEditor(VarispeedDelayProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setSize(800, 500);
}

void VarispeedDelayEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1e));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.0f));
    g.drawText("Varispeed Delay", getLocalBounds(), juce::Justification::centred);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(VARISPEEDDELAY_BUILD_DATE, getLocalBounds().reduced(8),
               juce::Justification::bottomRight);
}

void VarispeedDelayEditor::resized()
{
}
