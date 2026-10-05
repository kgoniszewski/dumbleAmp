#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "KorenTable.h"
#include "OnePole.h"
#include "TriodeModel.h"

namespace dumble
{
/**
    Common-cathode triode gain stage solved as a nodal circuit (DK-style):

        B+ --Rp--+-- plate --+-- [internal: Cc into Rload]  or  [external LinearNetwork]
                 |           +--Cp-- gnd   (stray/Miller capacitance of the plate node)
              triode  <-- grid <-- Rgs <-- input (+ Miller lowpass)
                 |
                 +-- cathode --[Rk || Ck]-- node B --Rub-- gnd      (Rub = unbypassed part, may be 0)
                                               ^
                                               +-- injected current (local feedback, e.g. SSS LNFB)

    Unknowns per sample: Vp, Vk (node B follows from KCL). Ck, Cp and Cc are discretised with
    trapezoidal companion models. When the plate drives a passive network (tone stack, filters),
    that network's exact load for this sample is passed in as an affine current i = alpha*Vp + beta
    (see LinearNetwork::sourceCurrentAffine) and included in the Newton solve.

    Newton-Raphson is warm-started from the previous sample with a hard iteration cap (early exit
    once converged) -> bounded worst-case cost. Grid conduction through the stopper is a scalar
    Newton solve. Verified against ngspice (spice/gen_sss002_refs.py).
*/
class TriodeStage
{
public:
    static constexpr int   kMaxNewtonIterations = 4;
    static constexpr double kNewtonTolerance    = 1.0e-4;  // volts
    static constexpr int   kGridIterations      = 3;

    void setCircuit (const circuit::TriodeStageValues& values, const KorenTriodeParams& tube) noexcept
    {
        v = values;
        tubeParams = tube;
    }

    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        sampleRate = spec.sampleRate;
        table = KorenTable::forTube (tubeParams);

        gCathode = 2.0 * (double) v.cCathode * sampleRate;
        gPlate   = 2.0 * (double) v.cPlate * sampleRate;
        internalLoad = v.cCoupling > 0.0f && v.rLoad > 0.0f;
        gCoupling = internalLoad ? 2.0 * (double) v.cCoupling * sampleRate : 0.0;
        gLoad = internalLoad ? 1.0 / v.rLoad : 0.0;
        gCouplingEff = internalLoad ? gCoupling * gLoad / (gCoupling + gLoad) : 0.0;

        // Miller pole: grid stopper (+ ideal source) against the input capacitance; disabled when the
        // capacitance lives in the driving LinearNetwork.
        millerEnabled = v.cMiller > 0.0f && v.rGridStopper > 0.0f;
        if (millerEnabled)
            miller.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * v.rGridStopper * v.cMiller), sampleRate);

        solveOperatingPoint();
        reset();
    }

    void reset() noexcept
    {
        vp = vpDC;
        vk = vkDC;
        vpSlope = vkSlope = 0.0;
        capHistory = gCathode * ((double) vkDC - vbDC);
        plateCapHistory = gPlate * vpDC;
        couplingHistory = gCoupling * vpDC; // DC: cap charged to Vp, no current, output at 0 V
        injection = 0.0;
        miller.reset (0.0f);
    }

    /** Current injected into the bottom of the cathode resistor chain (local feedback). */
    void setCathodeInjection (float amps) noexcept { injection = (double) amps; }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    /** Internal-load mode: returns the AC voltage across Rload. */
    float processSample (float input) noexcept
    {
        solve (input, 0.0, 0.0);

        const auto vout = internalLoad ? (gCoupling * vp - couplingHistory) / (gCoupling + gLoad) : vp - (double) vpDC;
        if (internalLoad)
            couplingHistory = gCoupling * (vp - vout) + vout * gLoad;
        return (float) vout;
    }

    /** External-load mode: the plate also feeds a network drawing i = alpha*Vp + beta. Returns absolute Vp. */
    float processSampleLoaded (float input, double alpha, double beta) noexcept
    {
        solve (input, alpha, beta);
        return (float) vp;
    }

    float getPlateVoltage() const noexcept     { return (float) vp; }
    float getPlateVoltageDC() const noexcept   { return vpDC; }
    float getCathodeVoltageDC() const noexcept { return vkDC; }

private:
    void solve (float input, double alpha, double beta) noexcept
    {
        // Double precision for the node equations: at high sample rates the cathode capacitor's
        // companion conductance 2C/T is ~2 S, so float states would quantise the cathode current
        // to ~0.5 uA, comparable to the signal current of a small-signal stage.
        // The network load alpha*Vp + beta (a small difference of two large numbers) is re-centred
        // on the previous plate voltage: i(Vp) = i0 + alpha * (Vp - Vp_prev).
        const auto vpPrev = vp, vkPrev = vk;
        const auto i0 = alpha * vpPrev + beta;

        // linear predictor: at the oversampled rate the nodes move almost linearly, so starting
        // Newton from the extrapolated point usually converges in a single step
        vp = juce::jlimit (0.0, (double) v.bPlus, vp + vpSlope);
        vk = juce::jlimit (-5.0, 80.0, vk + vkSlope);
        const auto vin = (double) (millerEnabled ? miller.processLowpass (input) : input);

        // 1) grid conduction: Vg + Rgs * Ig(Vg - Vk) = Vin (uses previous Vk; Vk moves slowly)
        auto vg = vin;
        auto ig = 0.0;

        if (v.rGridStopper > 0.0f && vin - vk > -0.5)
        {
            for (int i = 0; i < kGridIterations; ++i)
            {
                float dIg = 0.0f;
                ig = gridCurrent ((float) (vg - vk), tubeParams.rgi, dIg);
                vg -= (vg + v.rGridStopper * ig - vin) / (1.0 + v.rGridStopper * (double) dIg);
            }

            float unused = 0.0f;
            ig = gridCurrent ((float) (vg - vk), tubeParams.rgi, unused);
        }

        // 2) plate/cathode nodes
        const auto gp = 1.0 / v.rPlate;
        const auto gk = 1.0 / v.rCathode;
        const auto rub = (double) v.rCathodeUnbypassed;
        const auto s = gk + gCathode;

        TriodeCurrent t {};
        double dvp = 0.0, dvk = 0.0;
        for (int i = 0; i < kMaxNewtonIterations; ++i)
        {
            t = evaluate (vg - vk, vp - vk);
            dvp = dvk = 0.0;

            const auto u = vk - rub * (t.ip + ig + injection); // voltage across Rk || Ck
            const auto coupling = gCouplingEff * vp - (internalLoad ? gLoad * couplingHistory / (gCoupling + gLoad) : 0.0);

            const auto f1 = (v.bPlus - vp) * gp - t.ip - (gPlate * vp - plateCapHistory) - coupling - (i0 + alpha * (vp - vpPrev));
            const auto f2 = t.ip + ig - (s * u - capHistory);

            const auto d = (double) t.dIdVgk + t.dIdVpk;
            const auto j11 = -gp - t.dIdVpk - gPlate - gCouplingEff - alpha;
            const auto j12 = d;
            const auto j21 = t.dIdVpk * (1.0 + s * rub);
            const auto j22 = -d * (1.0 + s * rub) - s;

            const auto det = j11 * j22 - j12 * j21;
            if (std::abs (det) < 1.0e-30)
                break;

            const auto vpOld = vp, vkOld = vk;
            vp = juce::jlimit (0.0, (double) v.bPlus, vp - ( j22 * f1 - j12 * f2) / det);
            vk = juce::jlimit (-5.0, 80.0, vk - (-j21 * f1 + j11 * f2) / det);
            dvp = vp - vpOld;
            dvk = vk - vkOld;

            if (std::abs (dvp) < kNewtonTolerance && std::abs (dvk) < kNewtonTolerance)
                break;
        }

        // 3) companion updates. The last Newton step is tiny once converged, so the tube current at
        //    the final point is the first-order update of the last evaluation (saves a table read);
        //    if the iteration cap was hit the step may be large, so evaluate again.
        if (std::abs (dvp) < 0.05 && std::abs (dvk) < 0.05)
            t.ip += (float) ((double) t.dIdVgk * -dvk + (double) t.dIdVpk * (dvp - dvk));
        else
            t = evaluate (vg - vk, vp - vk);
        const auto u = vk - rub * (t.ip + ig + injection);
        const auto capCurrent = gCathode * u - capHistory;
        capHistory = gCathode * u + capCurrent;
        vpSlope = vp - vpPrev;
        vkSlope = vk - vkPrev;
        const auto plateCapCurrent = gPlate * vp - plateCapHistory;
        plateCapHistory = gPlate * vp + plateCapCurrent;
    }

    TriodeCurrent evaluate (double vgk, double vpk) const noexcept
    {
        return table != nullptr ? table->evaluate ((float) vgk, (float) vpk)
                                : korenTriode (tubeParams, (float) vgk, (float) vpk);
    }

    void solveOperatingPoint() noexcept
    {
        // DC: capacitors open, Vg = 0, no external load (networks are AC-coupled). Damped Newton.
        auto p = v.bPlus * 0.6f;
        auto k = 1.5f;
        const auto gp = 1.0f / v.rPlate;
        const auto gk = 1.0f / v.rCathode;
        const auto rub = v.rCathodeUnbypassed;

        for (int i = 0; i < 300; ++i)
        {
            const auto t = korenTriode (tubeParams, -k, p - k);
            const auto f1 = (v.bPlus - p) * gp - t.ip;
            const auto f2 = t.ip - (k - rub * t.ip) * gk;
            const auto d = t.dIdVgk + t.dIdVpk;
            const auto j11 = -gp - t.dIdVpk;
            const auto j12 = d;
            const auto j21 = t.dIdVpk * (1.0f + gk * rub);
            const auto j22 = -d * (1.0f + gk * rub) - gk;
            const auto det = j11 * j22 - j12 * j21;

            if (std::abs (det) < 1.0e-20f)
                break;

            p = juce::jlimit (0.0f, v.bPlus, p - 0.5f * ( j22 * f1 - j12 * f2) / det);
            k = juce::jlimit (0.0f, 80.0f, k - 0.5f * (-j21 * f1 + j11 * f2) / det);
        }

        vpDC = p;
        vkDC = k;
        vbDC = rub * korenTriode (tubeParams, -k, p - k).ip;
    }

    circuit::TriodeStageValues v {};
    KorenTriodeParams tubeParams = k12AX7;
    const KorenTable* table = nullptr;
    double sampleRate = 48000.0;
    bool internalLoad = true, millerEnabled = false;

    double gCathode = 0.0, gPlate = 0.0;
    double gCoupling = 0.0, gLoad = 0.0, gCouplingEff = 0.0, couplingHistory = 0.0;
    double vp = 0.0, vk = 0.0, vpSlope = 0.0, vkSlope = 0.0;
    float vpDC = 0.0f, vkDC = 0.0f, vbDC = 0.0f;
    double capHistory = 0.0, plateCapHistory = 0.0;
    double injection = 0.0;

    OnePole miller;
};
} // namespace dumble
