#pragma once

#include <juce_dsp/juce_dsp.h>

#include "BrightVolume.h"
#include "CircuitConstants.h"
#include "NegativeFeedback.h"
#include "OnePole.h"
#include "PhaseInverter.h"
#include "PowerAmp6550.h"

namespace dumble
{
/**
    ProcessorChain element closing the global NFB loop around:
        mixer -> PhaseInverter (LTP) -> Master -> PowerAmp6550 (+OT) -> NegativeFeedback -> PI

    The loop is closed with a one-sample delay; at 2x..8x oversampling that delay is
    <= 10 us and its effect on loop phase is negligible in the audio band.
*/
class PowerSection
{
public:
    void setMaster (float knob0to10) noexcept   { master.setTargetValue (audioTaper (knob0to10)); }
    void setPresence (float knob0to10) noexcept { nfb.setPresence (knob0to10); }
    void setDeep (bool on) noexcept             { nfb.setDeep (on); }

    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        pi.prepare();
        power.prepare (spec.sampleRate);
        nfb.prepare (spec.sampleRate);
        inputCoupling.setCutoff (8.0f, spec.sampleRate);
        master.reset (spec.sampleRate, 0.03);
        reset();
    }

    void reset() noexcept
    {
        power.reset();
        nfb.reset();
        inputCoupling.reset();
        master.setCurrentAndTargetValue (master.getTargetValue());
        lastFeedback = 0.0f;
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    float processSample (float x) noexcept
    {
        const auto in = inputCoupling.processHighpass (x) * circuit::kMixerGain;
        // grid A -> speaker is inverting (y = -G * e), and the feedback enters the LTP's other grid
        // with the speaker winding phased for negative feedback: e = in - (-beta * y) = in + fb.
        const auto drive = pi.processSample (in + lastFeedback);
        const auto m = master.getNextValue();
        const auto y = power.processSample (drive.a * m, drive.b * m);
        lastFeedback = nfb.processSample (y);
        return y;
    }

    const PowerAmp6550& getPowerAmp() const noexcept { return power; }

private:
    PhaseInverter pi;
    PowerAmp6550 power;
    NegativeFeedback nfb;
    OnePole inputCoupling;
    juce::SmoothedValue<float> master { 1.0f };
    float lastFeedback = 0.0f;
};
} // namespace dumble
