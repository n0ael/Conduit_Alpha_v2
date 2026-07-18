#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/CanvasGestureRecognizer.h"
#include "Core/CanvasViewport.h"

using Catch::Approx;
using conduit::canvas_view::ViewState;
namespace cv = conduit::canvas_view;

//==============================================================================
TEST_CASE ("CanvasViewport: toContent/toScreen sind invers", "[canvas][viewport]")
{
    const ViewState view { 120.0, -40.0, 0.75 };
    const juce::Point<double> screenPoint { 333.0, 217.0 };

    const auto content = cv::toContent (view, screenPoint);
    const auto back = cv::toScreen (view, content);

    CHECK (back.x == Approx (screenPoint.x));
    CHECK (back.y == Approx (screenPoint.y));

    // Identität lässt Punkte unverändert (Default-Zustand der Canvas)
    const auto identity = cv::toContent (ViewState{}, screenPoint);
    CHECK (identity.x == Approx (screenPoint.x));
    CHECK (identity.y == Approx (screenPoint.y));
}

TEST_CASE ("CanvasViewport: zoomAbout hält den Anker-Punkt fix", "[canvas][viewport]")
{
    const ViewState view { 50.0, 30.0, 1.0 };
    const juce::Point<double> anchor { 400.0, 300.0 };
    const auto anchorInContent = cv::toContent (view, anchor);

    const auto zoomed = cv::zoomAbout (view, anchor, 1.6);

    CHECK (zoomed.zoom == Approx (1.6));

    // Der Content-Punkt unter dem Anker bleibt derselbe
    const auto after = cv::toContent (zoomed, anchor);
    CHECK (after.x == Approx (anchorInContent.x));
    CHECK (after.y == Approx (anchorInContent.y));
}

TEST_CASE ("CanvasViewport: Zoom-Clamps 0.1–2.0", "[canvas][viewport]")
{
    CHECK (cv::clampZoom (0.01) == Approx (cv::minZoom));
    CHECK (cv::clampZoom (55.0) == Approx (cv::maxZoom));
    CHECK (cv::clampZoom (1.0)  == Approx (1.0));

    const auto clamped = cv::zoomAbout (ViewState{}, { 0.0, 0.0 }, 99.0);
    CHECK (clamped.zoom == Approx (cv::maxZoom));
}

TEST_CASE ("CanvasViewport: applyPinch — Zoom um Anker plus Pan-Delta", "[canvas][viewport]")
{
    const ViewState view { 0.0, 0.0, 1.0 };

    // Reines Pan (scaleFactor 1.0)
    const auto panned = cv::applyPinch (view, 1.0, { 25.0, -10.0 }, { 100.0, 100.0 });
    CHECK (panned.zoom == Approx (1.0));
    CHECK (panned.offsetX == Approx (25.0));
    CHECK (panned.offsetY == Approx (-10.0));

    // Zoom + Pan kombiniert: Anker bleibt bis auf das Pan-Delta fix
    const juce::Point<double> anchor { 200.0, 150.0 };
    const auto anchorInContent = cv::toContent (view, anchor);

    const auto pinched = cv::applyPinch (view, 1.25, { 10.0, 5.0 }, anchor);
    const auto anchorAfter = cv::toScreen (pinched, anchorInContent);

    CHECK (anchorAfter.x == Approx (anchor.x + 10.0));
    CHECK (anchorAfter.y == Approx (anchor.y + 5.0));
}

//==============================================================================
namespace
{

/** Protokolliert die Recognizer-Callbacks für Assertions. */
struct RecognizerLog
{
    explicit RecognizerLog (conduit::CanvasGestureRecognizer& recognizer)
    {
        recognizer.onPinchPan = [this] (double scale, juce::Point<double> pan,
                                        juce::Point<double>)
        {
            ++pinchCount;
            lastScale = scale;
            lastPan = pan;
        };
        recognizer.onGestureEnd = [this] { ++gestureEnds; };
        recognizer.onLevelBegin = [this] (int fingers) { levelBegins.push_back (fingers); };
        recognizer.onLevelEnd   = [this] (int fingers) { levelEnds.push_back (fingers); };
    }

    int pinchCount = 0, gestureEnds = 0;
    double lastScale = 0.0;
    juce::Point<double> lastPan;
    std::vector<int> levelBegins, levelEnds;
};

} // namespace

TEST_CASE ("CanvasGestureRecognizer: 2-Finger-Pinch liefert Scale und Pan", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;
    RecognizerLog log { recognizer };

    // Zwei Finger 100 px auseinander
    recognizer.touchDown (0, { 100.0f, 200.0f });
    recognizer.touchDown (1, { 200.0f, 200.0f });
    REQUIRE (recognizer.getActiveFingerCount() == 2);
    CHECK (log.pinchCount == 0);   // Referenz steht, noch keine Bewegung

    // Spreizen auf 200 px (Zentroid unverändert) → Scale 2.0, kein Pan
    recognizer.touchMove (0, { 50.0f, 200.0f });

    REQUIRE (log.pinchCount == 1);
    CHECK (log.lastScale == Approx (1.5));   // Spread 50→75 (ein Finger bewegt)
    recognizer.touchMove (1, { 250.0f, 200.0f });
    CHECK (log.lastScale == Approx (100.0 / 75.0));

    // Beide Finger parallel verschieben → reines Pan
    recognizer.touchMove (0, { 60.0f, 210.0f });
    recognizer.touchMove (1, { 260.0f, 210.0f });
    CHECK (log.lastPan.y == Approx (5.0));   // Zentroid je Move-Hälfte

    // Finger hoch → Gesten-Ende genau einmal
    recognizer.touchUp (0);
    CHECK (log.gestureEnds == 1);
    recognizer.touchUp (1);
    CHECK (log.gestureEnds == 1);   // Ebene 2 war schon beendet
}

TEST_CASE ("CanvasGestureRecognizer: Ebenen-Übergänge 2→3→2 ohne Sprünge", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;
    RecognizerLog log { recognizer };

    recognizer.touchDown (0, { 100.0f, 100.0f });
    recognizer.touchDown (1, { 200.0f, 100.0f });
    recognizer.touchMove (0, { 90.0f, 100.0f });
    REQUIRE (log.pinchCount == 1);

    // Dritter Finger: Ebene 2 endet (Persistenz), Ebene 3 beginnt
    recognizer.touchDown (2, { 150.0f, 200.0f });
    CHECK (log.gestureEnds == 1);
    REQUIRE (log.levelBegins == std::vector<int> { 3 });

    // Bewegungen in Ebene 3 erzeugen KEINE Pinch-Callbacks (M3a: no-op)
    recognizer.touchMove (2, { 150.0f, 260.0f });
    CHECK (log.pinchCount == 1);

    // Finger 2 hoch: Ebene 3 endet, Ebene 2 startet mit FRISCHER Referenz —
    // der nächste Move erzeugt kein Springen aus alten Referenzwerten
    recognizer.touchUp (2);
    REQUIRE (log.levelEnds == std::vector<int> { 3 });

    recognizer.touchMove (0, { 90.0f, 100.0f });   // Position unverändert
    CHECK (log.pinchCount == 2);
    CHECK (log.lastScale == Approx (1.0));
    CHECK (log.lastPan.x == Approx (0.0));
    CHECK (log.lastPan.y == Approx (0.0));
}

TEST_CASE ("CanvasGestureRecognizer: Leerraum-Regel — fremde Finger zählen nicht", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;
    RecognizerLog log { recognizer };

    // Nur EIN Finger begann auf dem Canvas (der zweite gehört einem Modul
    // und erreicht den Recognizer nie — ADR-008-Leerraum-Regel)
    recognizer.touchDown (0, { 100.0f, 100.0f });
    REQUIRE (recognizer.getActiveFingerCount() == 1);

    // Moves des Modul-Fingers (Index 1) sind dem Recognizer unbekannt
    recognizer.touchMove (1, { 300.0f, 300.0f });
    CHECK (log.pinchCount == 0);

    // Ein-Finger-Moves erzeugen ebenfalls keinen Pinch
    recognizer.touchMove (0, { 140.0f, 100.0f });
    CHECK (log.pinchCount == 0);

    recognizer.touchUp (1);   // no-op
    recognizer.touchUp (0);
    CHECK (recognizer.getActiveFingerCount() == 0);
    CHECK (log.gestureEnds == 0);   // Ebene 2 wurde nie erreicht
}

TEST_CASE ("CanvasViewport: softZoomResponse — Dead-Zone exakt, weiches Einblenden", "[canvas][viewport]")
{
    // Innerhalb der Dead-Zone: exakt 0 (Pan ändert den Zoom NICHT)
    CHECK (juce::exactlyEqual (cv::softZoomResponse (0.0), 0.0));
    CHECK (juce::exactlyEqual (cv::softZoomResponse (0.05), 0.0));
    CHECK (juce::exactlyEqual (cv::softZoomResponse (-0.05), 0.0));

    // Übergangsbereich: gedämpft, aber nicht null — und monoton
    const auto a = cv::softZoomResponse (0.09);
    const auto b = cv::softZoomResponse (0.13);
    CHECK (a > 0.0);
    CHECK (a < 0.09);
    CHECK (b > a);

    // Oberhalb fullResponse: volle Durchleitung, Vorzeichen erhalten
    CHECK (cv::softZoomResponse (0.5) == Approx (0.5));
    CHECK (cv::softZoomResponse (-0.5) == Approx (-0.5));
}

TEST_CASE ("CanvasGestureRecognizer: Pan-Wackeln lässt den Zoom exakt stehen", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;
    RecognizerLog log { recognizer };

    recognizer.touchDown (0, { 100.0f, 200.0f });
    recognizer.touchDown (1, { 200.0f, 200.0f });   // Spread 50

    // Leichtes Finger-Wackeln beim Pannen (Spread 50 → 49, ~2 % — tief in
    // der Dead-Zone): scaleFactor ist EXAKT 1.0, Pan kommt durch
    recognizer.touchMove (0, { 102.0f, 210.0f });
    REQUIRE (log.pinchCount == 1);
    CHECK (juce::exactlyEqual (log.lastScale, 1.0));
    CHECK (log.lastPan.y == Approx (5.0));

    recognizer.touchMove (1, { 202.0f, 210.0f });
    CHECK (juce::exactlyEqual (log.lastScale, 1.0));

    // Echtes Spreizen weit über die Dead-Zone hinaus: beide Richtungen
    // laufen voll durch — Spread 50 → 37.5 (eff −ln(4/3)) → 75 (eff +ln(1.5));
    // der zweite Move meldet exp(ln(1.5) − (−ln(4/3))) = 2.0
    recognizer.touchMove (0, { 127.0f, 210.0f });   // Positionen 127/202 → Spread 37.5
    CHECK (log.lastScale == Approx (0.75));

    recognizer.touchMove (0, { 52.0f, 210.0f });    // Positionen 52/202 → Spread 75
    CHECK (log.lastScale == Approx (2.0));
}

TEST_CASE ("CanvasGestureRecognizer: Pinch-Schwelle ist einstellbar (Dev-Tuning)", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;
    RecognizerLog log { recognizer };

    // Sehr träge Schwelle (ungenauer Touchscreen): auch kräftiges Spreizen
    // (Spread 50 → 70, ln 1.4 ≈ 0.34) bleibt reines Pan
    recognizer.setZoomDeadZone (0.5);
    recognizer.touchDown (0, { 100.0f, 200.0f });
    recognizer.touchDown (1, { 200.0f, 200.0f });
    recognizer.touchMove (0, { 60.0f, 200.0f });
    REQUIRE (log.pinchCount == 1);
    CHECK (juce::exactlyEqual (log.lastScale, 1.0));

    recognizer.touchUp (0);
    recognizer.touchUp (1);

    // Schwelle 0: jede Spread-Änderung zoomt sofort und voll
    recognizer.setZoomDeadZone (0.0);
    recognizer.touchDown (0, { 100.0f, 200.0f });
    recognizer.touchDown (1, { 200.0f, 200.0f });
    recognizer.touchMove (0, { 50.0f, 200.0f });   // Spread 50 → 75
    CHECK (log.lastScale == Approx (1.5));
}

TEST_CASE ("CanvasViewport: progressiveZoomResponse — langsamer Start, progressiv stärker", "[canvas][viewport]")
{
    // Neutral (1.0, 1.0) = Identität
    CHECK (cv::progressiveZoomResponse (0.4, 1.0, 1.0) == Approx (0.4));
    CHECK (cv::progressiveZoomResponse (-0.4, 1.0, 1.0) == Approx (-0.4));
    CHECK (juce::exactlyEqual (cv::progressiveZoomResponse (0.0, 0.6, 1.6), 0.0));

    // Gain senkt die Gesamt-Antwort linear
    CHECK (cv::progressiveZoomResponse (0.4, 0.5, 1.0) == Approx (0.2));

    // Exponent > 1: kleine Auslenkungen überproportional gedämpft — die
    // RELATIVE Dämpfung nimmt mit der Auslenkung ab (progressiv stärker)
    const auto small = cv::progressiveZoomResponse (0.1, 1.0, 2.0);
    const auto large = cv::progressiveZoomResponse (0.5, 1.0, 2.0);
    CHECK (small == Approx (0.01));
    CHECK (large == Approx (0.25));
    CHECK (small / 0.1 < large / 0.5);   // Antwortrate wächst mit dem Weg

    // Vorzeichen bleibt erhalten
    CHECK (cv::progressiveZoomResponse (-0.5, 1.0, 2.0) == Approx (-0.25));
}

TEST_CASE ("CanvasGestureRecognizer: Zoom-Antwort (Stärke/Kurve) ist einstellbar", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;
    RecognizerLog log { recognizer };

    // Dead-Zone aus, Gain 0.5, quadratische Kurve — exakter Erwartungswert:
    // Spread 50 → 75, raw = ln 1.5; eff = 0.5·raw² → scale = exp(eff)
    recognizer.setZoomDeadZone (0.0);
    recognizer.setZoomResponse (0.5, 2.0);

    recognizer.touchDown (0, { 100.0f, 200.0f });
    recognizer.touchDown (1, { 200.0f, 200.0f });
    recognizer.touchMove (0, { 50.0f, 200.0f });

    const auto raw = std::log (1.5);
    CHECK (log.lastScale == Approx (std::exp (0.5 * raw * raw)));
    CHECK (log.lastScale < 1.5);   // deutlich träger als linear
}

TEST_CASE ("CanvasGestureRecognizer: Glättung dämpft Sensor-Rauschen (EMA)", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;
    RecognizerLog log { recognizer };

    recognizer.setSmoothing (0.5);
    recognizer.touchDown (0, { 100.0f, 200.0f });
    recognizer.touchDown (1, { 200.0f, 200.0f });

    // Roher Zentroid-Sprung +10 px → EMA (0.5) liefert nur die Hälfte
    recognizer.touchMove (0, { 110.0f, 200.0f });
    recognizer.touchMove (1, { 210.0f, 200.0f });

    // Move 1: roh +5 (ein Finger) → gemeldet 2.5; Move 2: roh 155−152.5 …
    // entscheidend: die SUMME der gemeldeten Deltas bleibt hinter dem
    // rohen Gesamtweg (10 px) zurück und jedes Einzeldelta ist gedämpft
    CHECK (log.lastPan.x > 0.0);
    CHECK (log.lastPan.x < 5.0);

    // Glättung 0 = rohe Durchreichung (Bestandsverhalten)
    conduit::CanvasGestureRecognizer rawRecognizer;
    RecognizerLog rawLog { rawRecognizer };
    rawRecognizer.setSmoothing (0.0);
    rawRecognizer.touchDown (0, { 100.0f, 200.0f });
    rawRecognizer.touchDown (1, { 200.0f, 200.0f });
    rawRecognizer.touchMove (0, { 110.0f, 200.0f });
    CHECK (rawLog.lastPan.x == Approx (5.0));
}

TEST_CASE ("CanvasGestureRecognizer: 4/5-Finger-Ebenen werden erkannt", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;
    RecognizerLog log { recognizer };

    for (int i = 0; i < 5; ++i)
        recognizer.touchDown (i, { 100.0f + 30.0f * (float) i, 100.0f });

    REQUIRE (log.levelBegins == std::vector<int> { 3, 4, 5 });

    recognizer.reset();
    CHECK (recognizer.getActiveFingerCount() == 0);
    REQUIRE (log.levelEnds.back() == 5);
}

TEST_CASE ("CanvasGestureRecognizer: 4-Finger-Drag meldet Zentroid-Deltas (M3b)", "[canvas][gesture]")
{
    conduit::CanvasGestureRecognizer recognizer;

    int dragCount = 0, dragFingers = 0;
    juce::Point<double> totalDelta;
    recognizer.onLevelDrag = [&] (int fingers, juce::Point<double> delta)
    {
        ++dragCount;
        dragFingers = fingers;
        totalDelta += delta;
    };

    for (int i = 0; i < 4; ++i)
        recognizer.touchDown (i, { 100.0f + 40.0f * (float) i, 200.0f });

    // Alle vier Finger 60 px nach links → Gesamtdelta −60 (Zentroid folgt)
    for (int i = 0; i < 4; ++i)
        recognizer.touchMove (i, { 40.0f + 40.0f * (float) i, 200.0f });

    CHECK (dragCount == 4);
    CHECK (dragFingers == 4);
    CHECK (totalDelta.x == Approx (-60.0));
    CHECK (totalDelta.y == Approx (0.0));
}
