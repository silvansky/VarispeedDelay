#pragma once

#include "DelayEngine.h"
#include "LookAndFeel.h"

/** Stacked bars, one per live voice: length = repetition duration, offset = how far
    into it we are. The one place the overlap becomes legible. */
class RepetitionView : public juce::Component
{
public:
    explicit RepetitionView (vspd::DelayEngine& e);

    void paint (juce::Graphics&) override;
    void refresh();

private:
    vspd::DelayEngine& engine;
    vspd::DelayEngine::VoiceInfo info[vspd::kMaxVoices];
    int count = 0;
    float periodMs = 500.0f;
};
