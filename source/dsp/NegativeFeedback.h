#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "OnePole.h"

namespace dumble
{
/**
    Global negative feedback network: speaker winding -> divider -> PI feedback grid.

    Presence shunts the high-frequency part of the feedback signal to ground (less NFB at the top
    -> treble lift), Deep removes the low-frequency part (less NFB at the bottom -> bass lift).
*/
class NegativeFeedback
{
public:
    void setPresence (float knob0to10) noexcept { presence.setTargetValue (juce::jlimit (0.0f, 1.0f, knob0to10 * 0.1f)); }
    void setDeep (bool on) noexcept             { deep.setTargetValue (on ? 0.85f : 0.0f); }

    void prepare (double sampleRate) noexcept
    {
        presenceFilter.setCutoff (circuit::kPresenceHz, sampleRate);
        deepFilter.setCutoff (circuit::kDeepHz, sampleRate);
        presence.reset (sampleRate, 0.03);
        deep.reset (sampleRate, 0.05);
        reset();
    }

    void reset() noexcept
    {
        presenceFilter.reset();
        deepFilter.reset();
        presence.setCurrentAndTargetValue (presence.getTargetValue());
        deep.setCurrentAndTargetValue (deep.getTargetValue());
    }

    /** Normalised speaker signal -> feedback voltage at the PI feedback grid. */
    float processSample (float speaker) noexcept
    {
        const auto v = speaker * circuit::kSpeakerPeakVolts * circuit::kNfbRatio;
        const auto hf = presenceFilter.processHighpass (v);
        const auto lf = deepFilter.processLowpass (v);
        return v - presence.getNextValue() * hf - deep.getNextValue() * lf;
    }

private:
    juce::SmoothedValue<float> presence { 0.5f }, deep { 0.0f };
    OnePole presenceFilter, deepFilter;
};
} // namespace dumble
