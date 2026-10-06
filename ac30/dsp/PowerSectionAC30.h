#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Ac30Constants.h"
#include "NodalNetwork.h"
#include "PortSolver.h"
#include "PowerAmpEL84.h"
#include "TremoloOsc.h"
#include "dsp/OnePole.h"
#include "dsp/Pots.h"

namespace ac30
{
/** Master-section and tremolo settings. */
struct PowerControls
{
    float toneCut = 3.0f, master = 6.0f, tremSpeed = 5.0f, tremDepth = 0.0f;
    bool tremOn = true; // footswitch (Q2)
};

/** Supply rails (volts) and the currents that set them. */
struct Supplies { double b1 = 345.0, b2 = 320.0, b3 = 290.0, b4 = 260.0, b5 = 225.0; };

/**
    ProcessorChain element: phase inverter -> Tone Cut / Master -> 4 x EL84 -> OT, with the
    tremolo oscillator and the dynamic B+ (AC30C2 PreAmp / Power Amp sheets).

      in -- C45 -- grid A  V3 long-tailed pair: R55 1k2 + R60 47k tail, grid leaks R57/R63 1M to the
                           tail junction, grid B AC-grounded by C48, plates R67/R70 100k from B+3, C42 47p
      plates -- C50/C51 --+-- Tone Cut: VR9 + C80 4.7n between the phases
                          +-- R112/R105 10k -- Master VR10 between the phases -- R118/R115 220k
                          |                    (their junction = Depth wiper: the tremolo moves the
                          |                     common mode of the EL84 grids = bias tremolo)
                          +-- R113/R116 1M grid leaks -- 3k3 stoppers -- EL84 grids
      EL84 grid conduction loads this network, so overdrive charges C50/C51 (blocking distortion).

    The whole phase-inverter/master network is one NodalNetwork with 7 nonlinear ports (V3 plates,
    cathode, both grids, both EL84 grid pairs). Supplies: B+1 from the rectifier (no-load voltage
    behind the source resistance, reservoir C72), B+2 = B+1 - R120 1k (C68), B+3 = B+2 - R74 22k (C58),
    stepped explicitly per sample (time constants >= 4 ms).

    Input: U5B output volts. Output: speaker volts / kSpeakerFullScale.
*/
class PowerSectionAC30
{
public:
    static constexpr int kUpdateInterval = 32;

    PowerSectionAC30()
    {
        triode.table = dumble::KorenTable::forTube (dumble::k12AX7);
        triode.params = dumble::k12AX7;
        build();
    }

    void setControls (const PowerControls& c) noexcept
    {
        toneCut.setTargetValue (c.toneCut);
        master.setTargetValue (c.master);
        depth.setTargetValue (c.tremDepth);
        tremolo.setSpeed (c.tremSpeed);
        tremolo.setEnabled (c.tremOn);
    }

    /** Preamp supply currents drawn from B+2 through R22: B+4 (V2) and B+5 (V1). */
    void setPreampLoad (double ampsB4, double ampsB5) noexcept
    {
        preampB4 = ampsB4;
        preampB5 = ampsB5;
    }

    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        sampleRate = spec.sampleRate;
        dt = 1.0 / sampleRate;
        for (auto* s : { &toneCut, &master, &depth })
            s->reset (sampleRate, 0.03);
        net.prepare (sampleRate);
        power.prepare (sampleRate);
        tremolo.prepare (sampleRate);
        for (auto* s : { &toneCut, &master, &depth })
            s->setCurrentAndTargetValue (s->getTargetValue());
        solveOperatingPoint();
        reset();
    }

    /** Cheap (no DC solve): restores the operating point found by prepare(). Audio-thread safe. */
    void reset() noexcept
    {
        for (auto* s : { &toneCut, &master, &depth })
            s->setCurrentAndTargetValue (s->getTargetValue());
        update();

        rails = railsDC;
        power.reset();
        tremolo.setSupply (rails.b2);
        tremolo.reset();

        net.reset();
        setSourcesDC();
        for (int p = 0; p < solver.n; ++p)
            net.setCurrent (solver.port[(size_t) p], piCurrentsDC[(size_t) p]);
        net.buildDC();
        net.initialiseFromDC();
        solver.v = solver.vPrev = piVoltagesDC;
        cathodeEL84 = power.getCathodeVoltageDC();
        countdown = 0;
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        dumble::processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    float processSample (float inputVolts) noexcept
    {
        if (--countdown <= 0)
        {
            countdown = kUpdateInterval;
            if (toneCut.isSmoothing() || master.isSmoothing() || depth.isSmoothing())
            {
                for (auto* s : { &toneCut, &master, &depth })
                    s->skip (kUpdateInterval);
                update();
            }
        }

        // tremolo drive (top of the Depth pot)
        tremolo.setSupply (rails.b2);
        const auto lfo = tremolo.processSample();

        // phase inverter + master network
        net.setSource (sB3, rails.b3);
        net.setSource (sIn, (double) inputVolts);
        net.setSource (sLfo, (double) lfo);
        cathodeEL84 = power.getCathodeVoltage();
        solver.solve (net, [this] (const auto& v, auto& i, auto& g) { devices (v, i, g); });
        net.step();

        const auto& v = solver.v;
        const auto gridCurrents = -(solver.i[5] + solver.i[6]);
        const auto speaker = power.processSample (v[5], v[6], rails.b1, rails.b2, gridCurrents);

        stepSupplies ((rails.b3 - v[0]) / circuit::kPiPlate + (rails.b3 - v[1]) / circuit::kPiPlate);
        return speaker / circuit::kSpeakerFullScale;
    }

    //==========================================================================
    /** DC operating point of the whole power section for the given preamp load (not real-time):
        supplies, phase inverter, EL84 bias, tremolo oscillator. */
    void solveOperatingPoint() noexcept
    {
        using namespace circuit;
        Supplies s;

        update();

        for (int pass = 0; pass < 8; ++pass)
        {
            power.solveOperatingPoint (s.b1, s.b2);
            tremolo.setSupply (s.b2);
            tremolo.solveOperatingPoint();
            solvePhaseInverterDC (s.b3);

            const auto piCurrent = (s.b3 - solver.v[0]) / kPiPlate + (s.b3 - solver.v[1]) / kPiPlate;
            const auto platesB1 = 4.0 * power.getIdlePlateCurrent();
            const auto screensB2 = 4.0 * power.getIdleScreenCurrent();
            const auto fromB2 = screensB2 + tremolo.getSupplyCurrent() + piCurrent + preampB4 + preampB5;

            s.b1 = kNoLoadHT - kSourceR * (platesB1 + fromB2);
            s.b2 = s.b1 - kR120 * fromB2;
            s.b3 = s.b2 - kR74 * piCurrent;
            s.b4 = s.b2 - kR22 * (preampB4 + preampB5);
            s.b5 = s.b4 - kR15 * preampB5;
        }

        rails = railsDC = s;
        power.solveOperatingPoint (s.b1, s.b2);
        tremolo.setSupply (s.b2);
        tremolo.solveOperatingPoint();
        solvePhaseInverterDC (s.b3);
        piVoltagesDC = solver.v;
        piCurrentsDC = solver.i;
        piDC = { solver.v[0], solver.v[1], solver.v[2] };
    }

    const Supplies& getSupplies() const noexcept { return rails; }
    const PowerAmpEL84& getPowerAmp() const noexcept { return power; }
    struct PhaseInverterDC { double plateA, plateB, cathode; };
    PhaseInverterDC getPhaseInverterDC() const noexcept { return piDC; }
    const TremoloOsc& getTremolo() const noexcept { return tremolo; }

private:
    enum { nB3, nIn, nLfo, nGA, nGB, nPA, nPB, nK, nT, nOA, nOB, nCut, nOpP, nOpM, nQ, nEP, nEM, nCount };

    void build()
    {
        using namespace circuit;
        constexpr int g = decltype (net)::ground;
        net.setNumNodes (nCount);

        net.addCapacitor (nIn, nGA, kC45);                    // C45
        net.addResistor (nGA, nT, kPiGridLeak);               // R57
        net.addResistor (nGB, nT, kPiGridLeak);               // R63
        net.addCapacitor (nGB, g, kC48);                      // C48
        net.addResistor (nK, nT, kR55);                       // R55
        net.addResistor (nT, g, kR60);                        // R60
        net.addResistor (nB3, nPA, kPiPlate);                 // R67
        net.addResistor (nB3, nPB, kPiPlate);                 // R70
        net.addCapacitor (nPA, nPB, kC42);                    // C42
        for (auto [grid, plate] : { std::pair { nGA, nPA }, std::pair { nGB, nPB } })
        {
            net.addCapacitor (grid, plate, kCgp);
            net.addCapacitor (grid, nK, kCgk);
            net.addCapacitor (plate, nK, kCpk);
        }

        net.addCapacitor (nPA, nOA, kPiCoupling);             // C50
        net.addCapacitor (nPB, nOB, kPiCoupling);             // C51
        eCut = net.addResistor (nOB, nCut, kVR9);             // VR9 Tone Cut
        net.addCapacitor (nCut, nOA, kC80);                   // C80

        net.addResistor (nOA, nOpP, kMasterSeries);           // R112
        net.addResistor (nOB, nOpM, kMasterSeries);           // R105
        eMaster = net.addResistor (nOpP, nOpM, kVR10);        // VR10 Master
        net.addResistor (nOpP, nQ, kR118);                    // R118
        net.addResistor (nQ, nOpM, kR118);                    // R115
        net.addResistor (nQ, g, kPowerGridLeak);              // R114
        net.addResistor (nOpP, g, kPowerGridLeak);            // R113
        net.addResistor (nOpM, g, kPowerGridLeak);            // R116
        eDepthTop = net.addResistor (nLfo, nQ, kVR8 * 0.5);   // VR8 Depth (wiper = Q)
        eDepthBottom = net.addResistor (nQ, g, kVR8 * 0.5);

        net.addResistor (nOpP, nEP, kPowerStopper * 0.5);     // R101 || R108
        net.addResistor (nOpM, nEM, kPowerStopper * 0.5);     // R61 || R81
        net.addCapacitor (nEP, g, kPowerGridCap);
        net.addCapacitor (nEM, g, kPowerGridCap);

        sB3 = net.addVoltageSource (nB3);
        sIn = net.addVoltageSource (nIn);
        sLfo = net.addVoltageSource (nLfo);

        solver.port = { net.addCurrentPort (nPA), net.addCurrentPort (nPB), net.addCurrentPort (nK),
                        net.addCurrentPort (nGA), net.addCurrentPort (nGB),
                        net.addCurrentPort (nEP), net.addCurrentPort (nEM) };
    }

    // ports: 0 plate A, 1 plate B, 2 cathode, 3 grid A, 4 grid B, 5 EL84 grids (+), 6 EL84 grids (-)
    template <typename V, typename I, typename G>
    void devices (const V& v, I& i, G& g) const noexcept
    {
        i.fill (0.0);
        triode.add<7> (v, i, g, 0, 3, 2);
        triode.add<7> (v, i, g, 1, 4, 2);
        addGridDiode<7> (v, i, g, 5, cathodeEL84, 2.0, kEL84.rgi);
        addGridDiode<7> (v, i, g, 6, cathodeEL84, 2.0, kEL84.rgi);
    }

    void setSourcesDC() noexcept
    {
        net.setSource (sB3, rails.b3);
        net.setSource (sIn, 0.0);
        net.setSource (sLfo, 0.0);
    }

    void solvePhaseInverterDC (double b3) noexcept
    {
        net.reset();
        net.setSource (sB3, b3);
        net.setSource (sIn, 0.0);
        net.setSource (sLfo, 0.0);
        cathodeEL84 = power.getCathodeVoltageDC();
        solver.v = { 0.7 * b3, 0.7 * b3, 0.2 * b3, 0.19 * b3, 0.19 * b3, 0.0, 0.0 };
        solver.solveDC (net, [this] (const auto& v, auto& i, auto& g) { devices (v, i, g); });
    }

    void update() noexcept
    {
        using namespace circuit;
        // Tone Cut (B taper): clockwise = more cut = less resistance in series with C80
        net.setValue (eCut, 100.0 + kVR9 * (1.0 - dumble::potLinear (toneCut.getCurrentValue())));
        // Master (A taper rheostat between the phases): fully down shorts the phases
        net.setValue (eMaster, 10.0 + kVR10 * dumble::potAudio (master.getCurrentValue()));
        const auto d = depth.getCurrentValue() <= 0.0f ? 0.0 : dumble::potLinear (depth.getCurrentValue());
        net.setValue (eDepthTop, 10.0 + kVR8 * (1.0 - d));
        net.setValue (eDepthBottom, 10.0 + kVR8 * d);
        net.build();
    }

    void stepSupplies (double piCurrent) noexcept
    {
        using namespace circuit;
        auto& s = rails;
        const auto charge = std::fmax (0.0, (kNoLoadHT - s.b1) / kSourceR); // rectifier only sources current
        const auto i120 = (s.b1 - s.b2) / kR120;
        const auto i74 = (s.b2 - s.b3) / kR74;
        s.b1 += dt / kC72 * (charge - power.getPlateCurrent() - i120);
        s.b2 += dt / kC68 * (i120 - power.getScreenCurrent() - tremolo.getSupplyCurrent() - i74 - preampB4 - preampB5);
        s.b3 += dt / kC58 * (i74 - piCurrent);
    }

    NodalNetwork<nCount, 40, 10> net;
    PortSolver<7> solver;
    TriodeDevice triode;
    PowerAmpEL84 power;
    TremoloOsc tremolo;

    int eCut = 0, eMaster = 0, eDepthTop = 0, eDepthBottom = 0, sB3 = 0, sIn = 0, sLfo = 0;
    juce::SmoothedValue<float> toneCut { 3.0f }, master { 6.0f }, depth { 0.0f };
    Supplies rails, railsDC;
    std::array<double, 7> piVoltagesDC {}, piCurrentsDC {};
    PhaseInverterDC piDC {};
    double preampB4 = 0.004, preampB5 = 0.0015;
    double cathodeEL84 = 10.0;
    double sampleRate = 48000.0, dt = 1.0 / 48000.0;
    int countdown = 0;
};
} // namespace ac30
