#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "OnePole.h"
#include "PhaseInverter.h"
#include "PowerAmp6L6.h"

namespace dumble
{
/**
    ProcessorChain element: LTP phase inverter -> cathode-follower driver -> 4x6L6GC -> OT,
    with the global negative feedback (speaker -> R20 2.7k -> R8 270R at the bottom of the PI tail,
    which is also the AC reference of the second PI grid through C8).

    Input: AC voltage at the first PI grid (from the master network). Output: speaker voltage
    normalised to kSpeakerFullScale.

    The loop is closed with a one-sample delay. To keep that delay short enough for the loop's
    phase margin (it oscillates at 96 kHz otherwise), the section always runs at >= 176.4 kHz
    internally: at 2x oversampling each sample is split into sub-steps (linearly interpolated
    input, averaged output).
*/
class PowerSection
{
public:
    static constexpr double kMinInternalRate = 176400.0;

    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        subSteps = juce::jmax (1, (int) std::ceil (kMinInternalRate / spec.sampleRate - 1.0e-9));
        pi.prepare();
        power.prepare (spec.sampleRate * subSteps);
        reset();
    }

    void reset() noexcept
    {
        power.reset();
        lastFeedback = 0.0f;
        lastInput = 0.0f;
    }

    int getSubSteps() const noexcept { return subSteps; }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    float processSample (float gridA) noexcept
    {
        if (subSteps == 1)
            return step (gridA);

        float sum = 0.0f;
        for (int k = 1; k <= subSteps; ++k)
            sum += step (lastInput + (gridA - lastInput) * (float) k / (float) subSteps);

        lastInput = gridA;
        return sum / (float) subSteps;
    }

    const PowerAmp6L6& getPowerAmp() const noexcept { return power; }

private:
    float step (float gridA) noexcept
    {
        // differential drive: grid A minus the feedback voltage on grid B / tail
        const auto drive = pi.processSample (gridA - lastFeedback);
        const auto speaker = power.processSample (drive.a, drive.b);
        lastFeedback = sign * circuit::kNfbRatio * speaker;
        return speaker / circuit::kSpeakerFullScale;
    }

public:

    /** Feedback polarity. Grid A -> speaker is inverting here (y = -G e), so the secondary is phased to
        feed back -beta * y, which makes the loop negative. Exposed for tests (0 opens the loop). */
    void setFeedbackSign (float s) noexcept { sign = s; }

private:
    PhaseInverter pi;
    PowerAmp6L6 power;
    float lastFeedback = 0.0f, lastInput = 0.0f;
    float sign = -1.0f;
    int subSteps = 1;
};
} // namespace dumble
