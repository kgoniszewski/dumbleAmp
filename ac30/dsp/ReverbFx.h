#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Ac30Constants.h"
#include "NodalNetwork.h"
#include "PortSolver.h"
#include "dsp/OnePole.h"
#include "dsp/Pots.h"
#include "dsp/SpringTank.h"

namespace ac30
{
/** Reverb front-panel settings. */
struct ReverbControls
{
    float level = 0.0f, tone = 5.0f;
    bool on = true; // footswitch (Q1 J174 mutes the return when off)
};

/**
    AC30C2 Rev/FX board, U1B output to the phase-inverter input (ProcessorChain element).

      FX loop (bypassed): send divider -10 dB, return amp U1A x3.8           -> F (dry)
      driver:  F -> R42/C37 high-pass -> U2B x59 -> 6 x op-amp buffers || 47R -> tank (600 R in)
      tank:    3-spring 9EB2C1B / BL3EB3C1B, modelled with dumble::SpringTank
      recovery U5A: 1 + (R77 || C55) / (R76 + C56) -> R78 33k -> Level VR6 / Tone VR5
      sum:     dry R85 56k + wet R84 56k into R83 22k -> U5B x4.9           -> phase inverter

    Op-amp stages are linear filters built from their component values with a soft clip at the
    +/-27 V rails. The passive Level/Tone/sum section is a NodalNetwork with two sources.
*/
class ReverbFx
{
public:
    static constexpr int kUpdateInterval = 32;

    ReverbFx() { buildMixer(); }

    void setControls (const ReverbControls& c) noexcept
    {
        level.setTargetValue (c.level);
        tone.setTargetValue (c.tone);
        on.setTargetValue (c.on ? 1.0f : 0.0f);
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        using namespace circuit;
        const auto fs = spec.sampleRate;
        fxHighpass.setCutoff (kFxHighpassHz, fs);
        driverHighpass.setCutoff (kDriverHighpassHz, fs);
        recoveryHighpass.setCutoff (kRecoveryHighpassHz, fs);
        recoveryLowpass.setCutoff (kRecoveryLowpassHz, fs);
        tank.prepare ({ fs, spec.maximumBlockSize, 1 });

        level.reset (fs, 0.03);
        tone.reset (fs, 0.03);
        on.reset (fs, 0.02);
        mixer.prepare (fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto* f : { &fxHighpass, &driverHighpass, &recoveryHighpass, &recoveryLowpass })
            f->reset();
        tank.reset();
        for (auto* s : { &level, &tone, &on })
            s->setCurrentAndTargetValue (s->getTargetValue());
        update();
        mixer.reset();
        countdown = 0;
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        dumble::processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    float processSample (float mixerVolts) noexcept
    {
        using namespace circuit;

        if (--countdown <= 0)
        {
            countdown = kUpdateInterval;
            if (level.isSmoothing() || tone.isSmoothing())
            {
                level.skip (kUpdateInterval);
                tone.skip (kUpdateInterval);
                update();
            }
        }

        // FX loop in bypass: unity-ish through the send divider and the return amp
        const auto dry = opAmpClip (kFxGain * fxHighpass.processHighpass (mixerVolts), kOpAmpSwing);

        float recovered = 0.0f;
        const auto gate = on.getNextValue();
        if (levelRatio > 0.0 || ! tank.isQuiet())
        {
            const auto drive = opAmpClip (kDriverGain * driverHighpass.processHighpass (dry), kOpAmpSwing);
            const auto tankOut = kTankOutput * tank.processSample (kTankDrive * drive);
            const auto band = recoveryLowpass.processLowpass (recoveryHighpass.processHighpass (tankOut));
            recovered = gate * opAmpClip (tankOut + kRecoveryGain * band, kOpAmpSwing);
        }

        mixer.setSource (sWet, recovered);
        mixer.setSource (sDry, dry);
        mixer.step();
        return opAmpClip (kSumGain * (float) mixer.voltage (nSum), kOpAmpSwing);
    }

private:
    enum { nRec, nL, nW, nT2, nDry, nSum, nCount };

    void buildMixer()
    {
        using namespace circuit;
        constexpr int g = decltype (mixer)::ground;
        mixer.setNumNodes (nCount);
        mixer.addResistor (nRec, nL, kR78);                    // R78
        eLevelTop = mixer.addResistor (nL, nW, kVR6 * 0.5);    // VR6 Level
        eLevelBottom = mixer.addResistor (nW, g, kVR6 * 0.5);
        eTone = mixer.addResistor (nW, nT2, kVR5 * 0.5);       // VR5 Tone || C47, in series with C49
        mixer.addCapacitor (nW, nT2, kC47);
        mixer.addCapacitor (nT2, g, kC49);
        mixer.addResistor (nW, nSum, kR84);                    // R84
        mixer.addResistor (nDry, nSum, kR85);                  // R85
        mixer.addResistor (nSum, g, kR83);                     // R83
        sWet = mixer.addVoltageSource (nRec);
        sDry = mixer.addVoltageSource (nDry);
    }

    void update() noexcept
    {
        using namespace circuit;
        const auto l = level.getCurrentValue() <= 0.0f ? 1.0e-6 : dumble::potLinear (level.getCurrentValue());
        levelRatio = level.getCurrentValue() <= 0.0f ? 0.0 : l;
        mixer.setValue (eLevelTop, kVR6 * (1.0 - l));
        mixer.setValue (eLevelBottom, kVR6 * l);
        mixer.setValue (eTone, 100.0 + kVR5 * dumble::potAudio (tone.getCurrentValue()));
        mixer.build();
    }

    dumble::OnePole fxHighpass, driverHighpass, recoveryHighpass, recoveryLowpass;
    dumble::SpringTank tank;
    NodalNetwork<nCount, 10, 2> mixer;
    int eLevelTop = 0, eLevelBottom = 0, eTone = 0, sWet = 0, sDry = 0;
    juce::SmoothedValue<float> level { 0.0f }, tone { 5.0f }, on { 1.0f };
    double levelRatio = 0.0;
    int countdown = 0;
};
} // namespace ac30
