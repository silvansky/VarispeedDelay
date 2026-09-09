#include "PluginEditor.h"
#include "PluginProcessor.h"

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
struct Options
{
    juce::File   out { juce::File::getCurrentWorkingDirectory().getChildFile ("screenshot.png") };
    float        scale = 2.0f;
    int          settleMs = 400;
    int          audioMs = 250;
    juce::String preset;
    juce::StringPairArray params;
    bool         list = false;
};

bool parse (int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        const auto next = [&]() -> juce::String { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };

        if (a == "--out")            o.out = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--scale")     o.scale = next().getFloatValue();
        else if (a == "--settle")    o.settleMs = next().getIntValue();
        else if (a == "--audio")     o.audioMs = next().getIntValue();
        else if (a == "--preset")    o.preset = next();
        else if (a == "--list")      o.list = true;
        else if (a == "--param")     { const auto kv = next(); o.params.set (kv.upToFirstOccurrenceOf ("=", false, false),
                                                                            kv.fromFirstOccurrenceOf ("=", false, false)); }
        else { std::fprintf (stderr, "unknown option %s\n", argv[i]); return false; }
    }
    return true;
}

// The plugin build has no modal loops, so JUCE's own dispatch loop is unavailable here.
void pumpMessages (int ms)
{
   #if JUCE_MAC
    CFRunLoopRunInMode (kCFRunLoopDefaultMode, (double) ms * 0.001, false);
   #else
    juce::Thread::sleep (ms);
   #endif
}

void listState (VarispeedDelayProcessor& proc)
{
    auto& presets = proc.getPresets();
    for (int i = 0; i < presets.numPresets(); ++i)
        std::printf ("preset  %s\n", presets.getName (i).toRawUTF8());

    for (auto* p : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            std::printf ("param   %-10s %7.2f .. %-7.2f now %.2f\n",
                         r->getParameterID().toRawUTF8(),
                         r->getNormalisableRange().start,
                         r->getNormalisableRange().end,
                         r->convertFrom0to1 (r->getValue()));
}

// Readouts fed by the engine (the period line) only settle once blocks have run.
void runSilence (VarispeedDelayProcessor& proc, double sampleRate, int blockSize, int ms)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int done = 0; done < (int) (sampleRate * ms * 0.001); done += blockSize)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }
}

int applyPreset (VarispeedDelayProcessor& proc, const juce::String& wanted)
{
    auto& presets = proc.getPresets();
    for (int i = 0; i < presets.numPresets(); ++i)
        if (presets.getName (i).equalsIgnoreCase (wanted))
            return presets.apply (i) ? i : -1;
    return -1;
}
}

int main (int argc, char** argv)
{
    Options o;
    if (! parse (argc, argv, o)) return 2;

    juce::ScopedJuceInitialiser_GUI gui;

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize = 512;

    VarispeedDelayProcessor proc;
    proc.prepareToPlay (sampleRate, blockSize);

    if (o.list) { listState (proc); return 0; }

    if (o.preset.isNotEmpty() && applyPreset (proc, o.preset) < 0)
    {
        std::fprintf (stderr, "no preset named %s\n", o.preset.toRawUTF8());
        return 3;
    }

    for (const auto& id : o.params.getAllKeys())
    {
        auto* p = proc.getAPVTS().getParameter (id);
        if (p == nullptr) { std::fprintf (stderr, "no parameter %s\n", id.toRawUTF8()); return 3; }
        p->setValueNotifyingHost (p->convertTo0to1 (o.params[id].getFloatValue()));
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (editor == nullptr) return 4;

    runSilence (proc, sampleRate, blockSize, o.audioMs);
    pumpMessages (o.settleMs);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, o.scale);

    o.out.deleteFile();
    juce::FileOutputStream stream (o.out);
    if (! stream.openedOk()) { std::fprintf (stderr, "cannot write %s\n", o.out.getFullPathName().toRawUTF8()); return 5; }

    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, stream)) return 5;

    std::printf ("%s %dx%d\n", o.out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    editor.reset();
    return 0;
}
