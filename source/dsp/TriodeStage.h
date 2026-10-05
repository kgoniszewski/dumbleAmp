#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "OnePole.h"
#include "TriodeModel.h"

namespace dumble
{
/**
    Common-cathode triode gain stage solved as a nodal circuit (DK-style):

        B+ --Rp--+-- plate (output, via Cc into Rload)
                 |    +--Cp-- gnd   (plate node capacitance: band-limits the plate slew)
                 |
              triode  <-- grid <-- Rgs <-- input (+ Miller lowpass)
                 |
                 +-- cathode --[Rk || Ck]-- gnd

    Unknowns per sample: Vp, Vk. Ck and Cp are discretised with trapezoidal companion models.
    The nonlinear system is solved with Newton-Raphson, warm-started from the previous sample,
    with a hard iteration cap (early exit once converged) -> bounded worst-case cost per sample.
    Grid conduction through the grid stopper is solved separately (scalar Newton).
*/
class TriodeStage
{
public:
    static constexpr int   kMaxNewtonIterations = 4;     // hard upper bound -> bounded per-sample cost
    static constexpr float kNewtonTolerance     = 1.0e-4f; // volts
    static constexpr int   kGridIterations      = 3;

    TriodeStage() = default;

    void setCircuit (const circuit::TriodeStageValues& values, const KorenTriodeParams& tube) noexcept
    {
        v = values;
        tubeParams = tube;
    }

    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        sampleRate = spec.sampleRate;
        gCathode = (float) (2.0 * (double) v.cCathode * sampleRate);
        gPlate   = (float) (2.0 * (double) v.cPlate * sampleRate);

        // Miller pole: grid stopper plus a nominal source impedance against the input capacitance.
        constexpr float kSourceImpedance = 38.0e3f;
        miller.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * (v.rGridStopper + kSourceImpedance) * v.cMiller), sampleRate);
        coupling.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * v.cCoupling * v.rLoad), sampleRate);

        solveOperatingPoint();
        reset();
    }

    void reset() noexcept
    {
        vp = vpDC;
        vk = vkDC;
        capCurrent = 0.0f;
        capHistory = gCathode * vkDC;
        plateCapCurrent = 0.0f;
        plateCapHistory = gPlate * vpDC;
        miller.reset (0.0f);
        coupling.reset (vpDC);
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    float processSample (float input) noexcept
    {
        const auto vin = miller.processLowpass (input * v.inputDivider);

        // 1) grid conduction: Vg + Rgs * Ig(Vg - Vk) = Vin  (uses previous Vk; Vk moves slowly)
        auto vg = vin;
        auto ig = 0.0f;

        if (vin > vk)
        {
            for (int i = 0; i < kGridIterations; ++i)
            {
                float dIg = 0.0f;
                ig = gridCurrent (vg - vk, dIg);
                const auto f = vg + v.rGridStopper * ig - vin;
                vg -= f / (1.0f + v.rGridStopper * dIg);
            }

            float unused = 0.0f;
            ig = gridCurrent (vg - vk, unused);
        }

        // 2) plate/cathode nodes
        const auto gp = 1.0f / v.rPlate;
        const auto gk = 1.0f / v.rCathode;

        for (int i = 0; i < kMaxNewtonIterations; ++i)
        {
            const auto t = korenTriode (tubeParams, vg - vk, vp - vk);

            const auto f1 = (v.bPlus - vp) * gp - t.ip - (gPlate * vp - plateCapHistory);
            const auto f2 = t.ip + ig - vk * gk - (gCathode * vk - capHistory);

            const auto j11 = -gp - t.dIdVpk - gPlate;
            const auto j12 = t.dIdVgk + t.dIdVpk;
            const auto j21 = t.dIdVpk;
            const auto j22 = -t.dIdVgk - t.dIdVpk - gk - gCathode;

            const auto det = j11 * j22 - j12 * j21;

            if (std::abs (det) < 1.0e-20f)
                break;

            const auto invDet = 1.0f / det;
            const auto dvp = ( j22 * f1 - j12 * f2) * invDet;
            const auto dvk = (-j21 * f1 + j11 * f2) * invDet;

            vp = juce::jlimit (0.0f, v.bPlus, vp - dvp);
            vk = juce::jlimit (0.0f, 60.0f, vk - dvk);

            if (std::abs (dvp) < kNewtonTolerance && std::abs (dvk) < kNewtonTolerance)
                break;
        }

        // 3) trapezoidal companion updates for Ck and Cp
        capCurrent = gCathode * vk - capHistory;
        capHistory = gCathode * vk + capCurrent;
        plateCapCurrent = gPlate * vp - plateCapHistory;
        plateCapHistory = gPlate * vp + plateCapCurrent;

        // 4) output coupling cap into the next stage's load -> AC plate voltage
        return coupling.processHighpass (vp);
    }

    float getPlateVoltageDC() const noexcept   { return vpDC; }
    float getCathodeVoltageDC() const noexcept { return vkDC; }

private:
    void solveOperatingPoint() noexcept
    {
        // DC: Ck open, Vg = 0. Damped Newton from a sensible guess.
        auto p = v.bPlus * 0.6f;
        auto k = 1.5f;
        const auto gp = 1.0f / v.rPlate;
        const auto gk = 1.0f / v.rCathode;

        for (int i = 0; i < 200; ++i)
        {
            const auto t = korenTriode (tubeParams, -k, p - k);
            const auto f1 = (v.bPlus - p) * gp - t.ip;
            const auto f2 = t.ip - k * gk;
            const auto j11 = -gp - t.dIdVpk;
            const auto j12 = t.dIdVgk + t.dIdVpk;
            const auto j21 = t.dIdVpk;
            const auto j22 = -t.dIdVgk - t.dIdVpk - gk;
            const auto det = j11 * j22 - j12 * j21;

            if (std::abs (det) < 1.0e-20f)
                break;

            p -= 0.5f * ( j22 * f1 - j12 * f2) / det;
            k -= 0.5f * (-j21 * f1 + j11 * f2) / det;
            p = juce::jlimit (0.0f, v.bPlus, p);
            k = juce::jlimit (0.0f, 60.0f, k);
        }

        vpDC = p;
        vkDC = k;
    }

    circuit::TriodeStageValues v = circuit::kV1a;
    KorenTriodeParams tubeParams = k12AX7;
    double sampleRate = 48000.0;

    float gCathode = 0.0f, gPlate = 0.0f;
    float vp = 0.0f, vk = 0.0f;
    float vpDC = 0.0f, vkDC = 0.0f;
    float capCurrent = 0.0f, capHistory = 0.0f;
    float plateCapCurrent = 0.0f, plateCapHistory = 0.0f;

    OnePole miller, coupling;
};
} // namespace dumble
