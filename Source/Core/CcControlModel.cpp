#include "CcControlModel.h"

#include <algorithm>

namespace conduit::grid
{

int CcControlModel::addControl (CcTool type, int c0, int r0, int c1, int r1)
{
    CcControl control;
    control.id   = nextId++;
    control.type = type;
    control.c0 = std::min (c0, c1);
    control.c1 = std::max (c0, c1);
    control.r0 = std::min (r0, r1);
    control.r1 = std::max (r0, r1);

    items.push_back (control);
    return control.id;
}

void CcControlModel::restore (const CcControl& control)
{
    if (control.id <= 0 || find (control.id) != nullptr)
        return;   // ungueltige oder doppelte Id — defensiv ignorieren

    items.push_back (control);
    nextId = std::max (nextId, control.id + 1);
}

void CcControlModel::remove (int id) noexcept
{
    items.erase (std::remove_if (items.begin(), items.end(),
                                 [id] (const CcControl& control) { return control.id == id; }),
                 items.end());
}

void CcControlModel::clear() noexcept
{
    items.clear();
    nextId = 1;
}

bool CcControlModel::moveTo (int id, int c0, int r0, int cols, int rows) noexcept
{
    auto* control = find (id);
    if (control == nullptr)
        return false;

    const auto width  = control->c1 - control->c0;
    const auto height = control->r1 - control->r0;

    // Größe erhalten, Ursprung in die Rastergrenzen klemmen (max-Guard für
    // Controls, die größer als das Raster wären).
    const auto clampedC0 = std::clamp (c0, 0, std::max (0, cols - 1 - width));
    const auto clampedR0 = std::clamp (r0, 0, std::max (0, rows - 1 - height));

    control->c0 = clampedC0;
    control->c1 = clampedC0 + width;
    control->r0 = clampedR0;
    control->r1 = clampedR0 + height;
    return true;
}

CcControl* CcControlModel::find (int id) noexcept
{
    for (auto& control : items)
        if (control.id == id)
            return &control;

    return nullptr;
}

int CcControlModel::controlAt (int cellC, int cellR) const noexcept
{
    // Rückwärts: zuletzt platzierte Controls liegen oben.
    for (auto it = items.rbegin(); it != items.rend(); ++it)
        if (coversCell (*it, cellC, cellR))
            return it->id;

    return -1;
}

bool CcControlModel::coversCell (const CcControl& control, int cellC, int cellR) noexcept
{
    return cellC >= control.c0 && cellC <= control.c1
        && cellR >= control.r0 && cellR <= control.r1;
}

//==============================================================================
void buildXyFaderLayout (CcControlModel& model)
{
    // TODO(design): Anzahl/Anordnung der Controls + CC-Zuweisung final vom
    // User — vorerst XY links (2×2 Zellen) + 6 vertikale Fader (User-SVG
    // gridMpeXy als Vorlage).
    model.addControl (CcTool::xy, 0, 0, 1, 1);

    for (int col = 2; col < 8; ++col)
        model.addControl (CcTool::fader, col, 0, col, 1);
}

} // namespace conduit::grid
