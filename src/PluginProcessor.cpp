#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace vspd;

namespace
{
juce::String msString (float ms)
{
    if (ms < 1000.0f) return juce::String (ms, ms < 100.0f ? 1 : 0) + " ms";
    return juce::String (ms / 1000.0f, 2) + " s";
}

juce::StringArray divisionNames()
{
    juce::StringArray names;
    for (int i = 0; i < numDivisions(); ++i) names.add (division (i).name);
    return names;
}
}

VarispeedDelayProcessor::VarispeedDelayProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      presets (apvts)
{
    cacheParameterPointers();
    setLatencySamples (0);
}

juce::AudioProcessorValueTreeState::ParameterLayout VarispeedDelayProcessor::createLayout()
{
    using Float  = juce::AudioParameterFloat;
    using Bool   = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    using Attr   = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Float> (
        juce::ParameterID { pid::timeMs, 1 }, "Time",
        juce::NormalisableRange<float> ((float) kMinDelayMs, (float) kMaxDelayMs, 0.0f, 0.3f), 500.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return msString (v); })));

    layout.add (std::make_unique<Bool> (juce::ParameterID { pid::timeSync, 1 }, "Sync", false));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { pid::timeDiv, 1 }, "Division", divisionNames(), 8));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { pid::timeMode, 1 }, "Time Mode",
        juce::StringArray { "Regrid", "Bend" }, 0));

    juce::NormalisableRange<float> speedRange (
        (float) kMinSpeed, (float) kMaxSpeed,
        [] (float, float, float p) { return std::pow (2.0f, p * 4.0f - 2.0f); },
        [] (float, float, float v) { return (std::log2 (juce::jmax (1.0e-6f, v)) + 2.0f) / 4.0f; },
        [] (float s, float e, float v) { return juce::jlimit (s, e, v); });

    layout.add (std::make_unique<Float> (
        juce::ParameterID { pid::speed, 1 }, "Speed", speedRange, 1.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 3) + "x"; })));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { pid::feedback, 1 }, "Feedback",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.0f, 1.0f), 0.5f,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { pid::fbType, 1 }, "Feedback Type",
        juce::StringArray { "Raw", "Stable" }, 0));

    layout.add (std::make_unique<Bool> (juce::ParameterID { pid::clipOn, 1 }, "Soft Clip", true));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { pid::spacing, 1 }, "Spacing",
        juce::StringArray { "Grid", "Tape" }, 0));

    layout.add (std::make_unique<Bool> (juce::ParameterID { pid::eqOn, 1 }, "EQ", false));

    for (int i = 0; i < kNumEqBands; ++i)
    {
        const auto f = GraphicEQ::bandFreq[i];
        const auto label = f >= 1000.0f ? juce::String (f / 1000.0f, f == 16000.0f ? 0 : 1) + "k"
                                        : juce::String ((int) f);
        layout.add (std::make_unique<Float> (
            juce::ParameterID { pid::eqBand (i), 1 }, "EQ " + label,
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f,
            Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    }

    layout.add (std::make_unique<Float> (
        juce::ParameterID { pid::dry, 1 }, "Dry",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 1.0f), 1.0f));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { pid::wet, 1 }, "Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 1.0f), 0.5f));

    return layout;
}

void VarispeedDelayProcessor::cacheParameterPointers()
{
    pTimeMs   = apvts.getRawParameterValue (pid::timeMs);
    pTimeSync = apvts.getRawParameterValue (pid::timeSync);
    pTimeDiv  = apvts.getRawParameterValue (pid::timeDiv);
    pTimeMode = apvts.getRawParameterValue (pid::timeMode);
    pSpeed    = apvts.getRawParameterValue (pid::speed);
    pFeedback = apvts.getRawParameterValue (pid::feedback);
    pFbType   = apvts.getRawParameterValue (pid::fbType);
    pClip     = apvts.getRawParameterValue (pid::clipOn);
    pSpacing  = apvts.getRawParameterValue (pid::spacing);
    pEqOn     = apvts.getRawParameterValue (pid::eqOn);
    pDry      = apvts.getRawParameterValue (pid::dry);
    pWet      = apvts.getRawParameterValue (pid::wet);
    for (int i = 0; i < kNumEqBands; ++i)
        pEqBand[i] = apvts.getRawParameterValue (pid::eqBand (i));
}

void VarispeedDelayProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pushSettings();
    engine.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (0);
}

bool VarispeedDelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void VarispeedDelayProcessor::pushSettings()
{
    DelayEngine::Settings s;
    s.timeMs   = (double) pTimeMs->load();
    s.sync     = pTimeSync->load() > 0.5f;
    s.divIndex = (int) pTimeDiv->load();
    s.timeMode = pTimeMode->load() > 0.5f ? TimeMode::Bend : TimeMode::Regrid;
    s.speed    = (double) pSpeed->load();
    s.feedback = pFeedback->load();
    s.fbType   = pFbType->load() > 0.5f ? FbType::Stable : FbType::Raw;
    s.clip     = pClip->load() > 0.5f;
    s.spacing  = pSpacing->load() > 0.5f ? Spacing::Tape : Spacing::Grid;
    s.eqOn     = pEqOn->load() > 0.5f;
    s.dry      = pDry->load();
    s.wet      = pWet->load();
    for (int i = 0; i < kNumEqBands; ++i) s.eqDb[i] = pEqBand[i]->load();
    engine.setSettings (s);
}

void VarispeedDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    pushSettings();

    DelayEngine::Transport t;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            t.valid   = true;
            t.playing = pos->getIsPlaying();
            t.bpm     = pos->getBpm().orFallback (fallbackBpm);
            t.ppq     = pos->getPpqPosition().orFallback (0.0);
            if (auto sig = pos->getTimeSignature())
            {
                t.tsNum = sig->numerator;
                t.tsDen = sig->denominator;
            }
            if (! pos->getPpqPosition().hasValue()) t.playing = false;
        }
    }
    engine.setFallbackBpm (fallbackBpm);
    engine.setTransport (t);
    engine.process (buffer);
}

juce::AudioProcessorEditor* VarispeedDelayProcessor::createEditor()
{
    return new VarispeedDelayEditor (*this);
}

//==============================================================================
int VarispeedDelayProcessor::getNumPrograms()
{
    return juce::jmax (1, presets.numEmbedded());
}

void VarispeedDelayProcessor::setCurrentProgram (int index)
{
    if (presets.numEmbedded() == 0) { currentProgram = 0; return; }
    if (index < 0 || index >= presets.numEmbedded()) return;
    currentProgram = index;
    presets.apply (index);
}

const juce::String VarispeedDelayProcessor::getProgramName (int index)
{
    if (presets.numEmbedded() == 0) return "Default";
    return presets.getName (index);
}

void VarispeedDelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VarispeedDelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VarispeedDelayProcessor();
}
