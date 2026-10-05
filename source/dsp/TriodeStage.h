#pragma once

#include <cstring>

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "KorenTable.h"
#include "OnePole.h"
#include "TriodeModel.h"

namespace dumble
{
/**
    Common-cathode triode gain stage solved as a nodal circuit (DK-style):

        B+ --Rp--+-- plate --Cc--+-- output
                 |               Rload (next grid leak / pot)
                 |    +--Cp-- gnd   (plate node capacitance: band-limits the plate slew)
                 |
              triode  <-- grid <-- Rgs <-- input (+ Miller lowpass)
                 |
                 +-- cathode --[Rk || Ck]-- gnd

    Unknowns per sample: Vp, Vk. Ck, Cp and Cc are discretised with trapezoidal companion models;
    the output node (Cc into Rload) is linear and eliminated analytically, so the plate sees the
    real AC load of the next stage (verified against ngspice, see spice/v1a_stage.cir).
    The nonlinear system is solved with Newton-Raphson, warm-started from the previous sample,
    with a hard iteration cap (early exit once converged) -> bounded worst-case cost per sample.
    Grid conduction through the grid stopper is solved separately (scalar Newton).
*/
class TriodeStage
{
public:
    static constexpr int   kMaxNewtonIterations = 4;     // hard upper bound -> bounded per-sample cost
    static constexpr float kNewtonTolerance     = 1.0e-3f; // volts (plate swings are tens of volts)
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

        // tabulated tube model (built once, off the audio thread); analytic model otherwise
        table = std::memcmp (&tubeParams, &k12AX7, sizeof (KorenTriodeParams)) == 0 ? &KorenTable::get12AX7() : nullptr;
        gCathode = (float) (2.0 * (double) v.cCathode * sampleRate);
        gPlate   = (float) (2.0 * (double) v.cPlate * sampleRate);
        gCoupling = (float) (2.0 * (double) v.cCoupling * sampleRate);
        gLoad = 1.0f / v.rLoad;
        // output node eliminated: i_c = gEff * vp - gLoad * hist / (gCoupling + gLoad)
        gCouplingEff = gCoupling * gLoad / (gCoupling + gLoad);

        // Miller pole: grid stopper plus a nominal source impedance against the input capacitance.
        constexpr float kSourceImpedance = 38.0e3f;
        miller.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * (v.rGridStopper + kSourceImpedance) * v.cMiller), sampleRate);

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
        couplingHistory = gCoupling * vpDC; // DC: cap charged to Vp, no current, output at 0 V
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
            const auto t = table != nullptr ? table->evaluate (vg - vk, vp - vk)
                                             : korenTriode (tubeParams, vg - vk, vp - vk);

            const auto couplingCurrent = gCouplingEff * vp - gLoad * couplingHistory / (gCoupling + gLoad);
            const auto f1 = (v.bPlus - vp) * gp - t.ip - (gPlate * vp - plateCapHistory) - couplingCurrent;
            const auto f2 = t.ip + ig - vk * gk - (gCathode * vk - capHistory);

            const auto j11 = -gp - t.dIdVpk - gPlate - gCouplingEff;
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

        // 4) output node: Cc into Rload
        const auto vout = (gCoupling * vp - couplingHistory) / (gCoupling + gLoad);
        const auto ic = vout * gLoad;
        couplingHistory = gCoupling * (vp - vout) + ic;
        return vout;
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
    const KorenTable* table = nullptr;
    double sampleRate = 48000.0;

    float gCathode = 0.0f, gPlate = 0.0f;
    float gCoupling = 0.0f, gLoad = 1.0e-6f, gCouplingEff = 0.0f, couplingHistory = 0.0f;
    float vp = 0.0f, vk = 0.0f;
    float vpDC = 0.0f, vkDC = 0.0f;
    float capCurrent = 0.0f, capHistory = 0.0f;
    float plateCapCurrent = 0.0f, plateCapHistory = 0.0f;

    OnePole miller;
};
} // namespace dumble
