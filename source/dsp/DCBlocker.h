#pragma once

#include <juce_dsp/juce_dsp.h>

#include "OnePole.h"

namespace dumble
{
/** 1st-order DC blocking high-pass (ProcessorChain element). */
class DCBlocker
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        filter.setCutoff (cutoffHz, spec.sampleRate);
        reset();
    }

    void reset() noexcept { filter.reset(); }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept { return filter.processHighpass (x); });
    }

private:
    static constexpr float cutoffHz = 10.0f;
    OnePole filter;
};
} // namespace dumble
