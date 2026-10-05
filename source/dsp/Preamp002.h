#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CathodeFollower.h"
#include "CircuitConstants.h"
#include "LinearNetwork.h"
#include "OnePole.h"
#include "Pots.h"
#include "SpringTank.h"
#include "TriodeStage.h"

namespace dumble
{
/** Front-panel settings of the preamp (knobs 0..10). */
struct PreampControls
{
    float treble = 5.0f, middle = 5.0f, bass = 5.0f, volume = 5.0f;
    float reverbSend = 0.0f, reverbReturn = 0.0f;
    bool bright = true, deep = false;
    int high = 4, low = 4; // 1..7
};

/**
    The SSS #002 preamp, input jack to the dry/reverb mixing node (ProcessorChain element).

        V1 (5751) --plate--> tone stack ("Guitar") -> Volume (+Bright) -+-> V4 (5751) --plate--> filter network
          ^  cathode                         [Deep network]          |       ^ cathode      (C6, C37, R53+L2 300mH,
          |                                                           |       |               High 7-pos, Low 7-pos ladder)
          +---- LNFB: V4 plate -> C15 0.1u -> R54 100k ---------------|-------+                      |
                                                                      |       +--- LNFB2 <-- U38 ----+-- U37 (7025) -> U38 CF
                                                                      |                                              |
                                                                      +-> U20 send -> Send pot -> driver/tank        | 220k
                                                                          -> U28 recovery -> Return pot -> U39 -> U40 CF -- 220k --> mix node

    The two passive sections are LinearNetworks driven directly by the V1 and V4 plates; each plate
    stage includes the exact network load in its Newton solve. The three local feedback paths
    (LNFB, LNFB2, U40 -> U28) are closed with a one-sample delay.

    Output: open-circuit AC voltage of the mix node (Thevenin 110k, handled by MasterStage).
*/
class Preamp002
{
public:
    static constexpr int kUpdateInterval = 32;

    Preamp002()
    {
        using namespace circuit;
        v1.setCircuit (kV1, k5751);
        v4.setCircuit (kV4, k5751);
        u37.setCircuit (kU37, k7025);
        u39.setCircuit (kU39, k7025);
        u20.setCircuit (kU20, k7025);
        u28.setCircuit (kU28, k7025);
        // R84/R85 join two cathodes at the same DC level, so they carry no DC: no extra shunt.
        u38.setCircuit ({ kHT4, kCfLoad, 0.0f, 0.0f, 40.0e3f }, k7025);
        u40.setCircuit ({ kHT4, kCfLoad, 0.0f, 0.0f, 40.0e3f }, k7025);
        buildFrontTopology();
        buildMidTopology();
    }

    void setControls (const PreampControls& c) noexcept
    {
        treble.setTargetValue (c.treble);
        middle.setTargetValue (c.middle);
        bass.setTargetValue (c.bass);
        volume.setTargetValue (c.volume);
        send.setTargetValue (c.reverbSend);
        ret.setTargetValue (c.reverbReturn);

        if (c.bright != bright || c.deep != deep || c.high != high || c.low != low)
        {
            bright = c.bright;
            deep = c.deep;
            high = juce::jlimit (1, 7, c.high);
            low = juce::jlimit (1, 7, c.low);
            switchesChanged = true;
        }
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        const juce::dsp::ProcessSpec mono { spec.sampleRate, spec.maximumBlockSize, 1 };

        // the reverb return (tank output, ~4.5 kHz bandwidth) runs at >= 88.2 kHz only
        returnDecimation = juce::jmax (1, (int) std::lround (sampleRate / 96000.0));
        const juce::dsp::ProcessSpec returnSpec { sampleRate / returnDecimation, spec.maximumBlockSize, 1 };

        for (auto* t : { &v1, &v4, &u37, &u20 })
            t->prepare (mono);
        for (auto* t : { &u28, &u39 })
            t->prepare (returnSpec);

        u38.prepare (u37.getPlateVoltageDC());
        u40.prepare (u39.getPlateVoltageDC());
        tank.prepare (mono);

        for (auto* s : { &treble, &middle, &bass, &volume, &send, &ret })
            s->reset (sampleRate, 0.03);

        lnfb2Filter.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * circuit::kLnfb2R * circuit::kLnfb2C), sampleRate);
        revFbFilter.setCutoff (1.0f / (juce::MathConstants<float>::twoPi * circuit::kRevFbR * circuit::kRevFbC),
                               returnSpec.sampleRate);

        front.prepare (sampleRate);
        mid.prepare (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto* s : { &treble, &middle, &bass, &volume, &send, &ret })
            s->setCurrentAndTargetValue (s->getTargetValue());

        updateNetworks();

        for (auto* t : { &v1, &v4, &u37, &u39, &u20, &u28 })
            t->reset();
        u38.reset();
        u40.reset();
        tank.reset();
        lnfb2Filter.reset();
        revFbFilter.reset();

        front.initialiseDC (v1.getPlateVoltageDC());
        mid.initialiseDC (v4.getPlateVoltageDC());

        lnfbCurrent = lnfb2Current = revFbCurrent = 0.0f;
        countdown = 0;
        returnPhase = 0;
        returnAccum = wetPrev = wetNext = 0.0f;
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    float processSample (float input) noexcept
    {
        if (--countdown <= 0)
        {
            countdown = kUpdateInterval;
            const auto smoothing = treble.isSmoothing() || middle.isSmoothing() || bass.isSmoothing()
                                || volume.isSmoothing() || send.isSmoothing() || ret.isSmoothing();
            if (smoothing)
                for (auto* s : { &treble, &middle, &bass, &volume, &send, &ret })
                    s->skip (kUpdateInterval);

            if (smoothing || switchesChanged)
                updateNetworks();
        }

        double alpha = 0.0, beta = 0.0;

        // V1 into the tone stack / volume network
        v1.setCathodeInjection (lnfbCurrent);
        front.sourceCurrentAffine (alpha, beta);
        front.step (v1.processSampleLoaded (input, alpha, beta));
        const auto rev = (float) front.voltage (fREV);
        probes.rev = rev;

        // V4 into the filter network
        v4.setCathodeInjection (lnfb2Current);
        mid.sourceCurrentAffine (alpha, beta);
        mid.step (v4.processSampleLoaded ((float) front.voltage (fV4Grid), alpha, beta));
        lnfbCurrent = (float) mid.current (eLnfbR);
        probes.v4Plate = v4.getPlateVoltage();
        probes.u37Grid = (float) mid.voltage (mU37Grid);

        // U37 -> U38 (DC coupled)
        const auto vp37 = u37.processSampleLoaded ((float) mid.voltage (mU37Grid), 0.0, 0.0);
        u38.processSample (vp37);
        const auto dry = u38.getCathodeDeviation();
        probes.u37Plate = vp37;
        probes.dry = dry;
        lnfb2Current = lnfb2Filter.processHighpass (dry) / circuit::kLnfb2R;

        // reverb: U20 send -> Send pot -> driver/tank -> U28 -> Return pot -> U39 -> U40
        float wet = 0.0f;
        if (sendRatio > 0.0f || retRatio > 0.0f || ! tankQuiet())
        {
            u20.setCathodeInjection (tank.getSecondaryVoltage() / circuit::kTankFbR);
            const auto driverGrid = u20.processSample (rev) * sendRatio;
            returnAccum += tank.processSample (driverGrid);

            // U28 -> Return -> U39 -> U40 once every returnDecimation samples on the averaged tank
            // output; wet is interpolated between the last two results (delay <= 10 us)
            if (++returnPhase >= returnDecimation)
            {
                const auto tankOut = returnAccum / (float) returnDecimation;
                returnPhase = 0;
                returnAccum = 0.0f;

                u28.setCathodeInjection (revFbCurrent);
                const auto returned = u28.processSample (tankOut) * retRatio;
                const auto vp39 = u39.processSampleLoaded (returned, 0.0, 0.0);
                u40.processSample (vp39);
                wetPrev = wetNext;
                wetNext = u40.getCathodeDeviation();
                revFbCurrent = revFbFilter.processHighpass (wetNext) / circuit::kRevFbR;
            }

            wet = wetPrev + (wetNext - wetPrev) * (float) (returnPhase + 1) / (float) returnDecimation;
        }

        // mix node: two 220k from (near-ideal) CF outputs -> open-circuit voltage is the average
        return 0.5f * (dry + wet);
    }

    /** Node voltages of the last sample (for verification against SPICE). */
    struct Probes { float rev = 0, v4Plate = 0, u37Grid = 0, u37Plate = 0, dry = 0; };
    const Probes& getProbes() const noexcept { return probes; }

    /** DC operating points (for verification against SPICE). */
    struct OperatingPoint { float v1Plate, v1Cathode, v4Plate, v4Cathode, u37Plate, u38Cathode; };
    OperatingPoint getOperatingPoint() const noexcept
    {
        return { v1.getPlateVoltageDC(), v1.getCathodeVoltageDC(), v4.getPlateVoltageDC(), v4.getCathodeVoltageDC(),
                 u37.getPlateVoltageDC(), u38.getCathodeVoltageDC() };
    }

private:
    // front network nodes
    enum { fPlate, fTrebleTop, fTrebleBottom, fOut, fSlope, fBassTop, fBassWiper, fBassBottom, fMid, fREV, fDeepA, fDeepB, fBright, fV4Grid, fNumNodes };
    // mid network nodes
    enum { mPlate, mC6, mN030, mN055, mChoke, mN039, mU37Grid, mT1, mT2, mT3, mT4, mT5, mN040, mLowCommon, mLnfb, mNumNodes };

    void buildFrontTopology()
    {
        using namespace circuit;
        constexpr int g = decltype (front)::ground;
        front.setNumNodes (fNumNodes);
        front.setSourceNode (fPlate);

        front.addCapacitor (fPlate, fTrebleTop, kTsTrebleCap);                 // C1
        front.addResistor (fPlate, fSlope, kTsSlope);                          // R3
        front.addCapacitor (fSlope, fBassTop, kTsBassCap);                     // C3
        front.addCapacitor (fSlope, fMid, kTsMidCap);                          // C34
        eMid = front.addResistor (fMid, g, kTsMidPot * 0.5);                   // U33 wiper -> ground
        eTrebleTop = front.addResistor (fTrebleTop, fOut, kTsTreblePot * 0.5); // U2 A-wiper
        eTrebleBottom = front.addResistor (fOut, fTrebleBottom, kTsTreblePot * 0.5); // U2 wiper-B
        front.addCapacitor (fTrebleBottom, g, kTsTrebleFoot);                  // C35 (Guitar)
        eBassTop = front.addResistor (fBassTop, fBassWiper, kTsBassPot * 0.5); // U3 A-wiper
        eBassBottom = front.addResistor (fBassWiper, fBassBottom, kTsBassPot * 0.5);
        front.addResistor (fBassBottom, g, kTsBassFoot);                       // R7
        front.addResistor (fBassWiper, fOut, kTsBassMix);                      // R63 (Guitar)
        eVolTop = front.addResistor (fOut, fREV, kVolumePot * 0.5);            // U5 A-wiper
        eVolBottom = front.addResistor (fREV, g, kVolumePot * 0.5);            // U5 wiper-B
        front.addCapacitor (fOut, fBright, kBrightCap);                        // C4
        eBrightSw = front.addResistor (fBright, fREV, kSwitchClosed);          // U36
        eDeep1 = front.addResistor (fBassTop, fDeepA, kSwitchOpen);            // R64 via U35
        eDeep2 = front.addResistor (fREV, fDeepA, kSwitchOpen);                // R65 via U35
        front.addCapacitor (fDeepA, fDeepB, kDeepC);                           // C36
        front.addResistor (fDeepA, fDeepB, kDeepRpar);                         // R66
        front.addResistor (fDeepB, g, kDeepRfoot);                             // R67
        front.addResistor (fREV, fV4Grid, kV4Stopper);                         // R69
        front.addCapacitor (fV4Grid, g, kMiller5751);                          // V4 Miller capacitance
    }

    void buildMidTopology()
    {
        using namespace circuit;
        constexpr int g = decltype (mid)::ground;
        mid.setNumNodes (mNumNodes);
        mid.setSourceNode (mPlate);

        mid.addCapacitor (mPlate, mC6, kMidCoupling);             // C6
        mid.addCapacitor (mC6, mN030, kMidTrebleCap);             // C37
        mid.addResistor (mC6, mN055, kMidR53);                    // R53
        mid.addInductor (mN055, mChoke, kMidChoke);               // L2
        mid.addResistor (mChoke, mN039, kMidChokeR);              // L2 Rser
        mid.addResistor (mN039, mN030, kMidR34);                  // R34
        mid.addResistor (mN030, mU37Grid, kMidR70);               // R70
        mid.addCapacitor (mN030, g, kMidC44);                     // C44
        eHighShort = mid.addResistor (mN030, mU37Grid, kSwitchOpen); // High position 1
        eHighCap = mid.addCapacitor (mN030, mU37Grid, kHighCaps[3]); // High positions 2..7
        mid.addResistor (mU37Grid, g, kMidR71);                   // R71
        mid.addCapacitor (mU37Grid, g, kMiller7025);              // U37 Miller capacitance

        const int taps[8] = { mN039, mT1, mT2, mT3, mT4, mT5, mN040, g };
        for (int i = 0; i < 7; ++i)
            mid.addResistor (taps[i], taps[i + 1], kLowLadder[i]); // R72..R78
        mid.addCapacitor (mN039, mN040, kMidC45);                  // C45

        for (int i = 0; i < 7; ++i)
            eLow[i] = mid.addResistor (mLowCommon, taps[i], kSwitchOpen); // Low switch
        mid.addResistor (mLowCommon, mU37Grid, kMidR79);           // R79

        mid.addCapacitor (mPlate, mLnfb, kLnfbC);                  // C15
        eLnfbR = mid.addResistor (mLnfb, g, kLnfbR);               // R54 -> V1 cathode (~0 V)
    }

    void updateNetworks() noexcept
    {
        using namespace circuit;
        const auto t = potAudio (treble.getCurrentValue());
        const auto b = potAudio (bass.getCurrentValue());
        const auto v = potAudio (volume.getCurrentValue());

        front.setValue (eTrebleTop, kTsTreblePot * (1.0 - t));
        front.setValue (eTrebleBottom, kTsTreblePot * t);
        front.setValue (eBassTop, kTsBassPot * (1.0 - b));
        front.setValue (eBassBottom, kTsBassPot * b);
        front.setValue (eMid, kTsMidPot * potLinear (middle.getCurrentValue()));
        front.setValue (eVolTop, kVolumePot * (1.0 - v));
        front.setValue (eVolBottom, kVolumePot * v);
        front.setValue (eBrightSw, bright ? kSwitchClosed : kSwitchOpen);
        front.setValue (eDeep1, deep ? (double) kDeepR1 : kSwitchOpen);
        front.setValue (eDeep2, deep ? (double) kDeepR2 : kSwitchOpen);
        front.build();

        mid.setValue (eHighShort, high == 1 ? kSwitchClosed : kSwitchOpen);
        mid.setValue (eHighCap, high == 1 ? kHighCaps[1] : kHighCaps[high - 1]);
        for (int i = 0; i < 7; ++i)
            mid.setValue (eLow[i], i == low - 1 ? kSwitchClosed : kSwitchOpen);
        mid.build();

        sendRatio = (float) potAudio (send.getCurrentValue());
        retRatio = (float) potLinear (ret.getCurrentValue());
        if (send.getCurrentValue() <= 0.0f) sendRatio = 0.0f;
        if (ret.getCurrentValue() <= 0.0f)  retRatio = 0.0f;

        switchesChanged = false;
    }

    bool tankQuiet() const noexcept { return std::abs (revFbCurrent) < 1.0e-12f; }

    TriodeStage v1, v4, u37, u39, u20, u28;
    CathodeFollower u38, u40;
    SpringTank tank;
    LinearNetwork<fNumNodes, 26> front;
    LinearNetwork<mNumNodes, 34> mid;
    OnePole lnfb2Filter, revFbFilter;

    int eMid = 0, eTrebleTop = 0, eTrebleBottom = 0, eBassTop = 0, eBassBottom = 0, eVolTop = 0, eVolBottom = 0;
    int eBrightSw = 0, eDeep1 = 0, eDeep2 = 0, eHighShort = 0, eHighCap = 0, eLnfbR = 0;
    int eLow[7] {};

    juce::SmoothedValue<float> treble { 5.0f }, middle { 5.0f }, bass { 5.0f }, volume { 5.0f }, send { 0.0f }, ret { 0.0f };
    bool bright = true, deep = false, switchesChanged = true;
    int high = 4, low = 4;
    float sendRatio = 0.0f, retRatio = 0.0f;
    float lnfbCurrent = 0.0f, lnfb2Current = 0.0f, revFbCurrent = 0.0f;
    double sampleRate = 48000.0;
    int countdown = 0;
    int returnDecimation = 1, returnPhase = 0;
    float returnAccum = 0.0f, wetPrev = 0.0f, wetNext = 0.0f;
    Probes probes;
};
} // namespace dumble
