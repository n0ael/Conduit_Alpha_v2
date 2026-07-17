#include "ControllerProfile.h"

namespace conduit::midirig
{

namespace
{
    /** Identisch zu MidiDeviceProfile.cpp::splitCsvLine (RFC-4180-Quoting) --
        bewusst dupliziert statt geteilt: beide Parser sind klein, pure,
        unabhaengige Dateien; ein gemeinsamer Header waere hier mehr
        Kopplung als Nutzen (Rule midirig: Profil-Formate duerfen getrennt
        driften). */
    juce::StringArray splitCsvLine (const juce::String& line)
    {
        juce::StringArray fields;
        juce::String current;
        bool inQuotes = false;

        for (int i = 0; i < line.length(); ++i)
        {
            const auto c = line[i];

            if (inQuotes)
            {
                if (c == '"')
                {
                    if (i + 1 < line.length() && line[i + 1] == '"')
                    {
                        current += '"';
                        ++i;
                    }
                    else
                    {
                        inQuotes = false;
                    }
                }
                else
                {
                    current += c;
                }
            }
            else if (c == '"')
            {
                inQuotes = true;
            }
            else if (c == ',')
            {
                fields.add (current);
                current.clear();
            }
            else
            {
                current += c;
            }
        }

        fields.add (current);
        return fields;
    }

    int fieldAsInt (const juce::StringArray& fields, int index, int fallback)
    {
        if (index < 0 || index >= fields.size())
            return fallback;

        const auto trimmed = fields[index].trim();
        if (trimmed.isEmpty() || ! trimmed.containsOnly ("-0123456789"))
            return fallback;

        return trimmed.getIntValue();
    }

    juce::String fieldAsString (const juce::StringArray& fields, int index)
    {
        return index >= 0 && index < fields.size() ? fields[index].trim() : juce::String();
    }

    AddressKind fieldAsAddressKind (const juce::StringArray& fields, int index)
    {
        const auto text = fieldAsString (fields, index);

        if (text.equalsIgnoreCase ("note"))
            return AddressKind::note;

        // M8: Pitch Bend (Motorfader/Ribbon, AlphaTrack) -- Adresse ist der Kanal.
        if (text.equalsIgnoreCase ("pitchbend") || text.equalsIgnoreCase ("pb"))
            return AddressKind::pitchBend;

        return AddressKind::cc;
    }

    /** Liest Feedback-Slot N (1-basiert) -- nullopt-Ersatz: number bleibt -1
        wenn die Spalte fehlt/leer ist (kein Feedback an dieser Stelle).
        pitchBend-Feedback (M8) braucht keine number (Adresse = Kanal). */
    bool readFeedbackSlot (const juce::StringArray& fields, int kindCol, int channelCol,
                          int numberCol, int meaningCol, FeedbackAddress& out)
    {
        const auto kind   = fieldAsAddressKind (fields, kindCol);
        const auto number = fieldAsInt (fields, numberCol, -1);
        if (number < 0 && kind != AddressKind::pitchBend)
            return false;

        out.kind    = kind;
        out.channel = juce::jlimit (1, 16, fieldAsInt (fields, channelCol, 1));
        out.number  = juce::jmax (0, number);
        out.meaning = fieldAsString (fields, meaningCol);
        return true;
    }
}

const ControllerControl* ControllerProfile::findBySendAddress (
    AddressKind kind, int number) const noexcept
{
    for (const auto& control : controls)
    {
        if (control.sendKind != kind)
            continue;

        // M8 pitchBend: die Adresse ist der KANAL (number-Param traegt ihn).
        const auto address = kind == AddressKind::pitchBend ? control.sendChannel
                                                            : control.sendNumber;
        if (address == number)
            return &control;
    }

    return nullptr;
}

ControllerProfile parseControllerProfileCsv (const juce::String& text, ControllerParseReport* report)
{
    ControllerProfile profile;

    ControllerParseReport localReport;
    auto& out = report != nullptr ? *report : localReport;
    out = {};

    juce::StringArray lines;
    lines.addLines (text);

    if (lines.isEmpty())
        return profile;

    const auto header = splitCsvLine (lines[0]);
    const auto columnIndex = [&header] (const char* columnName)
    {
        for (int i = 0; i < header.size(); ++i)
            if (header[i].trim().equalsIgnoreCase (columnName))
                return i;
        return -1;
    };

    const auto colId          = columnIndex ("id");
    const auto colType        = columnIndex ("type");
    const auto colSection     = columnIndex ("section");
    const auto colGroup       = columnIndex ("group");
    const auto colRole        = columnIndex ("role");
    const auto colMode        = columnIndex ("mode");
    const auto colSteps       = columnIndex ("steps");
    const auto colRelEncoding = columnIndex ("rel_encoding");
    const auto colTouchNumber = columnIndex ("touch_number");
    const auto colDevice      = columnIndex ("device");
    const auto colDisplay     = columnIndex ("display");
    const auto colSendKind    = columnIndex ("send_kind");
    const auto colSendChannel = columnIndex ("send_channel");
    const auto colSendNumber  = columnIndex ("send_number");

    struct FeedbackColumns { int kind, channel, number, meaning; };
    const FeedbackColumns feedbackCols[3] = {
        { columnIndex ("feedback1_kind"), columnIndex ("feedback1_channel"),
          columnIndex ("feedback1_number"), columnIndex ("feedback1_meaning") },
        { columnIndex ("feedback2_kind"), columnIndex ("feedback2_channel"),
          columnIndex ("feedback2_number"), columnIndex ("feedback2_meaning") },
        { columnIndex ("feedback3_kind"), columnIndex ("feedback3_channel"),
          columnIndex ("feedback3_number"), columnIndex ("feedback3_meaning") },
    };

    if (colId < 0 || colSendNumber < 0)
    {
        out.warnings.add ("Kopfzeile ohne id/send_number -- keine Conduit-Controller-Profile-v1-CSV?");
        return profile;
    }

    for (int lineIndex = 1; lineIndex < lines.size(); ++lineIndex)
    {
        if (lines[lineIndex].trim().isEmpty())
            continue;

        const auto fields = splitCsvLine (lines[lineIndex]);

        const auto id = fieldAsString (fields, colId);
        if (id.isEmpty())
        {
            ++out.skipped;
            continue;
        }

        if (profile.device.isEmpty())
        {
            const auto deviceField = fieldAsString (fields, colDevice);
            if (deviceField.isNotEmpty())
                profile.device = deviceField;
        }

        if (profile.display.isEmpty())
        {
            const auto displayField = fieldAsString (fields, colDisplay);
            if (displayField.isNotEmpty())
                profile.display = displayField;
        }

        ControllerControl control;
        control.id          = id;
        control.type        = fieldAsString (fields, colType);
        control.section     = fieldAsString (fields, colSection);
        control.group       = fieldAsString (fields, colGroup);
        control.role        = fieldAsString (fields, colRole);
        control.mode        = fieldAsString (fields, colMode);
        control.steps       = juce::jmax (0, fieldAsInt (fields, colSteps, 0));
        control.relEncoding = parseRelativeEncoding (fieldAsString (fields, colRelEncoding));
        control.touchNumber = fieldAsInt (fields, colTouchNumber, -1);
        control.sendKind    = fieldAsAddressKind (fields, colSendKind);
        control.sendChannel = juce::jlimit (1, 16, fieldAsInt (fields, colSendChannel, 1));
        control.sendNumber  = fieldAsInt (fields, colSendNumber, -1);

        // M8 pitchBend: die Adresse ist der Kanal -- send_number darf fehlen.
        if (control.sendNumber < 0 && control.sendKind == AddressKind::pitchBend)
            control.sendNumber = 0;

        if (control.sendNumber < 0)
        {
            ++out.skipped;
            out.warnings.add ("Zeile " + juce::String (lineIndex + 1) + " ('" + id
                              + "'): keine send_number -- uebersprungen");
            continue;
        }

        for (const auto& fbCol : feedbackCols)
        {
            FeedbackAddress fb;
            if (readFeedbackSlot (fields, fbCol.kind, fbCol.channel, fbCol.number, fbCol.meaning, fb))
                control.feedback.push_back (fb);
        }

        profile.controls.push_back (std::move (control));
        ++out.accepted;
    }

    return profile;
}

} // namespace conduit::midirig
