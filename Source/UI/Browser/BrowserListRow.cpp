#include "BrowserListRow.h"

#include "UI/Browser/BrowserDragPayload.h"
#include "UI/PushIcons.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    /** Core-Icon-Referenz → Push-Icon (das Modell kennt keine UI-Header). */
    push::Icon toPushIcon (BrowserModel::Icon icon)
    {
        switch (icon)
        {
            case BrowserModel::Icon::projects:  return push::Icon::browserProjects;
            case BrowserModel::Icon::audio:     return push::Icon::browserAudio;
            case BrowserModel::Icon::cvControl: return push::Icon::browserCvControl;
            case BrowserModel::Icon::audioFx:   return push::Icon::browserAudioFx;
            case BrowserModel::Icon::none:      break;
        }

        return push::Icon::browserPanel;  // unbenutzt (none zeichnet nichts)
    }
} // namespace

BrowserListRow::BrowserListRow()
{
    setInterceptsMouseClicks (true, false);
}

void BrowserListRow::update (const BrowserModel::Row& rowToShow, int rowIndexToUse,
                             bool isSelected)
{
    row      = rowToShow;
    rowIndex = rowIndexToUse;
    selected = isSelected;
    repaint();
}

void BrowserListRow::paint (juce::Graphics& g)
{
    const auto isHint   = row.kind == BrowserModel::Row::Kind::hint;
    const auto isHeader = row.kind == BrowserModel::Row::Kind::branch;

    if (selected && ! isHint && ! isHeader)
    {
        g.setColour (push::colours::tileActive);
        g.fillAll();

        // EIN Akzent: schmaler Selektionsbalken links (Ableton-Look)
        g.setColour (push::colours::ledOrange);
        g.fillRect (0, 0, 3, getHeight());
    }

    auto area = getLocalBounds().reduced (12, 0);
    area.removeFromLeft (row.indent * 18);   // eingerückte Ebene

    // Icon links im 24-px-Raster (nur sichtbare Zeilen zeichnen — Lazy)
    const auto iconBox = area.removeFromLeft (24).withSizeKeepingCentre (24, 24);
    if (row.icon != BrowserModel::Icon::none)
        push::draw (g, toPushIcon (row.icon), iconBox.toFloat().reduced (1.0f),
                    isHint ? push::colours::textDim : push::colours::text);

    area.removeFromLeft (10);

    // Navigierbare Zeilen zeigen ein PERMANENTES ▸ (keine Hover-Affordance)
    const auto navigates = row.kind == BrowserModel::Row::Kind::section
                        || row.kind == BrowserModel::Row::Kind::category;
    if (navigates)
    {
        const auto chevron = area.removeFromRight (16);
        juce::Path arrow;
        arrow.addTriangle (0.0f, -5.0f, 0.0f, 5.0f, 5.5f, 0.0f);
        g.setColour (push::colours::textDim);
        g.fillPath (arrow, juce::AffineTransform::translation (
                               (float) chevron.getX() + 4.0f,
                               (float) chevron.getCentreY()));
    }

    // Sekundär-Info rechtsbündig dim (Suche: Kategorie; M6: Dauer/Format)
    if (row.secondary.isNotEmpty())
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (12.0f));
        const auto secondaryArea = area.removeFromRight (
            juce::jmin (area.getWidth() / 2, 120));
        g.drawText (row.secondary, secondaryArea,
                    juce::Justification::centredRight, true);
        area.removeFromRight (8);
    }

    g.setColour (isHint || isHeader ? push::colours::textDim : push::colours::text);
    g.setFont (push::scaledFont (isHeader ? 13.0f : 16.0f, isHeader));
    // Nie horizontal stauchen (User-Regel 07/2026) — kürzen statt quetschen
    g.drawText (isHeader ? row.label.toUpperCase() : row.label,
                area, juce::Justification::centredLeft, true);
}

void BrowserListRow::mouseDown (const juce::MouseEvent&)
{
    // Tap-Feedback übernimmt die Selektion nach dem mouseUp — hier nichts,
    // damit der Viewport vertikale Drags ungestört übernehmen kann
    dragStarted = false;
}

void BrowserListRow::mouseDrag (const juce::MouseEvent& event)
{
    if (dragStarted || row.kind != BrowserModel::Row::Kind::module)
        return;

    // Nur klar HORIZONTALE Bewegung wird zum Modul-Drag — vertikal
    // gehört dem Viewport (Flick-Scroll)
    const auto dx = std::abs (event.getDistanceFromDragStartX());
    const auto dy = std::abs (event.getDistanceFromDragStartY());

    if (dx <= tapThreshold || dx <= dy)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        // Kein eigenes Drag-Image: JUCE nimmt den Row-Snapshot
        container->startDragging (browser_drag::makeModulePayload (row.id),
                                  this, juce::ScaledImage(), true);
    }
}

void BrowserListRow::mouseUp (const juce::MouseEvent& event)
{
    // Tap nur ohne nennenswerte Bewegung — sonst war es Flick-Scroll/Drag
    if (dragStarted || event.getDistanceFromDragStart() > tapThreshold)
        return;

    if (! getLocalBounds().contains (event.getPosition()))
        return;

    if (onActivated != nullptr)
        onActivated (rowIndex);
}

} // namespace conduit
