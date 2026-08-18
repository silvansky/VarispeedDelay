#include "PluginProcessor.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>

// Replacing global new/delete lets the test prove processBlock never allocates.
std::atomic<int>  gAllocs { 0 };
std::atomic<bool> gTrackAllocs { false };

void* operator new (std::size_t n)
{
    if (gTrackAllocs.load (std::memory_order_relaxed)) gAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n != 0 ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setBpm (128.0);
        info.setPpqPosition (ppq);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

int failures = 0;
int checks = 0;
const char* currentTest = "";

void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL [%s] %s\n", currentTest, what.toRawUTF8());
    }
}

void test (const char* name, const std::function<void()>& body)
{
    currentTest = name;
    const int before = failures;
    body();
    std::printf ("%s %s\n", failures == before ? "ok  " : "FAIL", name);
}

juce::StringArray registeredIds (juce::AudioProcessor& p)
{
    juce::StringArray ids;
    for (auto* param : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            ids.add (rp->paramID);
    ids.sort (false);
    return ids;
}

juce::StringArray treeIds (const juce::ValueTree& tree)
{
    juce::StringArray ids;
    for (int i = 0; i < tree.getNumChildren(); ++i)
        if (tree.getChild (i).hasProperty ("id"))
            ids.add (tree.getChild (i).getProperty ("id").toString());
    ids.sort (false);
    return ids;
}

void setAll (VarispeedDelayProcessor& p, float normalised)
{
    for (auto* param : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            rp->setValueNotifyingHost (normalised);
}

//==============================================================================
void testEmbeddedPresetsAreCurrent()
{
    VarispeedDelayProcessor p;
    auto& pm = p.getPresets();
    const auto ids = registeredIds (p);

    std::printf ("     %d embedded preset(s)\n", pm.numEmbedded());

    for (int i = 0; i < pm.numEmbedded(); ++i)
    {
        const auto name = pm.getName (i);
        check (name.isNotEmpty(), "preset " + juce::String (i) + " has a name");
        check (pm.apply (i), name + " applies");
        check (treeIds (pm.capture()) == ids, name + " parameter IDs match the registered set");
    }
}

void testRoundTrip()
{
    VarispeedDelayProcessor p;

    setAll (p, 0.37f);
    const auto snapshot = p.getPresets().capture().createCopy();

    setAll (p, 0.82f);
    check (treeIds (p.getPresets().capture()) == treeIds (snapshot), "same IDs after a change");

    p.getPresets().applyTree (snapshot);
    const auto restored = p.getPresets().capture();

    bool same = true;
    for (int i = 0; i < snapshot.getNumChildren(); ++i)
    {
        const auto want = snapshot.getChild (i);
        const auto id = want.getProperty ("id").toString();
        bool found = false;
        for (int j = 0; j < restored.getNumChildren(); ++j)
        {
            const auto got = restored.getChild (j);
            if (got.getProperty ("id").toString() != id) continue;
            found = true;
            const double a = want.getProperty ("value");
            const double b = got.getProperty ("value");
            if (std::abs (a - b) > 1.0e-4 * juce::jmax (1.0, std::abs (a))) same = false;
        }
        if (! found) same = false;
    }
    check (same, "apply -> capture -> compare round-trips");
}

void testNoPresetBleed()
{
    VarispeedDelayProcessor p;

    // "Preset A" moves everything away from the default
    setAll (p, 0.9f);
    const auto a = p.getPresets().capture().createCopy();

    // "Preset B" only carries the wet parameter — everything else must fall back to its
    // default, not stay at A's value.
    juce::ValueTree b ("PARAMS");
    juce::ValueTree wet ("PARAM");
    wet.setProperty ("id", pid::wet, nullptr);
    wet.setProperty ("value", 0.25, nullptr);
    b.addChild (wet, -1, nullptr);

    p.getPresets().applyTree (a);
    p.getPresets().applyTree (b);

    check (std::abs (p.getAPVTS().getRawParameterValue (pid::wet)->load() - 0.25f) < 1.0e-5f,
           "B's own value applied");

    for (auto* param : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (rp->paramID != juce::String (pid::wet))
                check (std::abs (rp->getValue() - rp->getDefaultValue()) < 1.0e-5f,
                       rp->paramID + " reset to default, not left at A's value");
}

void testStateRoundTrip()
{
    VarispeedDelayProcessor p;
    setAll (p, 0.61f);

    juce::MemoryBlock block;
    p.getStateInformation (block);

    VarispeedDelayProcessor q;
    q.setStateInformation (block.getData(), (int) block.getSize());

    // compare snapped denormalised values: bools and choices only quantise on a round trip
    auto denorm = [] (juce::RangedAudioParameter* rp)
    {
        return rp->getNormalisableRange().snapToLegalValue (rp->convertFrom0to1 (rp->getValue()));
    };

    for (auto* param : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (auto* other = q.getAPVTS().getParameter (rp->paramID))
                check (std::abs (denorm (rp) - denorm (other))
                         <= 1.0e-5f * juce::jmax (1.0f, std::abs (denorm (rp))),
                       rp->paramID + " restored ("
                         + juce::String (denorm (rp), 6) + " vs "
                         + juce::String (denorm (other), 6) + ")");
}

void testProcessorBasics()
{
    VarispeedDelayProcessor p;
    p.prepareToPlay (48000.0, 512);

    check (p.getLatencySamples() == 0, "reports zero latency");
    check (p.getTailLengthSeconds() > 0.0, "reports a tail");
    check (p.getNumPrograms() >= 1, "at least one program");

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int b = 0; b < 200; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, 0.3f * std::sin ((b * 512 + i) * 0.01f));
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                finite = finite && std::isfinite (buf.getSample (ch, i));
    }
    check (finite, "processBlock output is finite");
}
void testProcessBlockDoesNotAllocate()
{
    VarispeedDelayProcessor p;
    FakePlayHead playHead;
    p.setPlayHead (&playHead);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    buf.clear();
    p.processBlock (buf, midi);         // warm up outside the tracked window

    auto* speed = p.getAPVTS().getParameter (pid::speed);
    auto* time  = p.getAPVTS().getParameter (pid::timeMs);
    auto* sync  = p.getAPVTS().getParameter (pid::timeSync);
    auto* eq    = p.getAPVTS().getParameter (pid::eqOn);

    gAllocs.store (0);
    gTrackAllocs.store (true);

    for (int b = 0; b < 300; ++b)
    {
        speed->setValue ((float) (b % 100) / 100.0f);
        time->setValue ((float) (b % 37) / 37.0f);
        sync->setValue ((b % 2) == 0 ? 1.0f : 0.0f);
        eq->setValue ((b % 3) == 0 ? 1.0f : 0.0f);
        playHead.ppq = b * 512 * 128.0 / 60.0 / 48000.0;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, 0.3f * std::sin ((b * 512 + i) * 0.01f));

        p.processBlock (buf, midi);
        (void) p.getTailLengthSeconds();
    }

    gTrackAllocs.store (false);
    const int n = gAllocs.load();
    check (n == 0, "processBlock allocates nothing (" + juce::String (n) + " allocations)");
}
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("VarispeedDelay preset / parameter tests\n");

    test ("embedded presets parse and match the registered parameter set", testEmbeddedPresetsAreCurrent);
    test ("apply -> capture round trip", testRoundTrip);
    test ("reset-then-overlay leaves nothing of the previous preset", testNoPresetBleed);
    test ("host state save/restore", testStateRoundTrip);
    test ("processor basics", testProcessorBasics);
    test ("processBlock does not allocate", testProcessBlockDoesNotAllocate);

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
