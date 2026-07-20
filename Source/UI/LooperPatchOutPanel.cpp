#include "LooperPatchOutPanel.h"

#include "Core/SignalFlowColours.h"
#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int meterWidth  = 120;  // wie die I/O-Endpunkt-Meter (User 19.07.2026)
    constexpr int meterHeight = 14;
    constexpr int chipX       = 3;    // Farbstreifen LINKS (Optik audio_in)
    constexpr int chipWidth   = 4;
    constexpr int chipHeight  = 12;
    constexpr int textX       = 26;   // Label linksbündig hinter Dreieck/Streifen
    constexpr int labelColumn = 92;   // Text-Spalte — Meter folgt DIREKT dahinter
}

LooperPatchOutPanel::LooperPatchOutPanel (juce::ValueTree nodeTreeToBind,
                                          LevelMeter* looperLevelsToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      looperLevels (looperLevelsToUse)
{
    nodeTree.addListener (this);
    rebuildSpecs();
}

LooperPatchOutPanel::~LooperPatchOutPanel()
{
    nodeTree.removeListener (this);
}

void LooperPatchOutPanel::stopUpdates()
{
    frozen = true;
    nodeTree.removeListener (this);
    for (auto& bar : meterBars)
        if (bar != nullptr)
            bar->stopUpdates();
    setInterceptsMouseClicks (false, false);
}

//==============================================================================
int LooperPatchOutPanel::sectionOfSpec (const LooperPatchOutModule::OutputSpec& spec) noexcept
{
    using Kind = LooperPatchOutModule::Kind;

    switch (spec.kind)
    {
        case Kind::track:  return juce::jlimit (0, 3, spec.looper - 1);
        case Kind::bus:    return sectionBusses;
        case Kind::send:   return sectionSends;
        case Kind::master: break;
    }
    return -1;
}

std::vector<LooperPatchOutPanel::Row> LooperPatchOutPanel::buildRows (
    const std::vector<LooperPatchOutModule::OutputSpec>& specs, int collapsedMask)
{
    using Kind = LooperPatchOutModule::Kind;

    std::vector<Row> rows;
    rows.reserve (specs.size() + 6);

    int lastHeaderSection = -1;
    for (int i = 0; i < (int) specs.size(); ++i)
    {
        const auto& spec = specs[(size_t) i];
        const auto section = sectionOfSpec (spec);

        // Überschrifts-Zeile beim Sektions-Wechsel (Looper n | Busse | Sends)
        if (section >= 0 && section != lastHeaderSection)
        {
            const auto title = spec.kind == Kind::track
                                 ? "Looper " + juce::String (spec.looper)
                                 : juce::String (spec.kind == Kind::bus ? "Busse" : "Returns");
            rows.push_back ({ title, -1, section,
                              (collapsedMask & (1 << section)) != 0 });
            lastHeaderSection = section;
        }

        if (section >= 0 && (collapsedMask & (1 << section)) != 0)
            continue;   // eingeklappt: Slot-Zeile entfällt

        juce::String label;
        switch (spec.kind)
        {
            case Kind::track:
                label = "Track " + juce::String (
                            LooperPatchOutModule::globalTrackNumber (spec));
                break;
            case Kind::bus:
                // „Bus 1–4" statt „Looper n · Bus" (User-Feedback Runde 3)
                label = "Bus " + juce::String (spec.looper);
                break;
            case Kind::send:
                label = "Send " + juce::String (spec.send);
                break;
            case Kind::master:
                label = "Master";
                break;
        }

        rows.push_back ({ std::move (label), i, section, false });
    }

    return rows;
}

int LooperPatchOutPanel::heightForSpecs (
    const std::vector<LooperPatchOutModule::OutputSpec>& specs, int collapsedMask)
{
    const auto numRows = juce::jmax ((size_t) 1, buildRows (specs, collapsedMask).size());
    return topPadding + (int) numRows * rowHeight + bottomPadding;
}

int LooperPatchOutPanel::rowCentreYForSlot (
    const std::vector<LooperPatchOutModule::OutputSpec>& specs, int collapsedMask,
    int slotIndex)
{
    const auto rows = buildRows (specs, collapsedMask);

    for (int r = 0; r < (int) rows.size(); ++r)
        if (rows[(size_t) r].slotIndex == slotIndex)
            return topPadding + r * rowHeight + rowHeight / 2;

    // Eingeklappt: alle Kabel der Sektion verlassen das Modul an der
    // Überschrifts-Zeile (User 19.07.2026)
    if (juce::isPositiveAndBelow (slotIndex, (int) specs.size()))
    {
        const auto section = sectionOfSpec (specs[(size_t) slotIndex]);
        for (int r = 0; r < (int) rows.size(); ++r)
            if (rows[(size_t) r].isHeader() && rows[(size_t) r].section == section)
                return topPadding + r * rowHeight + rowHeight / 2;
    }

    return topPadding + rowHeight / 2;
}

//==============================================================================
void LooperPatchOutPanel::rebuildSpecs()
{
    specs = LooperPatchOutModule::readOutputConfig (nodeTree);
    collapsedMask = currentMask();
    rows = buildRows (specs, collapsedMask);

    // Farben/Kabel-Flags ERHALTEN (Ein-/Ausklappen wischt sie sonst bis
    // zum nächsten refreshFlowColours weg — User-Bug-Report 19.07.2026)
    slotColours.resize (specs.size(), 0);
    slotHasCable.resize (specs.size(), 0);

    // Meter 1:1 zu den Slots — stabile Kanäle aus dem 4er-Raster
    // (meterChannelOf); ohne Meter-Quelle bleiben die Slots leer
    for (auto& bar : meterBars)
        if (bar != nullptr)
            bar->stopUpdates();
    meterBars.clear();

    for (const auto& spec : specs)
    {
        std::unique_ptr<LevelMeterBar> bar;
        if (const auto channel = LooperPatchOutModule::meterChannelOf (spec);
            looperLevels != nullptr && channel >= 0)
        {
            bar = std::make_unique<LevelMeterBar> (looperLevels, channel, 2);
            addAndMakeVisible (*bar);
        }
        meterBars.push_back (std::move (bar));
    }

    resized();
    repaint();
}

void LooperPatchOutPanel::refreshSlotColours (
    const std::function<juce::uint32 (int channel)>& resolveRgb,
    const std::function<juce::uint32 (int looperIndex)>& resolveHeaderRgb,
    const std::function<bool (int channel)>& resolveHasCable)
{
    bool changed = false;

    if (resolveRgb != nullptr)
        for (int i = 0; i < (int) specs.size(); ++i)
        {
            const auto rgb = resolveRgb (i * LooperPatchOutModule::slotWidth);
            if (slotColours[(size_t) i] != rgb)
            {
                slotColours[(size_t) i] = rgb;
                changed = true;
            }
        }

    if (resolveHasCable != nullptr)
        for (int i = 0; i < (int) specs.size(); ++i)
        {
            const auto hasCable = (char) resolveHasCable (i * LooperPatchOutModule::slotWidth);
            if (slotHasCable[(size_t) i] != hasCable)
            {
                slotHasCable[(size_t) i] = hasCable;
                changed = true;
            }
        }

    // Looper-Überschriften: Farbe der gewählten Quelle (User 19.07.2026)
    if (resolveHeaderRgb != nullptr)
        for (int l = 0; l < 4; ++l)
        {
            const auto rgb = resolveHeaderRgb (l);
            if (headerColours[(size_t) l] != rgb)
            {
                headerColours[(size_t) l] = rgb;
                changed = true;
            }
        }

    if (changed)
        repaint();
}

//==============================================================================
void LooperPatchOutPanel::valueTreePropertyChanged (juce::ValueTree& tree,
                                                  const juce::Identifier& property)
{
    if (tree.hasType (id::output) || (tree == nodeTree && property == id::outCollapsed))
        rebuildSpecs();
}

void LooperPatchOutPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (id::outputs) || parent == nodeTree)
        rebuildSpecs();
}

void LooperPatchOutPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (id::outputs) || parent == nodeTree)
        rebuildSpecs();
}

//==============================================================================
bool LooperPatchOutPanel::sectionHasCables (int section) const
{
    for (int i = 0; i < (int) specs.size(); ++i)
        if (sectionOfSpec (specs[(size_t) i]) == section && slotHasCable[(size_t) i] != 0)
            return true;
    return false;
}

juce::uint32 LooperPatchOutPanel::sectionStrandRgb (int section) const
{
    std::vector<juce::uint32> mix;
    for (int i = 0; i < (int) specs.size(); ++i)
        if (sectionOfSpec (specs[(size_t) i]) == section && slotHasCable[(size_t) i] != 0
            && slotColours[(size_t) i] != 0)
            mix.push_back (slotColours[(size_t) i]);

    const auto blended = flow_colours::blendRgb (mix);
    return blended != 0 ? blended : 0x8fd0a0u;   // Default-Kabelgrün
}

std::vector<LooperPatchOutPanel::CollapsedStrand> LooperPatchOutPanel::getCollapsedStrands() const
{
    std::vector<CollapsedStrand> strands;
    for (int r = 0; r < (int) rows.size(); ++r)
    {
        const auto& row = rows[(size_t) r];
        if (row.isHeader() && row.collapsed && sectionHasCables (row.section))
            strands.push_back ({ topPadding + r * rowHeight + rowHeight / 2,
                                 sectionStrandRgb (row.section) });
    }
    return strands;
}

//==============================================================================
void LooperPatchOutPanel::resized()
{
    for (int i = 0; i < (int) meterBars.size(); ++i)
        if (auto* bar = meterBars[(size_t) i].get())
        {
            const auto visible = ! isSlotCollapsed (i);
            bar->setVisible (visible);
            if (visible)
                bar->setBounds (labelColumn,
                                slotCentreY (i) - meterHeight / 2,
                                juce::jmin (meterWidth, getWidth() - labelColumn - 6),
                                meterHeight);
        }
}

void LooperPatchOutPanel::mouseUp (const juce::MouseEvent& event)
{
    if (frozen || event.mouseWasDraggedSinceMouseDown())
        return;

    const auto rowIndex = (event.y - topPadding) / rowHeight;
    if (! juce::isPositiveAndBelow (rowIndex, (int) rows.size())
        || ! rows[(size_t) rowIndex].isHeader())
        return;

    // Sektion ein-/ausklappen — VIEW-Zustand, ohne UndoManager (Muster
    // activePage); Panel + NodeComponent folgen über den Property-Listener
    const auto mask = collapsedMask ^ (1 << rows[(size_t) rowIndex].section);
    nodeTree.setProperty (id::outCollapsed, mask, nullptr);
}

void LooperPatchOutPanel::paint (juce::Graphics& g)
{
    using Kind = LooperPatchOutModule::Kind;

    for (int r = 0; r < (int) rows.size(); ++r)
    {
        const auto& row = rows[(size_t) r];
        const juce::Rectangle<int> rowArea { 0, topPadding + r * rowHeight,
                                             getWidth(), rowHeight };

        if (row.isHeader())
        {
            // ▸/▾-Dreieck vor der Überschrift (Einklapp-Indikator)
            juce::Path triangle;
            const auto cx = 14.0f;
            const auto cy = (float) rowArea.getCentreY();
            if (row.collapsed)
                triangle.addTriangle (cx - 3.0f, cy - 5.0f, cx - 3.0f, cy + 5.0f,
                                      cx + 4.0f, cy);
            else
                triangle.addTriangle (cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f,
                                      cx, cy + 4.0f);

            g.setColour (push::colours::textDim);
            g.fillPath (triangle);

            const auto headerFont = push::scaledFont (12.0f, true);
            g.setColour (push::colours::text);
            g.setFont (headerFont);
            g.drawText (row.label,
                        rowArea.withTrimmedLeft (textX).withTrimmedRight (8),
                        juce::Justification::centredLeft, false);

            // Textbreite für Punkt + Strang-Linie (User-Sketch 19.07.2026:
            // beides exakt auf der SCHRIFTLINIE, direkt hinter dem Label)
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (headerFont, row.label, 0.0f, 0.0f);
            auto x = (float) textX + glyphs.getBoundingBox (0, -1, true).getWidth() + 8.0f;

            // Punkt/Linie auf der optischen BUCHSTABEN-Linie (User-Lineal)
            const auto strandY = cy + (float) strandYOffset;

            // Eingeklappt + Kabel: Strang-Linie in KABELDICKE (3 px, wie
            // cableStroke) beginnt DIREKT am Punkt (kein Abstand, User
            // Runde 10); linkes Ende rund im Punkt-Radius, rechtes Ende
            // eckig — der NodeComponent setzt nahtlos fort
            const auto hasStrand = row.collapsed && sectionHasCables (row.section);
            if (hasStrand)
            {
                const auto rgb = sectionStrandRgb (row.section);
                juce::Path line;
                line.addRoundedRectangle (x, strandY - 1.5f,
                                          (float) getWidth() - x, 3.0f,
                                          1.5f, 1.5f, true, false, true, false);
                g.setColour (juce::Colour (0xff000000u | (rgb & 0x00ffffffu)));
                g.fillPath (line);
            }

            // Quellfarbe des Loopers als PUNKT hinter dem Text — Ø =
            // Linien-Dicke (3 px), sitzt bei Strang direkt auf dessen
            // rundem Anfang (verschmilzt grafisch)
            if (row.section >= 0 && row.section < 4)
                if (const auto rgb = headerColours[(size_t) row.section]; rgb != 0)
                {
                    g.setColour (juce::Colour (0xff000000u | (rgb & 0x00ffffffu)));
                    g.fillEllipse (x, strandY - 1.5f, 3.0f, 3.0f);
                }
            continue;
        }

        const auto& spec = specs[(size_t) row.slotIndex];

        // Dünner Trenner nur noch über der Master-Zeile
        if (spec.kind == Kind::master)
        {
            g.setColour (push::colours::outline);
            g.fillRect (rowArea.getX() + 4, rowArea.getY(), rowArea.getWidth() - 8, 1);
        }

        // Farbstreifen LINKS (Optik des audio_in-Kanalstreifens): zeigt,
        // wo welche Clips ankommen (User 19.07.2026)
        if (const auto rgb = slotColours[(size_t) row.slotIndex]; rgb != 0)
        {
            g.setColour (juce::Colour (0xff000000u | (rgb & 0x00ffffffu)));
            g.fillRoundedRectangle (
                juce::Rectangle<float> ((float) chipX,
                                        (float) rowArea.getCentreY() - chipHeight / 2.0f,
                                        (float) chipWidth, (float) chipHeight),
                2.0f);
        }

        g.setFont (push::scaledFont (12.0f, false));
        g.setColour (spec.kind == Kind::master ? push::colours::text
                                               : push::colours::textDim);
        g.drawText (row.label,
                    rowArea.withTrimmedLeft (textX).withWidth (labelColumn - textX),
                    juce::Justification::centredLeft, false);
    }
}

} // namespace conduit
