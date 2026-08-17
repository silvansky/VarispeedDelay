#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Factory presets are embedded via juce_add_binary_data and authored in the standalone.
    User presets live in presetDir() and are listed after the embedded ones. */
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    static juce::File presetDir();

    void rescan();

    int numEmbedded() const { return embedded.size(); }
    int numPresets() const  { return embedded.size() + userFiles.size(); }
    juce::String getName (int index) const;

    bool apply (int index);
    bool applyTree (const juce::ValueTree& tree);
    juce::ValueTree capture() const;

    /** Writes the current state as <NN>-<name>.xml into presetDir(). */
    bool save (const juce::String& name);

    void resetToDefaults();

private:
    struct Entry { juce::String name; juce::ValueTree tree; };

    juce::AudioProcessorValueTreeState& apvts;
    juce::Array<Entry> embedded;
    juce::Array<juce::File> userFiles;

    static juce::String displayName (const juce::String& fileName);
};
