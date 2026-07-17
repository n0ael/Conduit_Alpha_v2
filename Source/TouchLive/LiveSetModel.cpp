#include "LiveSetModel.h"

#include <juce_events/juce_events.h>

namespace conduit
{

namespace
{
    /** var-Deep-Vergleich: juce::var vergleicht DynamicObjects nur über
        Pointer und Arrays elementweise mit var::operator== — für frisch
        geparstes JSON braucht der Flacker-Schutz einen echten
        Inhaltsvergleich (LiveSetModel-Doku). */
    [[nodiscard]] bool deepEquals (const juce::var& a, const juce::var& b)
    {
        if (auto* objectA = a.getDynamicObject())
        {
            auto* objectB = b.getDynamicObject();

            if (objectB == nullptr)
                return false;

            const auto& propsA = objectA->getProperties();
            const auto& propsB = objectB->getProperties();

            if (propsA.size() != propsB.size())
                return false;

            for (const auto& prop : propsA)
            {
                if (! propsB.contains (prop.name))
                    return false;

                if (! deepEquals (prop.value, propsB[prop.name]))
                    return false;
            }

            return true;
        }

        if (auto* arrayA = a.getArray())
        {
            auto* arrayB = b.getArray();

            if (arrayB == nullptr || arrayA->size() != arrayB->size())
                return false;

            for (int i = 0; i < arrayA->size(); ++i)
                if (! deepEquals (arrayA->getReference (i), arrayB->getReference (i)))
                    return false;

            return true;
        }

        return a == b;
    }

    [[nodiscard]] bool isSuppressed (const LiveSetModel::SuppressionCheck& shouldSuppress,
                                     const juce::String& domainName,
                                     const juce::String& key,
                                     const juce::String& field)
    {
        return shouldSuppress != nullptr && shouldSuppress (domainName, key, field);
    }
} // namespace

//==============================================================================
juce::ValueTree LiveSetModel::getDomain (const juce::String& domainName)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto existing = state.getChildWithProperty (touchlive::id::domainName, domainName);

    if (existing.isValid())
        return existing;

    juce::ValueTree fresh (touchlive::id::domain);
    fresh.setProperty (touchlive::id::domainName, domainName, nullptr);
    state.appendChild (fresh, nullptr);
    return fresh;
}

juce::ValueTree LiveSetModel::findItem (const juce::String& domainName,
                                        const juce::String& key)
{
    return getDomain (domainName).getChildWithProperty (touchlive::id::itemKey, key);
}

//==============================================================================
void LiveSetModel::applySnapshot (const juce::String& domainName, const juce::var& payload,
                                  const SuppressionCheck& shouldSuppress)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto* object = payload.getDynamicObject();

    if (object == nullptr)
        return;

    auto domainTree = getDomain (domainName);
    const auto& incoming = object->getProperties();

    // Tree-DIFF statt Clear+Rebuild: erst verschwundene Skalar-/Array-Keys …
    for (int p = domainTree.getNumProperties(); --p >= 0;)
    {
        const auto propName = domainTree.getPropertyName (p);

        if (propName == touchlive::id::domainName || incoming.contains (propName))
            continue;

        if (isSuppressed (shouldSuppress, domainName, propName.toString(), {}))
            continue;

        domainTree.removeProperty (propName, nullptr);
    }

    // … dann verschwundene Items entfernen
    for (int c = domainTree.getNumChildren(); --c >= 0;)
    {
        const auto key = domainTree.getChild (c)
                                   .getProperty (touchlive::id::itemKey).toString();

        if (key.isEmpty() || ! incoming.contains (juce::Identifier (key)))
            domainTree.removeChild (c, nullptr);
    }

    // … zuletzt alle Keys inkrementell anwenden
    for (const auto& prop : incoming)
        applyKeyValue (domainTree, domainName, prop.name.toString(), prop.value,
                       shouldSuppress);
}

void LiveSetModel::applyDiff (const juce::String& domainName, const juce::var& payload,
                              const SuppressionCheck& shouldSuppress)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto* object = payload.getDynamicObject();

    if (object == nullptr)
        return;

    auto domainTree = getDomain (domainName);

    for (const auto& prop : object->getProperties())
    {
        const auto key = prop.name.toString();

        if (key.isEmpty())
            continue;

        // JSON null → Key entfernt (Diff-Semantik der Gegenseite)
        if (prop.value.isVoid())
        {
            auto child = domainTree.getChildWithProperty (touchlive::id::itemKey, key);

            if (child.isValid())
                domainTree.removeChild (child, nullptr);

            if (domainTree.hasProperty (prop.name))
                domainTree.removeProperty (prop.name, nullptr);

            continue;
        }

        applyKeyValue (domainTree, domainName, key, prop.value, shouldSuppress);
    }
}

//==============================================================================
void LiveSetModel::setItemField (const juce::String& domainName, const juce::String& key,
                                 const juce::Identifier& field, const juce::var& value)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto item = findItem (domainName, key);

    if (item.isValid() && ! deepEquals (item.getProperty (field), value))
        item.setProperty (field, value, nullptr);
}

void LiveSetModel::setItemArrayElement (const juce::String& domainName, const juce::String& key,
                                        const juce::Identifier& field, int index,
                                        const juce::var& value)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto item = findItem (domainName, key);

    if (! item.isValid())
        return;

    const auto* source = item.getProperty (field).getArray();

    if (source == nullptr || index < 0 || index >= source->size()
        || deepEquals (source->getReference (index), value))
        return;

    // Neues Array bauen: var vergleicht Arrays über den Pointer, in-place-Mutation
    // löste sonst KEINE Listener-Benachrichtigung aus (LiveSetModel-var-Falle).
    juce::Array<juce::var> updated (*source);
    updated.set (index, value);
    item.setProperty (field, juce::var (std::move (updated)), nullptr);
}

//==============================================================================
void LiveSetModel::applyKeyValue (juce::ValueTree domainTree, const juce::String& domainName,
                                  const juce::String& key, const juce::var& value,
                                  const SuppressionCheck& shouldSuppress)
{
    const juce::Identifier keyId { key };

    if (auto* object = value.getDynamicObject())
    {
        // Objekt-Wert → Item-Kind; Typwechsel Skalar→Objekt aufräumen
        if (domainTree.hasProperty (keyId))
            domainTree.removeProperty (keyId, nullptr);

        auto item = domainTree.getChildWithProperty (touchlive::id::itemKey, key);

        if (! item.isValid())
        {
            item = juce::ValueTree (touchlive::id::item);
            item.setProperty (touchlive::id::itemKey, key, nullptr);
            domainTree.appendChild (item, nullptr);
        }

        const auto& fields = object->getProperties();

        // Der Wert ersetzt das Objekt KOMPLETT — fehlende Felder entfernen
        for (int p = item.getNumProperties(); --p >= 0;)
        {
            const auto fieldName = item.getPropertyName (p);

            if (fieldName == touchlive::id::itemKey || fields.contains (fieldName))
                continue;

            if (isSuppressed (shouldSuppress, domainName, key, fieldName.toString()))
                continue;

            item.removeProperty (fieldName, nullptr);
        }

        for (const auto& field : fields)
        {
            if (isSuppressed (shouldSuppress, domainName, key, field.name.toString()))
                continue;

            if (! deepEquals (item.getProperty (field.name), field.value))
                item.setProperty (field.name, field.value, nullptr);
        }

        return;
    }

    // Skalar/Array → Property am Domain-Tree
    if (isSuppressed (shouldSuppress, domainName, key, {}))
        return;

    // Typwechsel Objekt→Skalar aufräumen
    {
        auto item = domainTree.getChildWithProperty (touchlive::id::itemKey, key);

        if (item.isValid())
            domainTree.removeChild (item, nullptr);
    }

    if (! deepEquals (domainTree.getProperty (keyId), value))
        domainTree.setProperty (keyId, value, nullptr);
}

} // namespace conduit
