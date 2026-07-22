#pragma once

#include <cmath>

#include "TouchSample.h"

namespace touchlab
{

//==============================================================================
/**
    DER portable Baustein — reine Funktion Sample->Sample, allocation-frei,
    jede Stufe einzeln zu-/abschaltbar. Später verbatim nach Conduit
    Source/Core/ (spiegelt Conduits RT-Disziplin, macht die Portierung
    trivial). Eine Instanz je Quelle (One-Euro-State ist pro Strich).

    Stufen in Reihenfolge:
      1) Dead-Zone / Move-Threshold  (0-fähig — Leons Kernregler)
      2) One-Euro-Filter             (Casiez et al. — minCutoff + beta)
      3) Jitter-Gate                 (Geschwindigkeits-Deadband im Stillstand)
      4) Sensitivität                (Skalierung relativ zum Down-Anker)
*/
class TouchFilterChain
{
public:
    struct Params
    {
        bool  deadZoneEnabled  = false;
        float deadZoneRadiusPx = 0.0f;    // 0 = passthrough

        bool  oneEuroEnabled = true;
        float minCutoff = 1.0f;           // Hz — Grundglättung im Stillstand
        float beta      = 0.007f;         // Glättungs-Nachlass bei Bewegung
        float dCutoff   = 1.0f;           // Hz — Tiefpass der Ableitung

        bool  jitterGateEnabled     = false;
        float jitterGateSpeedPxPerSec = 8.0f;  // darunter: Ausgabe einfrieren

        bool  sensitivityEnabled = false;
        float sensitivity        = 1.0f;  // Fingerweg -> Wertänderung (1 = 1:1)
    };

    Params params;

    //==========================================================================
    TouchSample process (const TouchSample& in)
    {
        TouchSample out = in;

        if (in.phase == Phase::Down)
        {
            resetTo (in.x, in.y);
            prevT = in.tSeconds;
            return out;
        }

        double dt = in.tSeconds - prevT;
        prevT = in.tSeconds;
        if (dt <= 0.0)
            dt = 1.0 / 60.0;   // Schutz gegen 0/negativ (Timer-Rundung)

        float x = in.x;
        float y = in.y;

        // 1) Dead-Zone / Move-Threshold
        if (params.deadZoneEnabled && params.deadZoneRadiusPx > 0.0f)
        {
            const float dx = x - committedX;
            const float dy = y - committedY;
            if (std::sqrt (dx * dx + dy * dy) >= params.deadZoneRadiusPx)
            {
                committedX = x;
                committedY = y;
            }
            x = committedX;
            y = committedY;
        }
        else
        {
            committedX = x;
            committedY = y;
        }

        // 2) One-Euro
        if (params.oneEuroEnabled)
        {
            x = euroX.filter (x, dt, params.minCutoff, params.beta, params.dCutoff);
            y = euroY.filter (y, dt, params.minCutoff, params.beta, params.dCutoff);
        }

        // 3) Jitter-Gate (Geschwindigkeits-Deadband)
        if (params.jitterGateEnabled)
        {
            const float dx = x - prevOutX;
            const float dy = y - prevOutY;
            const double speed = std::sqrt (dx * dx + dy * dy) / dt;
            if (speed < params.jitterGateSpeedPxPerSec)
            {
                x = prevOutX;
                y = prevOutY;
            }
        }

        // 4) Sensitivität (relativ zum Down-Anker)
        if (params.sensitivityEnabled)
        {
            x = anchorX + (x - anchorX) * params.sensitivity;
            y = anchorY + (y - anchorY) * params.sensitivity;
        }

        prevOutX = x;
        prevOutY = y;
        out.x = x;
        out.y = y;
        return out;
    }

private:
    //==========================================================================
    struct OneEuro
    {
        bool  hasX = false, hasDx = false;
        float xPrev = 0.0f, xHat = 0.0f, dxHat = 0.0f;

        void reset() noexcept { hasX = hasDx = false; }

        static float alpha (double cutoff, double dt) noexcept
        {
            const double tau = 1.0 / (2.0 * juce::MathConstants<double>::pi * cutoff);
            return (float) (1.0 / (1.0 + tau / dt));
        }

        float filter (float value, double dt, float minCutoff, float beta, float dCutoff) noexcept
        {
            const float dx = hasX ? (float) ((value - xPrev) / dt) : 0.0f;
            const float aD = alpha (dCutoff, dt);
            dxHat = hasDx ? (aD * dx + (1.0f - aD) * dxHat) : dx;
            hasDx = true;

            const double cutoff = (double) minCutoff + (double) beta * std::abs (dxHat);
            const float a = alpha (cutoff, dt);
            xHat = hasX ? (a * value + (1.0f - a) * xHat) : value;
            hasX = true;
            xPrev = value;
            return xHat;
        }
    };

    void resetTo (float x, float y) noexcept
    {
        committedX = anchorX = prevOutX = x;
        committedY = anchorY = prevOutY = y;
        euroX.reset();
        euroY.reset();
    }

    OneEuro euroX, euroY;
    float committedX = 0.0f, committedY = 0.0f;
    float prevOutX = 0.0f,  prevOutY = 0.0f;
    float anchorX = 0.0f,   anchorY = 0.0f;
    double prevT = 0.0;
};

} // namespace touchlab
