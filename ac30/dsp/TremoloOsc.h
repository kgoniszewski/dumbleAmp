#pragma once

#include <cmath>

#include <juce_dsp/juce_dsp.h>

#include "Ac30Constants.h"
#include "NodalNetwork.h"
#include "PortSolver.h"
#include "dsp/Pots.h"

namespace ac30
{
/**
    AC30C2 tremolo oscillator: a phase-shift oscillator around Q4 (LND150N3 depletion MOSFET),
    simulated as a circuit, so frequency, amplitude, waveform and start-up come from the schematic:

        B+2 -- R93 33k --+-- C60 10u                        drain -- C63 22n -- p1 -- C64 10n -- p2 -- C62 10n -- gate
                         +-- R91 100k -- drain                         R86 27k + VR7 (Speed)    R87 1M          R89 3M3
        source -- R90 470R || C66 100u -- R92 10k (shorted by Q2 when the tremolo is on)
        drain -- C61 100n -- R94 510k -- D (C67 22n, VR8 Depth 500k)  -> D drives the power-grid common mode

    It runs at a decimated rate (~8 kHz; the LFO is 2..8 Hz) and is linearly interpolated back up.
    Output: the AC voltage on node D (top of the Depth pot).
*/
class TremoloOsc
{
public:
    static constexpr double kTargetRate = 8000.0;

    TremoloOsc() { build(); }

    void setSpeed (float knob0to10) noexcept   { speed = knob0to10; }
    void setEnabled (bool shouldRun) noexcept  { enabled = shouldRun; }
    void setSupply (double bPlus2) noexcept    { supply = bPlus2; }

    void prepare (double sampleRate) noexcept
    {
        decimation = juce::jmax (1, (int) std::lround (sampleRate / kTargetRate));
        net.prepare (sampleRate / decimation);
        solveOperatingPoint();
        reset();
    }

    /** DC operating point for the present supply (not real-time). */
    void solveOperatingPoint() noexcept
    {
        applyControls (true);
        net.reset();
        net.setSource (sSupply, supply);
        solver.v = { 0.45 * supply, 0.7, 0.0 };
        solver.solveDC (net, [this] (const auto& v, auto& i, auto& g) { devices (v, i, g); });
        dcV = solver.v;
        dcI = solver.i;
        supplyCurrent = -dcI[0]; // the drain current is all the oscillator draws at DC
    }

    /** Cheap: restores the stored operating point (audio-thread safe). */
    void reset() noexcept
    {
        applyControls (true);
        net.reset();
        net.setSource (sSupply, supply);
        for (int p = 0; p < solver.n; ++p)
            net.setCurrent (solver.port[(size_t) p], dcI[(size_t) p]);
        net.buildDC();
        net.initialiseFromDC();
        solver.v = solver.vPrev = dcV;
        supplyCurrent = -dcI[0];
        phase = 0;
        previous = next = 0.0;
        kick = true;
    }

    /** One audio-rate sample of the Depth-pot drive voltage. */
    float processSample() noexcept
    {
        if (++phase >= decimation)
        {
            phase = 0;
            applyControls (false);
            previous = next;
            next = step();
        }

        return (float) (previous + (next - previous) * (double) (phase + 1) / (double) decimation);
    }

    /** Current drawn from B+2 (amps). */
    double getSupplyCurrent() const noexcept { return supplyCurrent; }

    double getDrainVoltage() const noexcept { return solver.v[0]; }

private:
    enum { nSupply, nN, nD, nS, nY, nP1, nP2, nG, nDD, nOut, nCount };

    void build()
    {
        using namespace circuit;
        constexpr int g = decltype (net)::ground;
        net.setNumNodes (nCount);
        eSupplyR = net.addResistor (nSupply, nN, kR93);   // R93
        net.addCapacitor (nN, g, kC60);                   // C60
        net.addResistor (nN, nD, kR91);                   // R91
        net.addResistor (nS, nY, kR90);                   // R90
        net.addCapacitor (nS, nY, kC66);                  // C66
        net.addResistor (nY, g, kR92);                    // R92
        eSwitch = net.addResistor (nY, g, 10.0);          // Q2
        net.addCapacitor (nD, nP1, kC63);                 // C63
        eSpeed = net.addResistor (nP1, g, kR86 + kVR7);   // R86 + VR7
        net.addCapacitor (nP1, nP2, kC64);                // C64
        net.addResistor (nP2, g, kR87);                   // R87
        net.addCapacitor (nP2, nG, kC62);                 // C62
        net.addResistor (nG, g, kR89);                    // R89
        net.addCapacitor (nD, nDD, kC61);                 // C61
        net.addResistor (nDD, nOut, kR94);                // R94
        net.addCapacitor (nOut, g, kC67);                 // C67
        net.addResistor (nOut, g, kVR8);                  // VR8 (whole track)

        sSupply = net.addVoltageSource (nSupply);
        solver.port = { net.addCurrentPort (nD), net.addCurrentPort (nS), net.addCurrentPort (nG) };
        solver.maxStep = 50.0;
    }

    void applyControls (bool force) noexcept
    {
        // Speed: VR7 2.2M "C" taper as a rheostat, clockwise = faster = less resistance
        const auto r = circuit::kR86 + circuit::kVR7 * dumble::potAudio (10.0f - speed);
        net.setValue (eSpeed, r);
        net.setValue (eSwitch, enabled ? 10.0 : 1.0e9);
        if (force || std::abs (r - lastSpeedR) > 1.0e-6 || enabled != lastEnabled)
        {
            if (enabled && ! lastEnabled)
                kick = true;
            lastSpeedR = r;
            lastEnabled = enabled;
            net.build();
        }
    }

    double step() noexcept
    {
        net.setSource (sSupply, supply);
        solver.solve (net, [this] (const auto& v, auto& i, auto& g) { devices (v, i, g); });

        if (kick)
        {
            // start-up: in the amp, noise starts the oscillation; give it a ~10 mV nudge at the gate
            net.setCurrent (solver.port[2], 1.0e-3);
            kick = false;
        }

        net.step();
        supplyCurrent = (supply - net.voltage (nN)) / circuit::kR93;
        return net.voltage (nOut);
    }

    // ports: 0 drain, 1 source, 2 gate (no current)
    template <typename V, typename I, typename G>
    void devices (const V& v, I& i, G& g) const noexcept
    {
        using namespace circuit;
        i.fill (0.0);
        const auto vgs = v[2] - v[1];
        const auto vds = std::fmax (0.0, v[0] - v[1]);

        // smooth square law: Vov = softplus(k (Vgs - Vto)) / k
        constexpr double k = 25.0;
        const auto z = k * (vgs - kLndVto);
        const auto vov = z > 30.0 ? z / k : std::log1p (std::exp (z)) / k;
        const auto sig = 1.0 / (1.0 + std::exp (-z));
        const auto clm = 1.0 + kLndLambda * vds;

        double id, gm, gds;
        if (vds >= vov)
        {
            id = 0.5 * kLndBeta * vov * vov * clm;
            gm = kLndBeta * vov * sig * clm;
            gds = 0.5 * kLndBeta * vov * vov * kLndLambda;
        }
        else
        {
            const auto core = vov * vds - 0.5 * vds * vds;
            id = kLndBeta * core * clm;
            gm = kLndBeta * vds * sig * clm;
            gds = kLndBeta * (vov - vds) * clm + kLndBeta * core * kLndLambda;
        }

        i[0] = -id;
        i[1] = id;
        // dId/dVd = gds, dId/dVg = gm, dId/dVs = -gm - gds
        g[0 * 3 + 0] = -gds; g[0 * 3 + 2] = -gm; g[0 * 3 + 1] = gm + gds;
        g[1 * 3 + 0] = gds;  g[1 * 3 + 2] = gm;  g[1 * 3 + 1] = -gm - gds;
    }

    NodalNetwork<nCount, 20, 4> net;
    PortSolver<3> solver;
    std::array<double, 3> dcV {}, dcI {};
    int eSupplyR = 0, eSwitch = 0, eSpeed = 0, sSupply = 0;
    float speed = 5.0f;
    bool enabled = true, lastEnabled = true, kick = true;
    double lastSpeedR = -1.0, supply = 320.0, supplyCurrent = 0.0;
    int decimation = 1, phase = 0;
    double previous = 0.0, next = 0.0;
};
} // namespace ac30
