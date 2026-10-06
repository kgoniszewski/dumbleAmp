#pragma once

#include <cmath>

#include <juce_core/juce_core.h>

#include "Ac30Constants.h"
#include "dsp/OnePole.h"
#include "dsp/PentodeTable.h"

namespace ac30
{
inline const dumble::PentodeTable& el84Table()
{
    static const dumble::PentodeTable table (kEL84);
    return table;
}

/**
    AC30C2 output stage: 4 x EL84 push-pull (two in parallel per phase), cathode bias on the shared
    R119 50R || C74 220u, screens from B+2 through 470R each, no global feedback.

    Per sample: screens by one fixed-point step, the two plate voltages jointly through the
    centre-tapped primary (Va + Vb = 2 B+1, ideal OT with Raa = 4k, scalar Newton), then the shared
    cathode node by its trapezoidal companion model with the new total cathode current (semi-
    implicit: the cathode's admittance at the oversampled rate is >80 S, so the one-sample lag is
    negligible). The cathode node is what makes the AC30 bias shift and compress when driven hard.

    Input: absolute grid voltages (after the stoppers), supplies. Output: speaker volts (16R).
*/
class PowerAmpEL84
{
public:
    static constexpr int   kMaxPlateIterations = 4;
    static constexpr float kPlateTolerance = 1.0e-3f;

    void prepare (double sampleRate) noexcept
    {
        using namespace circuit;
        table = &el84Table();
        gCathode = 2.0 * kC74 * sampleRate;
        turns = std::sqrt (kPrimaryLoadPerSide / kSpeakerLoad);
        otLow.setCutoff (kOtLowCutHz, sampleRate);
        otHigh.setCutoff (kOtHighCutHz, sampleRate);
        otFlux.setCutoff (kOtLowCutHz, sampleRate);
    }

    /** Idle operating point for the given supplies (not real-time). */
    void solveOperatingPoint (double bPlus1, double bPlus2) noexcept
    {
        auto cathode = 10.0;
        for (int it = 0; it < 400; ++it)
        {
            const auto g1 = (float) -cathode;
            auto vs = (float) bPlus2;
            for (int k = 0; k < 20; ++k)
                vs = (float) bPlus2 - (float) circuit::kScreenR * dumble::korenScreenCurrent (kEL84, g1, vs - (float) cathode);
            const auto ia = dumble::korenPentode (kEL84, g1, vs - (float) cathode, (float) (bPlus1 - cathode));
            const auto ig2 = dumble::korenScreenCurrent (kEL84, g1, vs - (float) cathode);
            idlePlate = ia;
            idleScreen = ig2;
            screenDC = vs;
            cathode += 0.2 * (4.0 * (ia + ig2) * circuit::kR119 - cathode);
        }

        cathodeDC = cathode;
        plateSupplyDC = bPlus1;
    }

    void reset() noexcept
    {
        for (auto* f : { &otLow, &otHigh, &otFlux })
            f->reset();
        vk = cathodeDC;
        cathodeHistory = gCathode * vk; // capacitor charged, no current
        screenP = screenM = screenDC;
        plateA = plateAPrev = plateSupplyDC;
        plateCurrent = 4.0 * idlePlate;
        screenCurrent = 4.0 * idleScreen;
    }

    /** Grid voltages (absolute), supplies, grid currents of both phases. Returns speaker volts. */
    float processSample (double gridP, double gridM, double bPlus1, double bPlus2, double gridCurrents) noexcept
    {
        using namespace circuit;
        const auto& t = *table;
        const auto k = (float) vk;
        const auto g1P = (float) gridP - k, g1M = (float) gridM - k;

        // screens: one fixed-point step from the previous value (2 tubes per phase, 470R each)
        const auto ig2P = t.screenCurrent (g1P, screenP - k);
        const auto ig2M = t.screenCurrent (g1M, screenM - k);
        screenP = (float) bPlus2 - (float) kScreenR * ig2P;
        screenM = (float) bPlus2 - (float) kScreenR * ig2M;

        const auto kP = 2.0f * t.gridTerm (g1P, screenP - k);
        const auto kM = 2.0f * t.gridTerm (g1M, screenM - k);

        // joint plate solve through the centre-tapped primary
        const auto supply = (float) bPlus1;
        const auto rl = (float) kPrimaryLoadPerSide;
        auto va = juce::jlimit (k, 2.0f * supply - k, 2.0f * plateA - plateAPrev);
        float aP = 0.0f, aM = 0.0f, dP = 0.0f, dM = 0.0f;

        for (int i = 0; i < kMaxPlateIterations; ++i)
        {
            aP = t.plateFactor (va - k, dP);
            aM = t.plateFactor (2.0f * supply - va - k, dM);
            const auto f = va - supply + (kP * aP - kM * aM) * rl;
            const auto df = 1.0f + rl * (kP * dP + kM * dM);
            const auto vaNew = juce::jlimit (k, 2.0f * supply - k, va - f / df);
            const auto step = va - vaNew;
            va = vaNew;
            if (std::abs (step) < kPlateTolerance)
                break;
        }

        aP = t.plateFactor (va - k, dP);
        aM = t.plateFactor (2.0f * supply - va - k, dM);
        plateAPrev = plateA;
        plateA = va;

        const auto iaP = kP * aP, iaM = kM * aM;
        plateCurrent = (double) iaP + iaM;
        screenCurrent = 2.0 * ((double) ig2P + ig2M);

        // shared cathode: (gC + 1/Rk) Vk = Ik + history
        const auto ik = plateCurrent + screenCurrent + gridCurrents;
        const auto vkNew = (ik + cathodeHistory) / (gCathode + 1.0 / kR119);
        const auto capCurrent = gCathode * vkNew - cathodeHistory;
        cathodeHistory = gCathode * vkNew + capCurrent;
        vk = vkNew;

        // ideal OT: Vsec = (Ia - Ib) Rl / n, then primary-inductance low cut, leakage high cut, core
        auto vs = (iaP - iaM) * rl / turns;
        vs = otHigh.processLowpass (otLow.processHighpass (vs));
        const auto flux = otFlux.processLowpass (vs) / kSpeakerFullScale * 4.0f;
        return vs / (1.0f + kOtCoreSaturation * flux * flux);
    }

    double getCathodeVoltage() const noexcept    { return vk; }
    double getCathodeVoltageDC() const noexcept  { return cathodeDC; }
    double getIdlePlateCurrent() const noexcept  { return idlePlate; }  // per tube
    double getIdleScreenCurrent() const noexcept { return idleScreen; } // per tube
    double getPlateCurrent() const noexcept      { return plateCurrent; }  // all four, from B+1
    double getScreenCurrent() const noexcept     { return screenCurrent; } // all four, from B+2

private:
    const dumble::PentodeTable* table = nullptr;
    dumble::OnePole otLow, otHigh, otFlux;
    double gCathode = 1.0, cathodeHistory = 0.0, vk = 10.0, cathodeDC = 10.0;
    double idlePlate = 0.045, idleScreen = 0.005, plateSupplyDC = 340.0;
    double plateCurrent = 0.18, screenCurrent = 0.02;
    float screenDC = 320.0f, screenP = 320.0f, screenM = 320.0f;
    float plateA = 340.0f, plateAPrev = 340.0f, turns = 7.9f;
};
} // namespace ac30
