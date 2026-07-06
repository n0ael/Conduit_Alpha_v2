#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/RingTouchModel.h"

namespace grid = conduit::grid;
using Catch::Approx;

//==============================================================================
TEST_CASE ("RingTouchModel: primäre und Ring-Zuordnung nach Abstand", "[grid]")
{
    grid::RingTouchModel model;

    const auto d1 = model.onDown (1, { 0.0f, 0.0f });
    REQUIRE (d1.kind == grid::RingTouchModel::TouchKind::Primary);

    // Abstand 50 < grabRadiusPx (90) -> wird Ring von Finger 1
    const auto d2 = model.onDown (2, { 50.0f, 0.0f });
    REQUIRE (d2.kind == grid::RingTouchModel::TouchKind::Ring);
    REQUIRE (d2.ringOwner == 1);

    // Weit weg -> eigener primärer Finger
    const auto d3 = model.onDown (3, { 500.0f, 0.0f });
    REQUIRE (d3.kind == grid::RingTouchModel::TouchKind::Primary);
}

TEST_CASE ("RingTouchModel: zweiter Touch nahe an einem primären mit bestehendem Ring wird selbst primär", "[grid]")
{
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });
    model.onDown (2, { 50.0f, 0.0f }); // Ring von 1

    const auto d3 = model.onDown (3, { 60.0f, 0.0f }); // nah an 1, aber 1 hat schon einen Ring
    REQUIRE (d3.kind == grid::RingTouchModel::TouchKind::Primary);
}

TEST_CASE ("RingTouchModel: Radius moduliert Slide zwischen min-/maxRadiusPx", "[grid]")
{
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });
    model.onDown (2, { 50.0f, 0.0f });

    const auto atMax = model.onMove (2, { 220.0f, 0.0f }); // maxRadiusPx-Abstand von {0,0}
    REQUIRE (atMax.hasSlide);
    REQUIRE (atMax.owner == 1);
    REQUIRE (atMax.slide01 == Approx (1.0f));

    const auto atMin = model.onMove (2, { 40.0f, 0.0f }); // minRadiusPx-Abstand
    REQUIRE (atMin.hasSlide);
    REQUIRE (atMin.slide01 == Approx (0.0f));

    // Bewegung des primären Fingers selbst liefert kein Slide-Event
    // (Zentrum folgt zwar, aber das meldet onMove nicht als Slide)
    const auto primaryMove = model.onMove (1, { 5.0f, 0.0f });
    REQUIRE_FALSE (primaryMove.hasSlide);
}

TEST_CASE ("RingTouchModel: primäres Zentrum folgt dem Finger, Ring-Offset bleibt konstant", "[grid]")
{
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });
    model.onDown (2, { 50.0f, 0.0f }); // Ring von 1, Radius 50

    auto circles = model.activeCircles();
    REQUIRE (circles[0].center.x == Approx (0.0f));
    REQUIRE (circles[0].radiusPx == Approx (50.0f));

    // Primärer Finger wandert nach rechts -- Zentrum folgt; der Ring-Finger
    // bewegt sich dabei NICHT selbst, also bleibt der Offset (Radius 50)
    // unverändert -- der Mond wandert mit der Sonne mit.
    const auto primaryMove = model.onMove (1, { 30.0f, 0.0f });
    REQUIRE_FALSE (primaryMove.hasSlide);

    circles = model.activeCircles();
    REQUIRE (circles[0].center.x == Approx (30.0f));
    REQUIRE (circles[0].radiusPx == Approx (50.0f));   // unverändert
    REQUIRE (circles[0].orbitPos.x == Approx (80.0f)); // center(30) + Offset(50)
}

TEST_CASE ("RingTouchModel: primäre Bewegung ohne Ring hält den Kreis auf minRadiusPx", "[grid]")
{
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });

    model.onMove (1, { 200.0f, 0.0f });

    const auto circles = model.activeCircles();
    REQUIRE (circles[0].center.x == Approx (200.0f));
    REQUIRE (circles[0].radiusPx == Approx (40.0f)); // minRadiusPx unverändert
    REQUIRE_FALSE (circles[0].hasOrbit); // nie ein Ring angedockt -- kein Mond
}

TEST_CASE ("RingTouchModel: Mond-Orbit -- Radius friert beim Loslassen ein, erneutes Greifen möglich", "[grid]")
{
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });
    model.onDown (2, { 50.0f, 0.0f }); // Ring, Radius 50
    model.onMove (2, { 80.0f, 0.0f }); // Radius jetzt 80

    const auto up = model.onUp (2);
    REQUIRE (up.wasRing);
    REQUIRE (up.ringOwner == 1);

    // Radius bleibt eingefroren (kein Reset auf minRadiusPx), Mond bleibt
    // sichtbar an der zuletzt bekannten Ring-Position stehen
    const auto circles = model.activeCircles();
    REQUIRE (circles[0].radiusPx == Approx (80.0f));
    REQUIRE (circles[0].hasOrbit);
    REQUIRE (circles[0].orbitPos.x == Approx (80.0f));

    // Neuer Touch nahe dem primären Finger greift die Umlaufbahn erneut
    const auto regrab = model.onDown (3, { 60.0f, 0.0f });
    REQUIRE (regrab.kind == grid::RingTouchModel::TouchKind::Ring);
    REQUIRE (regrab.ringOwner == 1);
}

TEST_CASE ("RingTouchModel: Umlaufbahn lässt sich auch WEIT weg von der Sonne, nahe am Mond, erneut greifen", "[grid]")
{
    // Fund 06.07.2026 (dritte Runde): der Greif-Check darf nach dem
    // Loslassen nicht nur die Nähe zum Zentrum prüfen -- sonst erzeugt ein
    // Touch nahe dem (weit entfernten) Mond fälschlich einen NEUEN primären
    // Finger, statt die Umlaufbahn wieder aufzugreifen.
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });
    model.onDown (2, { 50.0f, 0.0f });
    model.onMove (2, { 200.0f, 0.0f }); // Mond weit weg, Radius 200
    model.onUp (2);

    // Touch nahe der Sonne (Zentrum) wäre außerhalb des grabRadius (90px)
    // zum Zentrum, aber nahe genug am eingefrorenen Mond bei x=200
    const auto regrab = model.onDown (3, { 210.0f, 0.0f });
    REQUIRE (regrab.kind == grid::RingTouchModel::TouchKind::Ring);
    REQUIRE (regrab.ringOwner == 1);
}

TEST_CASE ("RingTouchModel: Mond wandert mit der Sonne mit -- Radius bleibt konstant", "[grid]")
{
    // Fund 06.07.2026 (zweite Runde): der Mond darf NICHT an einer fixen
    // Bildschirmposition kleben bleiben, wenn die Sonne (primärer Finger)
    // sich bewegt -- er muss den relativen Offset zur Sonne beibehalten und
    // mitwandern. Nur ein aktiv bewegter Ring-Finger darf den Offset
    // (und damit Radius/Slide) verändern.
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });
    model.onDown (2, { 50.0f, 0.0f }); // Ring, Radius 50
    model.onMove (2, { 80.0f, 0.0f }); // Radius jetzt 80

    model.onUp (2); // Ring loslassen -- Offset (Radius 80) bleibt eingefroren

    // Primärer Finger bewegt sich, NACHDEM der Ring schon weg ist
    const auto primaryMove = model.onMove (1, { 10.0f, 0.0f });
    REQUIRE_FALSE (primaryMove.hasSlide);

    const auto circles = model.activeCircles();
    REQUIRE (circles[0].center.x == Approx (10.0f));
    // Radius bleibt EXAKT 80 -- ändert sich nicht durch die Sonnen-Bewegung
    REQUIRE (circles[0].radiusPx == Approx (80.0f));
    REQUIRE (circles[0].hasOrbit);
    // Mond wandert mit: center(10) + Offset(80) = 90
    REQUIRE (circles[0].orbitPos.x == Approx (90.0f));
}

TEST_CASE ("RingTouchModel: onUp deregistriert Ring bzw. entfernt den primären Finger", "[grid]")
{
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });
    model.onDown (2, { 50.0f, 0.0f });

    const auto upRing = model.onUp (2);
    REQUIRE (upRing.wasRing);
    REQUIRE_FALSE (upRing.wasPrimary);
    REQUIRE (upRing.ringOwner == 1);

    // Ring ist jetzt inaktiv -- eine weitere Bewegung liefert kein Slide mehr
    const auto moveAfterUp = model.onMove (2, { 100.0f, 0.0f });
    REQUIRE_FALSE (moveAfterUp.hasSlide);

    const auto upPrimary = model.onUp (1);
    REQUIRE (upPrimary.wasPrimary);
    REQUIRE_FALSE (upPrimary.wasRing);
    REQUIRE (upPrimary.primaryFinger == 1);
}

TEST_CASE ("RingTouchModel: Radius über/unter min-/maxRadiusPx liefert ungeklemmtes slide01 (M1b-6)", "[grid]")
{
    // slide01 klemmt seit M1b-6 nicht mehr selbst -- der Ausgang klemmt erst
    // in der slideAxis (GridVoiceEngine), damit ein negativer Slide-Offset
    // per Weiterwischen den vollen Bereich erreichen kann (Muster
    // expressionFromDrag).
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });
    model.onDown (2, { 50.0f, 0.0f }); // Ring von 1

    const auto beyondMax = model.onMove (2, { 300.0f, 0.0f }); // > maxRadiusPx (220)
    REQUIRE (beyondMax.hasSlide);
    REQUIRE (beyondMax.slide01 > 1.0f);

    const auto belowMin = model.onMove (2, { 10.0f, 0.0f }); // < minRadiusPx (40)
    REQUIRE (belowMin.hasSlide);
    REQUIRE (belowMin.slide01 < 0.0f);
}

TEST_CASE ("RingTouchModel: activeCircles spiegelt Radius wider", "[grid]")
{
    grid::RingTouchModel model;
    model.onDown (1, { 0.0f, 0.0f });

    auto circles = model.activeCircles();
    REQUIRE (circles.size() == 1);
    REQUIRE (circles[0].radiusPx == Approx (40.0f)); // minRadiusPx, kein Ring aktiv

    model.onDown (2, { 50.0f, 0.0f });
    model.onMove (2, { 100.0f, 0.0f });

    circles = model.activeCircles();
    REQUIRE (circles.size() == 1); // Ring-Finger bekommt keinen eigenen Kreis
    REQUIRE (circles[0].radiusPx == Approx (100.0f));
}
