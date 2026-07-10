#include <catch2/catch_test_macros.hpp>

#include "Core/CcControlModel.h"

namespace grid = conduit::grid;

//==============================================================================
TEST_CASE ("CcControlModel: addControl normalisiert verkehrte Ecken", "[grid]")
{
    grid::CcControlModel model;
    const auto id = model.addControl (grid::CcTool::fader, 5, 3, 2, 1);

    auto* control = model.find (id);
    REQUIRE (control != nullptr);
    REQUIRE (control->type == grid::CcTool::fader);
    REQUIRE (control->c0 == 2);
    REQUIRE (control->c1 == 5);
    REQUIRE (control->r0 == 1);
    REQUIRE (control->r1 == 3);
}

TEST_CASE ("CcControlModel: moveTo erhaelt die Groesse und klemmt an allen vier Raendern", "[grid]")
{
    grid::CcControlModel model;
    const auto id = model.addControl (grid::CcTool::xy, 2, 1, 4, 2);   // 3×2 Zellen

    // Über den linken/oberen Rand hinaus -> Ursprung (0,0)
    REQUIRE (model.moveTo (id, -3, -5, 8, 4));
    auto* control = model.find (id);
    REQUIRE (control != nullptr);
    REQUIRE (control->c0 == 0);
    REQUIRE (control->c1 == 2);
    REQUIRE (control->r0 == 0);
    REQUIRE (control->r1 == 1);

    // Über den rechten/unteren Rand hinaus -> ans Maximum, Größe bleibt 3×2
    REQUIRE (model.moveTo (id, 99, 99, 8, 4));
    REQUIRE (control->c0 == 5);
    REQUIRE (control->c1 == 7);
    REQUIRE (control->r0 == 2);
    REQUIRE (control->r1 == 3);

    // Unbekannte id -> false, kein Effekt
    REQUIRE_FALSE (model.moveTo (4711, 0, 0, 8, 4));
}

TEST_CASE ("CcControlModel: remove entfernt genau das eine Control", "[grid]")
{
    grid::CcControlModel model;
    const auto first  = model.addControl (grid::CcTool::push, 0, 0, 0, 0);
    const auto second = model.addControl (grid::CcTool::toggle, 1, 1, 1, 1);

    model.remove (first);

    REQUIRE (model.find (first) == nullptr);
    REQUIRE (model.find (second) != nullptr);
    REQUIRE (model.controls().size() == 1);

    model.remove (4711);   // unbekannte id: kein Effekt
    REQUIRE (model.controls().size() == 1);
}

TEST_CASE ("CcControlModel: controlAt liefert das oberste Control, coversCell die Flaeche", "[grid]")
{
    grid::CcControlModel model;
    const auto below = model.addControl (grid::CcTool::fader, 0, 0, 3, 3);
    const auto above = model.addControl (grid::CcTool::push, 2, 2, 3, 3);   // überlappt unten rechts

    REQUIRE (model.controlAt (0, 0) == below);
    REQUIRE (model.controlAt (2, 2) == above);   // zuletzt platziert liegt oben
    REQUIRE (model.controlAt (7, 0) == -1);      // freie Zelle

    const auto* control = model.find (below);
    REQUIRE (control != nullptr);
    REQUIRE (grid::CcControlModel::coversCell (*control, 3, 3));
    REQUIRE (grid::CcControlModel::coversCell (*control, 0, 3));
    REQUIRE_FALSE (grid::CcControlModel::coversCell (*control, 4, 3));
    REQUIRE_FALSE (grid::CcControlModel::coversCell (*control, 3, 4));
}

TEST_CASE ("CcControlModel: clear entfernt alles und setzt die Id-Vergabe zurueck", "[grid]")
{
    grid::CcControlModel model;
    model.addControl (grid::CcTool::fader, 0, 0, 0, 1);
    model.addControl (grid::CcTool::xy, 1, 0, 2, 1);

    model.clear();
    REQUIRE (model.controls().empty());

    // Id-Vergabe beginnt wieder bei 1 (deterministische System-Bestueckung).
    REQUIRE (model.addControl (grid::CcTool::toggle, 0, 0, 0, 0) == 1);
}

//==============================================================================
TEST_CASE ("buildXyFaderLayout: XY + 6 Fader decken alle 16 Zellen des 8x2-Rasters", "[grid]")
{
    grid::CcControlModel model;
    grid::buildXyFaderLayout (model);

    // 1 XY-Pad ueber (0,0)-(1,1) plus 6 vertikale Fader (c,0)-(c,1), c=2..7.
    const auto& controls = model.controls();
    REQUIRE (controls.size() == 7);

    REQUIRE (controls[0].type == grid::CcTool::xy);
    REQUIRE (controls[0].c0 == 0);
    REQUIRE (controls[0].r0 == 0);
    REQUIRE (controls[0].c1 == 1);
    REQUIRE (controls[0].r1 == 1);

    int faderCount = 0;
    for (size_t i = 1; i < controls.size(); ++i)
    {
        REQUIRE (controls[i].type == grid::CcTool::fader);
        REQUIRE (controls[i].c0 == (int) i + 1);   // Spalten 2..7
        REQUIRE (controls[i].c1 == controls[i].c0);
        REQUIRE (controls[i].r0 == 0);
        REQUIRE (controls[i].r1 == 1);
        ++faderCount;
    }
    REQUIRE (faderCount == 6);

    // Keine freie Zelle: im Play-Modus faellt damit nichts zum Keyboard
    // durch -- die ueberdeckten Pads sind unspielbar (GridPage, 10.07.2026).
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 8; ++c)
            REQUIRE (model.controlAt (c, r) >= 0);
}
