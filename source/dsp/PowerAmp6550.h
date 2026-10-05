#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "OnePole.h"
#include "TriodeModel.h"

namespace dumble
{
/**
    Class-AB push-pull output stage, 2x6550 per side, fixed bias, with:
      - per-side coupling caps + grid conduction -> bias shift ("blocking" distortion),
      - plate load line through the output transformer (both plate voltages solved jointly:
        Va + Vb = 2 Vs, one scalar Newton solve with fixed iteration count),
      - power supply sag (reservoir RC driven by the total cathode current),
      - output transformer: primary-inductance low cut, leakage high cut, mild LF core saturation.

    Input: the two AC grid drives (volts) from the phase inverter. Output: normalised speaker signal.
*/
class PowerAmp6550
{
public:
    static constexpr int   kMaxPlateIterations = 4;
    static constexpr float kPlateTolerance     = 1.0e-3f; // volts

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        const auto dt = 1.0 / sampleRate;

        couplingA.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * circuit::kPiCCoupling * circuit::kPiRGridLeak), sampleRate);
        couplingB.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * circuit::kPiCCoupling * circuit::kPiRGridLeak), sampleRate);

        // grid-conduction charging (through PI source impedance) and discharge (through grid leak)
        chargeCoeff    = (float) (1.0 - std::exp (-dt / (circuit::kPiSourceR * circuit::kPiCCoupling)));
        dischargeCoeff = (float) std::exp (-dt / (circuit::kPiRGridLeak * circuit::kPiCCoupling * 4.0));

        sag.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * circuit::kSagTimeConstant), sampleRate);
        otLow.setCutoff (circuit::kOtLowCutHz, sampleRate);
        otHigh.setCutoff (circuit::kOtHighCutHz, sampleRate);
        otFlux.setCutoff (circuit::kOtLowCutHz * 0.5f, sampleRate);

        solveBias();
        reset();
    }

    void reset() noexcept
    {
        couplingA.reset();
        couplingB.reset();
        shiftA = shiftB = 0.0f;
        sag.reset (idleTotalCurrent);
        supply = circuit::kPowerBplus;
        plateA = circuit::kPowerBplus;
        otLow.reset();
        otHigh.reset();
        otFlux.reset();
    }

    float processSample (float driveA, float driveB) noexcept
    {
        // coupling caps into the grid leaks
        const auto ga = gridVoltage (couplingA.processHighpass (driveA), shiftA);
        const auto gb = gridVoltage (couplingB.processHighpass (driveB), shiftB);

        const auto screen = supply - circuit::kScreenDrop;
        const auto ka = (float) circuit::kPowerTubesPerSide * korenPentodeGridTerm (k6550, ga, screen);
        const auto kb = (float) circuit::kPowerTubesPerSide * korenPentodeGridTerm (k6550, gb, screen);

        // Joint plate solve through the centre-tapped primary:
        //   Va = Vs - (Ia - Ib) * Rl,  Vb = 2 Vs - Va,  I = k * atan(V / kvb)
        const auto rl = circuit::kPrimaryLoadPerSide;
        const auto kvb = k6550.kvb;
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

        // power supply sag
        const auto avgCurrent = sag.processLowpass (ia + ib);
        supply = juce::jlimit (0.6f * circuit::kPowerBplus, circuit::kPowerBplus,
                               circuit::kPowerBplus - circuit::kSagResistance * (avgCurrent - idleTotalCurrent));

        // output transformer
        auto y = (ia - ib) / circuit::kOutputRefCurrent;
        y = otHigh.processLowpass (otLow.processHighpass (y));
        const auto flux = otFlux.processLowpass (y) * 4.0f;
        return y / (1.0f + circuit::kOtCoreSaturation * flux * flux);
    }

    float getBiasVoltage() const noexcept      { return bias; }
    float getIdleTotalCurrent() const noexcept { return idleTotalCurrent; }
    float getSupplyVoltage() const noexcept    { return supply; }

private:
    float gridVoltage (float drive, float& shift) noexcept
    {
        auto g = bias + drive - shift;

        // grid conduction: the coupling cap charges, shifting the bias more negative
        if (g > 0.0f)
        {
            shift += chargeCoeff * g;
            g = g / (1.0f + g * 0.3f); // soft clamp of the conducting grid against the PI source impedance
        }

        shift *= dischargeCoeff;
        return g;
    }

    void solveBias() noexcept
    {
        // find the fixed bias giving the target idle current per tube (bisection, offline)
        const auto screen = circuit::kPowerBplus - circuit::kScreenDrop;
        auto lo = -120.0f, hi = 0.0f;

        for (int i = 0; i < 60; ++i)
        {
            const auto mid = 0.5f * (lo + hi);
            const auto ip = korenPentode (k6550, mid, screen, circuit::kPowerBplus);
            (ip > circuit::kIdleCurrentPerTube ? hi : lo) = mid;
        }

        bias = 0.5f * (lo + hi);
        idleTotalCurrent = 2.0f * (float) circuit::kPowerTubesPerSide
                         * korenPentode (k6550, bias, screen, circuit::kPowerBplus);
    }

    double sampleRate = 48000.0;
    float bias = -40.0f, idleTotalCurrent = 0.16f;
    float chargeCoeff = 0.0f, dischargeCoeff = 1.0f;
    float shiftA = 0.0f, shiftB = 0.0f;
    float supply = circuit::kPowerBplus, plateA = circuit::kPowerBplus;
    OnePole couplingA, couplingB, sag, otLow, otHigh, otFlux;
};
} // namespace dumble
