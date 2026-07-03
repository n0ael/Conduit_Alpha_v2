#include "ModuleUiDefaults.h"

#include "Modules/ChassisSchema.h"

namespace conduit
{

namespace
{
    // XML-Format pro factoryId-Eintrag:
    //   <Defaults><Param id="density" userMin=".." userMax=".."
    //                    uiHidden=".." curve=".."/>…</Defaults>
    constexpr const char* defaultsTag = "Defaults";
    constexpr const char* paramTag    = "Param";
    constexpr const char* keyPrefix   = "defaults_";   // Property-Key = defaults_{factoryId}

    [[nodiscard]] juce::String keyFor (const juce::String& factoryId)
    {
        return keyPrefix + factoryId;
    }
}

//==============================================================================
juce::PropertiesFile::Options ModuleUiDefaults::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "ModuleUiDefaults";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

ModuleUiDefaults::ModuleUiDefaults (const juce::PropertiesFile::Options& options)
{
    applicationProperties.setStorageParameters (options);
}

ModuleUiDefaults::~ModuleUiDefaults()
{
    if (auto* file = applicationProperties.getUserSettings())
        file->saveIfNeeded();
}

//==============================================================================
void ModuleUiDefaults::captureFromNode (const juce::ValueTree& nodeTree)
{
    auto* file = applicationProperties.getUserSettings();

    if (file == nullptr)
        return;

    const auto factoryId = nodeTree.getProperty (id::factoryId).toString();

    if (factoryId.isEmpty())
        return;

    juce::XmlElement defaults (defaultsTag);
    const auto params = nodeTree.getChildWithName (id::parameters);

    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        const auto param = params.getChild (i);

        if (ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
            continue;

        const auto hasOverride = param.hasProperty (id::paramUserMin)
                              || param.hasProperty (id::paramUserMax)
                              || (bool) param.getProperty (id::paramUiHidden, false)
                              || param.hasProperty (id::paramCurve);

        if (! hasOverride)
            continue;

        auto* entry = defaults.createNewChildElement (paramTag);
        entry->setAttribute ("id", param.getProperty (id::paramId).toString());

        if (param.hasProperty (id::paramUserMin))
            entry->setAttribute ("userMin", (double) param.getProperty (id::paramUserMin));

        if (param.hasProperty (id::paramUserMax))
            entry->setAttribute ("userMax", (double) param.getProperty (id::paramUserMax));

        if ((bool) param.getProperty (id::paramUiHidden, false))
            entry->setAttribute ("uiHidden", true);

        if (param.hasProperty (id::paramCurve))
            entry->setAttribute ("curve", param.getProperty (id::paramCurve).toString());
    }

    // Keine Overrides mehr → Eintrag löschen (Reset auf Werks-Defaults)
    if (defaults.getNumChildElements() == 0)
        file->removeValue (keyFor (factoryId));
    else
        file->setValue (keyFor (factoryId), defaults.toString());

    file->saveIfNeeded();
    sendChangeMessage();
}

void ModuleUiDefaults::applyTo (juce::ValueTree& nodeTree)
{
    auto* file = applicationProperties.getUserSettings();

    if (file == nullptr)
        return;

    const auto stored = file->getValue (keyFor (nodeTree.getProperty (id::factoryId).toString()));

    if (stored.isEmpty())
        return;

    const auto defaults = juce::XmlDocument::parse (stored);

    if (defaults == nullptr || ! defaults->hasTagName (defaultsTag))
        return;

    auto params = nodeTree.getChildWithName (id::parameters);

    for (const auto* entry : defaults->getChildIterator())
    {
        if (! entry->hasTagName (paramTag))
            continue;

        auto param = params.getChildWithProperty (id::paramId,
                                                  entry->getStringAttribute ("id"));

        // Nur bekannte dsp-Parameter — veraltete Einträge werden ignoriert
        if (! param.isValid()
            || ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
            continue;

        if (entry->hasAttribute ("userMin"))
            param.setProperty (id::paramUserMin, entry->getDoubleAttribute ("userMin"), nullptr);

        if (entry->hasAttribute ("userMax"))
            param.setProperty (id::paramUserMax, entry->getDoubleAttribute ("userMax"), nullptr);

        if (entry->getBoolAttribute ("uiHidden"))
            param.setProperty (id::paramUiHidden, true, nullptr);

        if (entry->hasAttribute ("curve"))
            param.setProperty (id::paramCurve, entry->getStringAttribute ("curve"), nullptr);
    }
}

bool ModuleUiDefaults::hasDefaultsFor (const juce::String& factoryId)
{
    auto* file = applicationProperties.getUserSettings();
    return file != nullptr && file->containsKey (keyFor (factoryId));
}

void ModuleUiDefaults::clearDefaultsFor (const juce::String& factoryId)
{
    if (auto* file = applicationProperties.getUserSettings())
    {
        file->removeValue (keyFor (factoryId));
        file->saveIfNeeded();
        sendChangeMessage();
    }
}

} // namespace conduit
