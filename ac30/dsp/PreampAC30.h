#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Ac30Constants.h"
#include "NodalNetwork.h"
#include "PortSolver.h"
#include "dsp/OnePole.h"
#include "dsp/Pots.h"

namespace ac30
{
/** Which input jack the guitar is in ("both" = Normal Hi jumpered into Top Boost Hi). */
enum class InputJack { normalHi, normalLo, topBoostHi, topBoostLo, both };

/** Front-panel settings of the preamp (knobs 0..10). */
struct PreampControls
{
    InputJack input = InputJack::topBoostHi;
    float normalVolume = 0.0f, topBoostVolume = 5.0f, treble = 6.0f, bass = 5.0f;
};

/**
    AC30C2 preamp, input jacks to the U1B mixer output (ProcessorChain element).

      Network A (B+5):  jacks -> V1 TB triode (220k)  -> C9 470p -> TB Volume (+C15 bright) -> V2a grid
                        jacks -> V1 Normal triode (100k) -> C7 -> R13 || C8 -> Normal Volume -> R37 470k
                        both triodes on the shared cathode R16 1k5 || C11 22u
      Network B (B+4):  V2a (100k, 1k5 || 22u) = V2b cathode follower (DC) -> Top Boost stack -> R34/R35
      U1B:              TB on (+), Normal through R37 on (-), R40 820k: Normal is phase-inverted

    Each network is a NodalNetwork; the tubes are its nonlinear ports, solved by Newton every sample
    (A: two plates, shared cathode, both grids, V2a grid conduction; B: V2a plate and cathode, CF
    cathode with grid conduction into the V2a plate). The V2a grid voltage couples A -> B, and B's
    V2a cathode voltage (previous sample) closes the grid-conduction loop. Inter-electrode
    capacitances are network elements, so the Miller effect is exact.

    Output: U1B output volts (soft-saturated at the +/-27 V rails).
*/
class PreampAC30
{
public:
    static constexpr int kUpdateInterval = 32;

    PreampAC30()
    {
        triode.table = dumble::KorenTable::forTube (dumble::k12AX7);
        triode.params = dumble::k12AX7;
        buildA();
        buildB();
    }

    void setControls (const PreampControls& c) noexcept
    {
        normalVolume.setTargetValue (c.normalVolume);
        topBoostVolume.setTargetValue (c.topBoostVolume);
        treble.setTargetValue (c.treble);
        bass.setTargetValue (c.bass);

        if (c.input != input)
        {
            input = c.input;
            inputChanged = true;
        }
    }

    /** Supply voltages (B+4 for V2, B+5 for V1); applied at the next prepare()/reset(). */
    void setSupplies (double bPlus4, double bPlus5) noexcept
    {
        b4 = bPlus4;
        b5 = bPlus5;
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        for (auto* s : { &normalVolume, &topBoostVolume, &treble, &bass })
            s->reset (sampleRate, 0.03);

        netA.prepare (sampleRate);
        netB.prepare (sampleRate);
        for (auto* s : { &normalVolume, &topBoostVolume, &treble, &bass })
            s->setCurrentAndTargetValue (s->getTargetValue());
        solveOperatingPoint();
        reset();
    }

    /** Cheap (no DC solve): restores the operating point found by prepare(). Audio-thread safe. */
    void reset() noexcept
    {
        for (auto* s : { &normalVolume, &topBoostVolume, &treble, &bass })
            s->setCurrentAndTargetValue (s->getTargetValue());

        updateNetworks();
        netA.reset();
        netB.reset();
        setSourcesA (0.0);
        setSourcesB (0.0);
        restore (netA, solverA, dcA);
        restore (netB, solverB, dcB);
        v2aCathode = dcB.v[1];
        netBGrid = 0.0;
        countdown = 0;
    }

    /** DC operating point for the present supplies (not real-time; prepare() calls it). */
    void solveOperatingPoint() noexcept
    {
        updateNetworks();

        // network B first (V2a grid at 0 V DC), then A with the V2a cathode known
        netB.reset();
        setSourcesB (0.0);
        netBGrid = 0.0;
        solverB.v = { 0.55 * b4, 1.5, 0.57 * b4 };
        solverB.solveDC (netB, [this] (const auto& v, auto& i, auto& g) { devicesB (v, i, g); });
        dcB = { solverB.v, solverB.i };
        v2aCathode = solverB.v[1];

        netA.reset();
        setSourcesA (0.0);
        solverA.v = { 0.55 * b5, 0.6 * b5, 1.2, 0.0, 0.0, 0.0 };
        solverA.solveDC (netA, [this] (const auto& v, auto& i, auto& g) { devicesA (v, i, g); });
        dcA = { solverA.v, solverA.i };

        op = { dcA.v[0], dcA.v[1], dcA.v[2], dcB.v[0], dcB.v[1], dcB.v[2] };
        i5 = (b5 - op.v1PlateTB) / circuit::kV1PlateTB + (b5 - op.v1PlateN) / circuit::kV1PlateN;
        i4 = (b4 - op.v2aPlate) / circuit::kV2Plate + op.cfCathode / circuit::kV2bLoad;
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
            const auto smoothing = normalVolume.isSmoothing() || topBoostVolume.isSmoothing()
                                || treble.isSmoothing() || bass.isSmoothing();
            if (smoothing)
                for (auto* s : { &normalVolume, &topBoostVolume, &treble, &bass })
                    s->skip (kUpdateInterval);

            if (smoothing || inputChanged)
                updateNetworks();
        }

        // network A: V1 pair + channel networks
        const auto x = (double) inputVolts;
        netA.setSource (sInN, normalGain * x);
        netA.setSource (sInT, topBoostGain * x);
        solverA.solve (netA, [this] (const auto& v, auto& i, auto& g) { devicesA (v, i, g); });
        netA.step();

        // network B: V2a + cathode follower + Top Boost stack
        netBGrid = netA.voltage (aTw);
        netB.setSource (sG2, netBGrid);
        solverB.solve (netB, [this] (const auto& v, auto& i, auto& g) { devicesB (v, i, g); });
        netB.step();
        v2aCathode = solverB.v[1];

        const auto plus = netB.voltage (bP);
        const auto wiper = netA.voltage (aNw);
        const auto out = plus * (1.0 + circuit::kR40 / circuit::kR37) - wiper * (circuit::kR40 / circuit::kR37);
        return opAmpClip ((float) out, circuit::kOpAmpSwing);
    }

    /** DC operating point (for tests). */
    struct OperatingPoint { double v1PlateTB, v1PlateN, v1Cathode, v2aPlate, v2aCathode, cfCathode; };
    OperatingPoint getOperatingPoint() const noexcept { return op; }

    /** Supply currents drawn at the operating point: { B+4, B+5 } in amps. */
    std::pair<double, double> getSupplyCurrents() const noexcept { return { i4, i5 }; }

    /** Mean Newton iterations per sample of the two networks (diagnostics). */
    std::pair<double, double> getMeanIterations() const noexcept
    {
        return { (double) solverA.totalIterations / (double) std::max (1LL, solverA.totalSolves),
                 (double) solverB.totalIterations / (double) std::max (1LL, solverB.totalSolves) };
    }

private:
    // network A nodes
    enum { aInN, aInT, aB5, aGN, aGT, aPN, aPT, aK, aNa, aNb, aNw, aTt, aTw, aCount };
    // network B nodes
    enum { bB4, bG2, bPA, bKA, bKB, bTt, bTw, bJ, bA, bBn, bO1, bP, bCount };

    void buildA()
    {
        using namespace circuit;
        constexpr int g = decltype (netA)::ground;
        netA.setNumNodes (aCount);

        eSeriesN = netA.addResistor (aInN, aGN, kHiSeries);
        eSeriesT = netA.addResistor (aInT, aGT, kHiSeries);
        netA.addCapacitor (aGT, aK, kC13);

        // grid-plate capacitances (Miller); Cgk (to the bypassed cathode) and Cpk are negligible here
        netA.addCapacitor (aGN, aPN, kCgp);
        netA.addCapacitor (aGT, aPT, kCgp);

        netA.addResistor (aB5, aPT, kV1PlateTB);              // R14
        netA.addResistor (aB5, aPN, kV1PlateN);               // R12
        netA.addResistor (aK, g, kV1Cathode);                 // R16
        netA.addCapacitor (aK, g, kV1CathodeCap);             // C11

        netA.addCapacitor (aPN, aNa, kC7);                    // C7
        netA.addResistor (aNa, aNb, kR13);                    // R13
        netA.addCapacitor (aNa, aNb, kC8);                    // C8
        eNormalTop = netA.addResistor (aNb, aNw, kVR1 * 0.5); // VR1
        eNormalBottom = netA.addResistor (aNw, g, kVR1 * 0.5);
        netA.addResistor (aNw, g, kR37);                      // R37 into U1B's (-) input (virtual ground)

        netA.addCapacitor (aPT, aTt, kC9);                    // C9
        eTopBoostTop = netA.addResistor (aTt, aTw, kVR2 * 0.5); // VR2
        eTopBoostBottom = netA.addResistor (aTw, g, kVR2 * 0.5);
        netA.addCapacitor (aTt, aTw, kC15);                   // C15 bright
        netA.addCapacitor (aTw, g, kCgk + kCgp * 61.0);       // V2a input capacitance (Miller, gain ~60)

        sInN = netA.addVoltageSource (aInN);
        sInT = netA.addVoltageSource (aInT);
        sB5 = netA.addVoltageSource (aB5);

        solverA.port = { netA.addCurrentPort (aPT), netA.addCurrentPort (aPN), netA.addCurrentPort (aK),
                         netA.addCurrentPort (aGT), netA.addCurrentPort (aGN), netA.addCurrentPort (aTw) };
    }

    void buildB()
    {
        using namespace circuit;
        constexpr int g = decltype (netB)::ground;
        netB.setNumNodes (bCount);

        netB.addResistor (bB4, bPA, kV2Plate);                // R32
        netB.addResistor (bKA, g, kV2Cathode);                // R26
        netB.addCapacitor (bKA, g, kV2CathodeCap);            // C18
        // V2's inter-electrode capacitances only add poles in the MHz range here (V2a's Miller
        // capacitance is in network A, where it loads the Top Boost volume)
        netB.addResistor (bKB, g, kV2bLoad);                  // R21
        netB.addCapacitor (bKB, g, kC84);                     // C84

        netB.addCapacitor (bKB, bTt, kC23);                   // C23
        eTrebleTop = netB.addResistor (bTt, bTw, kVR3 * 0.5); // VR3
        eTrebleBottom = netB.addResistor (bTw, bJ, kVR3 * 0.5);
        netB.addResistor (bKB, bA, kR19);                     // R19
        netB.addCapacitor (bA, bJ, kC28);                     // C28
        eBass = netB.addResistor (bJ, bBn, kVR4 * 0.5);       // VR4 (wiper tied to its bottom end)
        netB.addCapacitor (bA, bBn, kC38);                    // C38
        netB.addResistor (bBn, g, kR47);                      // R47
        netB.addCapacitor (bTw, bO1, kC25);                   // C25
        netB.addResistor (bO1, bP, kR34);                     // R34
        netB.addResistor (bP, g, kR35);                       // R35

        sB4 = netB.addVoltageSource (bB4);
        sG2 = netB.addVoltageSource (bG2);

        solverB.port = { netB.addCurrentPort (bPA), netB.addCurrentPort (bKA), netB.addCurrentPort (bKB) };
    }

    // A: 0 TB plate, 1 Normal plate, 2 cathode, 3 TB grid, 4 Normal grid, 5 V2a grid (TB volume wiper)
    template <typename V, typename I, typename G>
    void devicesA (const V& v, I& i, G& g) const noexcept
    {
        i.fill (0.0);
        triode.add<6> (v, i, g, 0, 3, 2);
        triode.add<6> (v, i, g, 1, 4, 2);
        addGridDiode<6> (v, i, g, 5, v2aCathode, 1.0, triode.params.rgi);
    }

    // B: 0 V2a plate (= CF grid), 1 V2a cathode, 2 CF cathode
    template <typename V, typename I, typename G>
    void devicesB (const V& v, I& i, G& g) const noexcept
    {
        i.fill (0.0);
        triode.add<3> (v, i, g, 0, -1, 1, 0.0, netBGrid);
        triode.add<3> (v, i, g, -1, 0, 2, b4);
    }

    void updateNetworks() noexcept
    {
        using namespace circuit;
        const auto n = dumble::potAudio (normalVolume.getCurrentValue());
        const auto t = dumble::potAudio (topBoostVolume.getCurrentValue());
        netA.setValue (eNormalTop, kVR1 * (1.0 - n));
        netA.setValue (eNormalBottom, kVR1 * n);
        netA.setValue (eTopBoostTop, kVR2 * (1.0 - t));
        netA.setValue (eTopBoostBottom, kVR2 * t);

        const auto normalIn = input == InputJack::normalHi || input == InputJack::normalLo || input == InputJack::both;
        const auto topBoostIn = input == InputJack::topBoostHi || input == InputJack::topBoostLo || input == InputJack::both;
        const auto normalLo = input == InputJack::normalLo, topBoostLo = input == InputJack::topBoostLo;
        normalGain = normalIn ? (normalLo ? kLoGain : 1.0) : 0.0;
        topBoostGain = topBoostIn ? (topBoostLo ? kLoGain : 1.0) : 0.0;
        netA.setValue (eSeriesN, normalLo ? kLoSeries : kHiSeries);
        netA.setValue (eSeriesT, topBoostLo ? kLoSeries : kHiSeries);
        netA.build();

        const auto tr = dumble::potAudio (treble.getCurrentValue());
        netB.setValue (eTrebleTop, kVR3 * (1.0 - tr));
        netB.setValue (eTrebleBottom, kVR3 * tr);
        netB.setValue (eBass, kVR4 * dumble::potAudio (bass.getCurrentValue()));
        netB.build();

        inputChanged = false;
    }

    void setSourcesA (double in) noexcept
    {
        netA.setSource (sB5, b5);
        netA.setSource (sInN, normalGain * in);
        netA.setSource (sInT, topBoostGain * in);
    }

    void setSourcesB (double grid) noexcept
    {
        netB.setSource (sB4, b4);
        netB.setSource (sG2, grid);
    }

    template <int N> struct DCState { std::array<double, (size_t) (N)> v {}, i {}; };

    template <typename Net, typename Solver, typename State>
    static void restore (Net& net, Solver& solver, const State& dc) noexcept
    {
        // sources were set by setSources*; device currents from the stored solution
        for (int p = 0; p < solver.n; ++p)
            net.setCurrent (solver.port[(size_t) p], dc.i[(size_t) p]);
        net.buildDC();
        net.initialiseFromDC();
        solver.v = dc.v;
        solver.vPrev = dc.v;
    }

    NodalNetwork<aCount, 24, 9> netA;
    NodalNetwork<bCount, 24, 6> netB;
    PortSolver<6> solverA;
    PortSolver<3> solverB;
    DCState<6> dcA;
    DCState<3> dcB;
    TriodeDevice triode;

    int eSeriesN = 0, eSeriesT = 0, eNormalTop = 0, eNormalBottom = 0, eTopBoostTop = 0, eTopBoostBottom = 0;
    int eTrebleTop = 0, eTrebleBottom = 0, eBass = 0;
    int sInN = 0, sInT = 0, sB5 = 0, sB4 = 0, sG2 = 0;

    juce::SmoothedValue<float> normalVolume { 0.0f }, topBoostVolume { 5.0f }, treble { 6.0f }, bass { 5.0f };
    InputJack input = InputJack::topBoostHi;
    bool inputChanged = true;
    double normalGain = 0.0, topBoostGain = 1.0;
    double b4 = 262.0, b5 = 228.0;
    double v2aCathode = 1.2, netBGrid = 0.0;
    double i4 = 0.0, i5 = 0.0;
    OperatingPoint op {};
    double sampleRate = 48000.0;
    int countdown = 0;
};
} // namespace ac30
