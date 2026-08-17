#include "Presets.h"

#if VSPD_HAS_PRESETS
 #include <BinaryData.h>
#endif

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    rescan();
}

juce::File PresetManager::presetDir()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("VSPD_PRESET_DIR", {}); env.isNotEmpty())
        return juce::File (env);

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
             .getChildFile ("VarispeedDelay")
             .getChildFile ("Presets");
}

juce::String PresetManager::displayName (const juce::String& fileName)
{
    auto n = fileName.upToLastOccurrenceOf (".xml", false, true);
    if (n.length() > 3 && n[0] >= '0' && n[0] <= '9' && n[1] >= '0' && n[1] <= '9' && n[2] == '-')
        n = n.substring (3);
    return n.replaceCharacter ('_', ' ');
}

void PresetManager::rescan()
{
    embedded.clearQuick();
    userFiles.clearQuick();

#if VSPD_HAS_PRESETS
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const auto* originalName = BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]);
        if (originalName == nullptr) continue;

        const juce::String file (originalName);
        if (! file.endsWithIgnoreCase (".xml")) continue;

        int size = 0;
        if (const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            if (auto xml = juce::parseXML (juce::String::createStringFromData (data, size)))
                embedded.add ({ displayName (file), juce::ValueTree::fromXml (*xml) });
    }

    std::sort (embedded.begin(), embedded.end(),
               [] (const Entry& a, const Entry& b) { return a.name < b.name; });
#endif

    auto dir = presetDir();
    if (dir.isDirectory())
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
            userFiles.add (f);

    std::sort (userFiles.begin(), userFiles.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
}

juce::String PresetManager::getName (int index) const
{
    if (index < 0) return {};
    if (index < embedded.size()) return embedded.getReference (index).name;

    const int u = index - embedded.size();
    if (u < userFiles.size()) return displayName (userFiles.getReference (u).getFileName());
    return {};
}

void PresetManager::resetToDefaults()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
}

bool PresetManager::applyTree (const juce::ValueTree& tree)
{
    if (! tree.isValid()) return false;

    // Reset first: replaceState would leave parameters absent from the tree at their
    // current value, which is the classic preset-bleed bug.
    resetToDefaults();

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        if (! child.hasProperty ("id") || ! child.hasProperty ("value")) continue;

        if (auto* p = apvts.getParameter (child.getProperty ("id").toString()))
        {
            const float v = (float) (double) child.getProperty ("value");
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        }
    }
    return true;
}

bool PresetManager::apply (int index)
{
    if (index < 0) return false;
    if (index < embedded.size()) return applyTree (embedded.getReference (index).tree);

    const int u = index - embedded.size();
    if (u >= userFiles.size()) return false;

    if (auto xml = juce::parseXML (userFiles.getReference (u)))
        return applyTree (juce::ValueTree::fromXml (*xml));
    return false;
}

juce::ValueTree PresetManager::capture() const
{
    return apvts.copyState();
}

bool PresetManager::save (const juce::String& name)
{
    auto dir = presetDir();
    if (! dir.isDirectory() && ! dir.createDirectory()) return false;

    const auto clean = juce::File::createLegalFileName (name.trim()).replaceCharacter (' ', '_');
    if (clean.isEmpty()) return false;

    int next = 1;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
        next = juce::jmax (next, f.getFileName().substring (0, 2).getIntValue() + 1);

    const auto file = dir.getChildFile (juce::String (next).paddedLeft ('0', 2) + "-" + clean + ".xml");

    if (auto xml = capture().createXml())
    {
        if (! file.replaceWithText (xml->toString())) return false;
        rescan();
        return true;
    }
    return false;
}
