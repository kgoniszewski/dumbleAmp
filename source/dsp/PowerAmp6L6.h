#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CathodeFollower.h"
#include "CircuitConstants.h"
#include "OnePole.h"
#include "TriodeModel.h"

namespace dumble
{
/**
    SSS #002 output section, per side:

        PI plate --(C14 68p between plates)--> C10 0.1u --> CF grid (1M to the bias divider, -43.8 V)
        CF U12/U13 (7025): plate HT2, cathode -> 220k to -320 V, 235k to ground,
                           -> 1.5k stoppers -> grids of 2x 6L6GC (DC coupled: no blocking distortion,
                              the grids can be driven positive into AB2, limited by the CF)
        6L6GC: cathodes grounded, screens from HT2 via 470R, plates into a 2 kOhm CT : 8 Ohm OT.

    Both plate voltages are solved jointly through the centre-tapped primary (Va + Vb = 2 Vs, scalar
    Newton). Supply sag from the reservoir (stiff solid-state supply in the SSS). The OT is modelled as
    an ideal transformer plus primary-inductance low cut, leakage high cut and mild LF core saturation.
    Input: PI plate swings (AC volts). Output: speaker voltage (volts).
*/
class PowerAmp6L6
{
public:
    static constexpr int   kMaxPlateIterations = 4;
    static constexpr float kPlateTolerance     = 1.0e-3f;

    void prepare (double sampleRate) noexcept
    {
        using namespace circuit;
        gridBiasDC = kBias * kDrvBiasBottom / (kDrvBiasBottom + kDrvBiasTop);
        turns = std::sqrt (kPrimaryLoadPerSide / kSpeakerLoad);

        const CathodeFollower::Values cf { kHT2, kDrvLoad, kBias, kDrvShunt, kPiSourceR,
                                           (kPowerStopper + k6L6GC.rgi) / (float) kPowerTubesPerSide };
        for (auto* d : { &driverA, &driverB })
        {
            d->setCircuit (cf, k7025);
            d->prepare (gridBiasDC);
        }

        for (auto* f : { &plateLpA, &plateLpB })
            f->setCutoff (26000.0f, sampleRate);
        for (auto* f : { &couplingA, &couplingB })
            f->setCutoff (1.0f / (juce::MathConstants<float>::twoPi * kDrvCoupling * kDrvGridLeak), sampleRate);

        sag.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * kSagTimeConstant), sampleRate);
        otLow.setCutoff (kOtLowCutHz, sampleRate);
        otHigh.setCutoff (kOtHighCutHz, sampleRate);
        otFlux.setCutoff (kOtLowCutHz, sampleRate);

        // idle operating point
        const auto vg1 = driverA.getCathodeVoltageDC();
        screenA = screenB = kHT2;
        for (int i = 0; i < 50; ++i)
            screenA = screenB = kHT2 - kScreenR * korenScreenCurrent (k6L6GC, vg1, screenA);

        idlePerTube = korenPentode (k6L6GC, vg1, screenA, kHT1);
        idleTotal = 2.0f * (float) kPowerTubesPerSide * idlePerTube;
        reset();
    }

    void reset() noexcept
    {
        for (auto* f : { &plateLpA, &plateLpB, &couplingA, &couplingB, &otLow, &otHigh, &otFlux })
            f->reset();
        driverA.reset();
        driverB.reset();
        sag.reset (idleTotal);
        supply = circuit::kHT1;
        plateA = circuit::kHT1;
    }

    /** PI plate swings in (volts AC), speaker voltage out. */
    float processSample (float piA, float piB) noexcept
    {
        using namespace circuit;

        // PI plates -> C14 roll-off -> coupling caps -> driver CF grids on the bias divider
        const auto gridA = gridBiasDC + couplingA.processHighpass (plateLpA.processLowpass (piA));
        const auto gridB = gridBiasDC + couplingB.processHighpass (plateLpB.processLowpass (piB));

        const auto vkA = driverA.processSample (gridA);
        const auto vkB = driverB.processSample (gridB);

        // power grids sit behind 1.5k stoppers; conduction current comes from the CF
        const auto g1A = vkA - kPowerStopper * driverA.getGridLoadCurrent() / (float) kPowerTubesPerSide;
        const auto g1B = vkB - kPowerStopper * driverB.getGridLoadCurrent() / (float) kPowerTubesPerSide;

        // screens (one fixed-point step per sample from the previous value)
        screenA = kHT2 - kScreenR * korenScreenCurrent (k6L6GC, g1A, screenA);
        screenB = kHT2 - kScreenR * korenScreenCurrent (k6L6GC, g1B, screenB);

        const auto ka = (float) kPowerTubesPerSide * korenPentodeGridTerm (k6L6GC, g1A, screenA);
        const auto kb = (float) kPowerTubesPerSide * korenPentodeGridTerm (k6L6GC, g1B, screenB);

        // joint plate solve through the centre-tapped primary
        const auto rl = kPrimaryLoadPerSide;
        const auto kvb = k6L6GC.kvb;
        auto va = juce::jlimit (0.0f, 2.0f * supply, plateA);

        for (int i = 0; i < kMaxPlateIterations; ++i)
        {
            const auto vb = 2.0f * supply - va;
            const auto f = va - supply + (ka * std::atan (va / kvb) - kb * std::atan (vb / kvb)) * rl;
            const auto df = 1.0f + rl * (ka / (kvb * (1.0f + (va / kvb) * (va / kvb)))
                                       + kb / (kvb * (1.0f + (vb / kvb) * (vb / kvb))));
            const auto step = f / df;
            va = juce::jlimit (0.0f, 2.0f * supply, va - step);
            if (std::abs (step) < kPlateTolerance)
                break;
        }

        plateA = va;
        const auto ia = ka * std::atan (va / kvb);
        const auto ib = kb * std::atan ((2.0f * supply - va) / kvb);

        const auto avgCurrent = sag.processLowpass (ia + ib);
        supply = juce::jlimit (0.8f * kHT1, kHT1, kHT1 - kSupplyR * (avgCurrent - idleTotal));

        // ideal OT: Vsec = (Ia - Ib) * Rl / n, n = sqrt(Rl / Rspeaker)
        auto vs = (ia - ib) * rl / turns;
        vs = otHigh.processLowpass (otLow.processHighpass (vs));
        const auto flux = otFlux.processLowpass (vs) / kSpeakerFullScale * 4.0f;
        return vs / (1.0f + kOtCoreSaturation * flux * flux);
    }

    float getGridBias() const noexcept          { return driverA.getCathodeVoltageDC(); }
    float getIdleCurrentPerTube() const noexcept { return idlePerTube; }
    float getSupplyVoltage() const noexcept     { return supply; }

private:
    CathodeFollower driverA, driverB;
    OnePole plateLpA, plateLpB, couplingA, couplingB, sag, otLow, otHigh, otFlux;
    float gridBiasDC = -43.8f, turns = 7.9f;
    float screenA = circuit::kHT2, screenB = circuit::kHT2;
    float idlePerTube = 0.04f, idleTotal = 0.16f;
    float supply = circuit::kHT1, plateA = circuit::kHT1;
};
} // namespace dumble
