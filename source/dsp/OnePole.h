#pragma once

#include <algorithm>
#include <cmath>

namespace dumble
{
/** Topology-preserving-transform one-pole filter (Zavalishin). Allocation free, noexcept. */
class OnePole
{
public:
    void setCutoff (float cutoffHz, double sampleRate) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        const auto nyquistSafe = std::min ((double) cutoffHz, 0.49 * sampleRate);
        const auto g = std::tan (pi * nyquistSafe / sampleRate);
        coeff = (float) (g / (1.0 + g));
    }

    void reset (float initialState = 0.0f) noexcept { state = initialState; }

    float processLowpass (float x) noexcept
    {
        const auto v = (x - state) * coeff;
        const auto y = v + state;
        state = y + v;
        return y;
    }

    float processHighpass (float x) noexcept { return x - processLowpass (x); }

    float getState() const noexcept { return state; }

private:
    float coeff = 0.0f;
    float state = 0.0f;
};

/** Process a mono ProcessContext sample by sample through a callable. */
template <typename Context, typename Fn>
inline void processMono (const Context& context, Fn&& fn) noexcept
{
    auto& out = context.getOutputBlock();
    const auto& in = context.getInputBlock();

    if (context.isBypassed)
    {
        if (context.usesSeparateInputAndOutputBlocks())
            out.copyFrom (in);
        return;
    }

    const auto numSamples = out.getNumSamples();
    const auto* x = in.getChannelPointer (0);
    auto* y = out.getChannelPointer (0);

    for (size_t i = 0; i < numSamples; ++i)
        y[i] = fn (x[i]);

    // Any extra channels mirror channel 0 (the engine itself runs mono).
    for (size_t ch = 1; ch < out.getNumChannels(); ++ch)
        out.getSingleChannelBlock (ch).copyFrom (out.getSingleChannelBlock (0));
}
} // namespace dumble
