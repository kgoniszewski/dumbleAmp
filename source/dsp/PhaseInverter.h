#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "TriodeModel.h"

namespace dumble
{
/**
    Long-tail-pair (Schmitt) phase inverter with a 12AT7:

        B+ --RpA-- plate A        B+ --RpB-- plate B
                 |                         |
        grid A ->|-- cathodes --+--|<- grid B (AC-grounded by its cap; NFB injected here)
                                Rbias
                                 +-- grid leak reference
                                Rtail
                                 |
                                gnd

    The quasi-static transfer from the differential grid voltage to the two plate swings is
    obtained by solving the full nonlinear DC circuit (3 unknowns, Newton) for a dense grid of
    input voltages in prepare(). At run time only a linear LUT interpolation is performed,
    which captures the pair's asymmetry (82k/100k plates), compression and grid conduction
    at a fixed, tiny cost. The LUT lives in a std::array, so prepare() does not allocate.
*/
class PhaseInverter
{
public:
    static constexpr int   kTableSize = 2049;
    static constexpr float kInputRange = 80.0f; // +/- volts at grid A

    struct Output { float a, b; };

    void prepare() noexcept
    {
        if (tableReady)
            return;

        // quiescent point: the grid-leak reference follows the tail junction (DC)
        State s { circuit::kPiBplus * 0.6f, circuit::kPiBplus * 0.6f, 20.0f };
        s = solve (0.0f, s, 400, -1.0f);
        const auto quiescent = s;

        // AC: both grids are held by their coupling/bypass caps at the quiescent grid voltage
        const auto gridRef = s.vk * circuit::kPiRTail / (circuit::kPiRBias + circuit::kPiRTail);

        // sweep upwards and downwards from 0 V, warm-starting from the neighbour
        const auto centre = kTableSize / 2;
        tableA[(size_t) centre] = 0.0f;
        tableB[(size_t) centre] = 0.0f;

        for (int dir : { 1, -1 })
        {
            auto st = quiescent;

            for (int i = centre + dir; i >= 0 && i < kTableSize; i += dir)
            {
                st = solve (indexToVolts (i), st, 60, gridRef);
                tableA[(size_t) i] = st.va - quiescent.va;
                tableB[(size_t) i] = st.vb - quiescent.vb;
            }
        }

        tableReady = true;
    }

    Output processSample (float gridDiff) const noexcept
    {
        const auto pos = juce::jlimit (0.0f, (float) (kTableSize - 1),
                                       (gridDiff + kInputRange) * (float) (kTableSize - 1) / (2.0f * kInputRange));
        const auto i0 = juce::jmin ((int) pos, kTableSize - 2);
        const auto frac = pos - (float) i0;

        return { tableA[(size_t) i0] + frac * (tableA[(size_t) i0 + 1] - tableA[(size_t) i0]),
                 tableB[(size_t) i0] + frac * (tableB[(size_t) i0 + 1] - tableB[(size_t) i0]) };
    }

private:
    struct State { float va, vb, vk; };

    static float indexToVolts (int i) noexcept
    {
        return -kInputRange + 2.0f * kInputRange * (float) i / (float) (kTableSize - 1);
    }

    /** gridRef < 0: DC solve (grids follow the tail junction); otherwise grids sit at gridRef (+ vin on A). */
    static void residual (float vin, const State& s, float f[3], float gridRef) noexcept
    {
        using namespace circuit;
        const auto vt = gridRef < 0.0f ? s.vk * kPiRTail / (kPiRBias + kPiRTail) : gridRef;

        // grid A driven from the previous stage (~40k source) -> soft grid conduction clamp
        constexpr float kSource = 40.0e3f;
        auto vgA = vt + vin;

        if (vgA > s.vk)
        {
            for (int i = 0; i < 8; ++i)
            {
                float d = 0.0f;
                const auto ig = gridCurrent (vgA - s.vk, d);
                vgA -= (vgA + kSource * ig - (vt + vin)) / (1.0f + kSource * d);
            }
        }

        const auto ia = korenTriode (k12AT7, vgA - s.vk, s.va - s.vk).ip;
        const auto ib = korenTriode (k12AT7, vt - s.vk,  s.vb - s.vk).ip;

        f[0] = (kPiBplus - s.va) / kPiRPlateA - ia;
        f[1] = (kPiBplus - s.vb) / kPiRPlateB - ib;
        f[2] = s.vk / (kPiRBias + kPiRTail) - (ia + ib);
    }

    static State solve (float vin, State s, int iterations, float gridRef) noexcept
    {
        for (int it = 0; it < iterations; ++it)
        {
            float f0[3];
            residual (vin, s, f0, gridRef);

            // numeric Jacobian (offline only)
            float j[3][3];
            for (int c = 0; c < 3; ++c)
            {
                auto sp = s;
                auto* v = c == 0 ? &sp.va : (c == 1 ? &sp.vb : &sp.vk);
                const auto h = 1.0e-3f * juce::jmax (1.0f, std::abs (*v));
                *v += h;
                float f1[3];
                residual (vin, sp, f1, gridRef);
                for (int r = 0; r < 3; ++r)
                    j[r][c] = (f1[r] - f0[r]) / h;
            }

            float dx[3];
            if (! solve3x3 (j, f0, dx))
                break;

            s.va = juce::jlimit (0.0f, circuit::kPiBplus, s.va - 0.7f * dx[0]);
            s.vb = juce::jlimit (0.0f, circuit::kPiBplus, s.vb - 0.7f * dx[1]);
            s.vk = juce::jlimit (0.0f, circuit::kPiBplus, s.vk - 0.7f * dx[2]);
        }

        return s;
    }

    static bool solve3x3 (const float m[3][3], const float b[3], float x[3]) noexcept
    {
        const double a = m[0][0], bb = m[0][1], c = m[0][2];
        const double d = m[1][0], e = m[1][1],  f = m[1][2];
        const double g = m[2][0], h = m[2][1],  k = m[2][2];
        const double det = a * (e * k - f * h) - bb * (d * k - f * g) + c * (d * h - e * g);

        if (std::abs (det) < 1.0e-30)
            return false;

        const double inv[3][3] = {
            { (e * k - f * h) / det, (c * h - bb * k) / det, (bb * f - c * e) / det },
            { (f * g - d * k) / det, (a * k - c * g) / det,  (c * d - a * f) / det },
            { (d * h - e * g) / det, (bb * g - a * h) / det, (a * e - bb * d) / det } };

        for (int r = 0; r < 3; ++r)
            x[r] = (float) (inv[r][0] * b[0] + inv[r][1] * b[1] + inv[r][2] * b[2]);

        return true;
    }

    std::array<float, kTableSize> tableA {}, tableB {};
    bool tableReady = false;
};
} // namespace dumble
