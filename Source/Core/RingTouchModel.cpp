#include "RingTouchModel.h"

namespace conduit::grid
{

RingTouchModel::RingTouchModel() noexcept : RingTouchModel (Config{})
{
}

RingTouchModel::RingTouchModel (const Config& cfg) noexcept : config (cfg)
{
}

RingTouchModel::DownResult RingTouchModel::onDown (uint32_t fingerId, juce::Point<float> pos) noexcept
{
    PrimaryFinger* nearest = nullptr;
    float nearestDist = 0.0f;

    for (auto& primary : primaries)
    {
        if (primary.ringFinger != 0)
            continue;

        // Greifpunkt: die aktuelle Mond-Position, falls schon einmal ein Ring
        // aktiv war -- die Umlaufbahn kann weit von der Sonne entfernt
        // eingefroren sein, und genau DORT muss man sie wieder aufgreifen
        // können (User-Fund 06.07.2026); ohne Orbit-Historie zählt weiterhin
        // die Nähe zum Zentrum (Erstgriff).
        const auto grabPoint = primary.hasOrbit ? (primary.center + primary.ringOffset)
                                                 : primary.center;
        const auto dist = pos.getDistanceFrom (grabPoint);

        if (dist <= config.grabRadiusPx && (nearest == nullptr || dist < nearestDist))
        {
            nearest = &primary;
            nearestDist = dist;
        }
    }

    if (nearest != nullptr)
    {
        nearest->ringFinger = fingerId;
        nearest->ringOffset = pos - nearest->center;
        nearest->hasOrbit = true;
        nearest->curRadiusPx = nearest->ringOffset.getDistanceFromOrigin();
        return { TouchKind::Ring, nearest->id };
    }

    primaries.push_back ({ fingerId, pos, 0, {}, false, config.minRadiusPx });
    return { TouchKind::Primary, 0 };
}

RingTouchModel::MoveResult RingTouchModel::onMove (uint32_t fingerId, juce::Point<float> pos) noexcept
{
    for (auto& primary : primaries)
    {
        if (primary.ringFinger == fingerId)
        {
            // Ring-Finger bewegt sich: Offset relativ zum AKTUELLEN Zentrum
            // neu erfassen, Radius = dessen Betrag.
            primary.ringOffset = pos - primary.center;
            const auto radius = primary.ringOffset.getDistanceFromOrigin();
            primary.curRadiusPx = radius;

            // NICHT geklemmt -- Werte über 1 / unter 0 durch Radius über
            // maxRadiusPx bzw. unter minRadiusPx hinaus sind gewollt (die
            // slideAxis in GridVoiceEngine klemmt erst am Ausgang).
            const auto range = config.maxRadiusPx - config.minRadiusPx;
            const auto slide  = range > 0.0f ? (radius - config.minRadiusPx) / range : 0.0f;

            return { true, primary.id, slide };
        }

        if (primary.id == fingerId)
        {
            // Primärer Finger bewegt sich: Zentrum folgt. Der Ring-Offset
            // (und damit Radius/Slide) bleibt UNVERÄNDERT -- der Mond klebt
            // relativ zur Sonne und wandert mit ihr mit (User 06.07.2026:
            // Fund -- der Mond blieb an einer FIXEN Bildschirmposition
            // stehen, statt der Sonne zu folgen, weil der Radius vorher aus
            // dem Live-Abstand zu einer absoluten Ring-Position berechnet
            // wurde; jetzt ist der Offset relativ, ändert sich also nur,
            // wenn der Ring-Finger selbst aktiv bewegt wird).
            primary.center = pos;
            return {};
        }
    }

    return {};
}

RingTouchModel::UpResult RingTouchModel::onUp (uint32_t fingerId) noexcept
{
    for (auto& primary : primaries)
    {
        if (primary.ringFinger == fingerId)
        {
            // Mond-Orbit (User-Entscheidung 06.07.2026): curRadiusPx bleibt
            // auf dem zuletzt gemessenen Wert eingefroren, KEIN Reset auf
            // minRadiusPx -- der Kreis bleibt sichtbar an Ort und Abstand
            // stehen, bis ein neuer Touch im grabRadius (ringFinger == 0)
            // die Umlaufbahn erneut aufgreift.
            const auto owner = primary.id;
            primary.ringFinger = 0;
            return { false, true, 0, owner, false, {}, {}, false };
        }
    }

    for (std::size_t i = 0; i < primaries.size(); ++i)
    {
        if (primaries[i].id == fingerId)
        {
            // Block M (Hold/Drone): letzte Geometrie + Mond-Status
            // mitliefern — der Besitzer entscheidet daraus über den
            // Drone-Start (Sonne zuerst losgelassen, Mond liegt noch).
            UpResult result { true, false, fingerId, 0,
                              primaries[i].ringFinger != 0,
                              primaries[i].center, primaries[i].ringOffset,
                              primaries[i].hasOrbit };

            primaries.erase (primaries.begin() + (std::ptrdiff_t) i);
            return result;
        }
    }

    return {};
}

std::vector<RingTouchModel::Circle> RingTouchModel::activeCircles() const
{
    std::vector<Circle> circles;
    circles.reserve (primaries.size());

    for (const auto& primary : primaries)
        circles.push_back ({ primary.center, primary.curRadiusPx, primary.hasOrbit,
                              primary.center + primary.ringOffset });

    return circles;
}

void RingTouchModel::reset() noexcept
{
    primaries.clear();
}

void RingTouchModel::setRadiusRange (float newMinPx, float newMaxPx) noexcept
{
    config.minRadiusPx = juce::jmax (0.0f, newMinPx);
    config.maxRadiusPx = juce::jmax (config.minRadiusPx + 1.0f, newMaxPx);
}

} // namespace conduit::grid
