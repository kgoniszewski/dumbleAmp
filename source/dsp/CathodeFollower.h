#pragma once

#include <juce_core/juce_core.h>

#include "KorenTable.h"
#include "TriodeModel.h"

namespace dumble
{
/**
    Cathode follower (plate at a fixed supply), as used three times in the SSS #002:
    U38 / U40 (dry and reverb-return buffers into the mixer) and U12 / U13 (DC-coupled driver
    of the 6L6GC grids).

        B+ -- plate
        grid <-- Rsrc <-- input (absolute volts; grid conduction solved through Rsrc)
        cathode -- Rload -- Vneg      (Vneg = 0 V for the preamp buffers, the bias supply for the driver)
                -- Rshunt -- gnd      (optional)
                -- grid load          (optional: power-tube grids through their stoppers, conducting above ~0.36 V)

    The cathode voltage is the single unknown: scalar Newton, warm start, iteration cap.
*/
class CathodeFollower
{
public:
    struct Values
    {
        float bPlus;          // plate supply [V]
        float rLoad;          // cathode resistor [ohm]
        float vNeg;           // voltage Rload returns to [V]
        float rShunt = 0.0f;  // optional extra resistor cathode -> ground (0 = none)
        float rSource = 0.0f; // source impedance seen by the grid (for grid conduction), 0 = ideal
        float rGridLoad = 0.0f; // optional conducting grid load (stopper + RGI), 0 = none
    };

    static constexpr int   kMaxIterations = 4;
    static constexpr float kMaxStep = 5.0f;         // volts per Newton step
    static constexpr float kPredictorGain = 0.98f;

    void setCircuit (const Values& values, const KorenTriodeParams& tube) noexcept
    {
        v = values;
        tubeParams = tube;
        table = KorenTable::forTube (tube);
    }

    /** Solves the DC operating point for a given DC grid voltage. Not real-time (iterates to convergence). */
    void prepare (float gridDC) noexcept
    {
        vk = (double) gridDC + 1.5;
        for (int i = 0; i < 400; ++i)
            iterate (gridDC, 0.0, 0.5);
        vkDC = (float) vk;
        gridQuiescent = gridDC;
        lastGrid = gridQuiescent;
    }

    void reset() noexcept
    {
        vk = vkDC;
        lastGrid = gridQuiescent;
    }

    /** Absolute grid voltage in, absolute cathode voltage out. */
    float processSample (float gridVolts) noexcept
    {
        // predictor: a follower has a gain just below one, so start from the previous cathode
        // voltage moved by the grid change (keeps Newton away from cut-off on fast edges)
        vk += kPredictorGain * ((double) gridVolts - lastGrid);
        lastGrid = gridVolts;

        // 1) cathode with no grid current (the follower normally keeps Vgk negative)
        for (int i = 0; i < kMaxIterations; ++i)
            if (std::abs (iterate (gridVolts, 0.0, 1.0)) < 1.0e-4)
                break;

        // 2) grid conduction only if the *current* Vgk approaches the diode knee: solve the grid
        //    through the source impedance, then the cathode again with that grid current
        if (v.rSource > 0.0f && gridVolts - vk > 0.0)
        {
            double vg = gridVolts, ig = 0.0;
            for (int i = 0; i < 3; ++i)
            {
                float d = 0.0f;
                ig = gridCurrent ((float) (vg - vk), tubeParams.rgi, d);
                vg -= (vg + v.rSource * ig - gridVolts) / (1.0 + v.rSource * (double) d);
            }

            for (int i = 0; i < kMaxIterations; ++i)
                if (std::abs (iterate (vg, ig, 1.0)) < 1.0e-4)
                    break;
        }

        return (float) vk;
    }

    /** Cathode voltage relative to its quiescent value (full double precision before rounding). */
    float getCathodeDeviation() const noexcept { return (float) (vk - (double) vkDC); }

    float getCathodeVoltage() const noexcept   { return (float) vk; }
    float getCathodeVoltageDC() const noexcept { return vkDC; }

    /** Current into the conducting grid load (e.g. power-tube grid current) at the present cathode voltage. */
    float getGridLoadCurrent() const noexcept
    {
        if (v.rGridLoad <= 0.0f)
            return 0.0f;
        float unused = 0.0f;
        return gridCurrent ((float) vk, v.rGridLoad, unused);
    }

private:
    double iterate (double vg, double ig, double damping) noexcept
    {
        const auto vgk = (float) (vg - vk), vpk = (float) ((double) v.bPlus - vk);
        const auto t = table != nullptr ? table->evaluate (vgk, vpk) : korenTriode (tubeParams, vgk, vpk);

        float dLoad = 0.0f;
        const auto gridLoad = v.rGridLoad > 0.0f ? (double) gridCurrent ((float) vk, v.rGridLoad, dLoad) : 0.0;

        const auto f = t.ip + ig - (vk - v.vNeg) / v.rLoad - (v.rShunt > 0.0f ? vk / v.rShunt : 0.0) - gridLoad;
        const auto df = -((double) t.dIdVgk + t.dIdVpk) - 1.0 / v.rLoad - (v.rShunt > 0.0f ? 1.0 / v.rShunt : 0.0) - dLoad;

        const auto step = juce::jlimit (-(double) kMaxStep, (double) kMaxStep, damping * f / df);
        vk = juce::jlimit ((double) v.vNeg, (double) v.bPlus, vk - step);
        return step;
    }

    Values v { 300.0f, 100.0e3f, 0.0f };
    KorenTriodeParams tubeParams = k7025;
    const KorenTable* table = nullptr;
    double vk = 0.0, lastGrid = 0.0;
    float vkDC = 0.0f, gridQuiescent = 0.0f;
};
} // namespace dumble
