#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DelayEngine.h"
#include "Presets.h"

namespace pid
{
inline constexpr const char* timeMs   = "time_ms";
inline constexpr const char* timeSync = "time_sync";
inline constexpr const char* timeDiv  = "time_div";
inline constexpr const char* timeMode = "time_mode";
inline constexpr const char* speed    = "speed";
inline constexpr const char* feedback = "feedback";
inline constexpr const char* fbType   = "fb_type";
inline constexpr const char* clipOn   = "clip_on";
inline constexpr const char* spacing  = "spacing";
inline constexpr const char* eqOn     = "eq_on";
inline constexpr const char* dry      = "dry";
inline constexpr const char* wet      = "wet";
inline const juce::String eqBand (int i) { return "eq_b" + juce::String (i + 1); }
}

class VarispeedDelayProcessor : public juce::AudioProcessor
{
public:
    VarispeedDelayProcessor();
    ~VarispeedDelayProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return engine.getTailSeconds(); }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    vspd::DelayEngine& getEngine() { return engine; }
    PresetManager& getPresets() { return presets; }

    void setFallbackBpm (double bpm) { fallbackBpm = bpm; }
    double getFallbackBpm() const { return fallbackBpm; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void cacheParameterPointers();
    void pushSettings();

    juce::AudioProcessorValueTreeState apvts;
    vspd::DelayEngine engine;
    PresetManager presets;

    std::atomic<float>* pTimeMs = nullptr;
    std::atomic<float>* pTimeSync = nullptr;
    std::atomic<float>* pTimeDiv = nullptr;
    std::atomic<float>* pTimeMode = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pFbType = nullptr;
    std::atomic<float>* pClip = nullptr;
    std::atomic<float>* pSpacing = nullptr;
    std::atomic<float>* pEqOn = nullptr;
    std::atomic<float>* pEqBand[vspd::kNumEqBands] {};
    std::atomic<float>* pDry = nullptr;
    std::atomic<float>* pWet = nullptr;

    double fallbackBpm = 120.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VarispeedDelayProcessor)
};
