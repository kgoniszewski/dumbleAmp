#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "OnePole.h"

namespace dumble
{
/** Audio-taper approximation used for the "A" pots (knob 0..10 -> wiper fraction 0..1). */
inline float audioTaper (float knob0to10) noexcept
{
    const auto x = juce::jlimit (0.0f, 1.0f, knob0to10 * 0.1f);
    return (std::pow (10.0f, 2.0f * x) - 1.0f) / 99.0f;
}

/**
    Volume pot (1M, audio taper) with a bright cap across the upper section:

        in --[Rt || Cb]--+-- out
                         Rb
                         |
                        gnd

    H(s) = Rb (1 + s Cb Rt) / (Rt + Rb + s Cb Rt Rb), discretised with the bilinear transform.
    Coefficients are recomputed from smoothed parameter values in small sub-blocks only while
    a parameter is moving (no allocation, no zipper noise).
*/
class BrightVolume
{
public:
    static constexpr int kCoefficientUpdateInterval = 16;

    void setVolume (float knob0to10) noexcept   { volume.setTargetValue (knob0to10); }
    void setBright (bool on) noexcept           { bright.setTargetValue (on ? 1.0f : 0.0f); }

    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        sampleRate = spec.sampleRate;
        volume.reset (sampleRate, 0.03);
        bright.reset (sampleRate, 0.03);
        updateCoefficients (volume.getCurrentValue(), bright.getCurrentValue());
        reset();
    }

    void reset() noexcept
    {
        x1 = y1 = 0.0f;
        volume.setCurrentAndTargetValue (volume.getTargetValue());
        bright.setCurrentAndTargetValue (bright.getTargetValue());
        updateCoefficients (volume.getCurrentValue(), bright.getCurrentValue());
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept
        {
            if ((volume.isSmoothing() || bright.isSmoothing()) && --countdown <= 0)
            {
                countdown = kCoefficientUpdateInterval;
                updateCoefficients (volume.getNextValue(), bright.getNextValue());
                volume.skip (kCoefficientUpdateInterval - 1);
                bright.skip (kCoefficientUpdateInterval - 1);
            }

            const auto y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x;
            y1 = y;
            return y;
        });
    }

private:
    void updateCoefficients (float knob, float brightAmount) noexcept
    {
        const auto alpha = audioTaper (knob);
        const auto rb = juce::jmax (alpha * circuit::kVolumePot, 1.0f);
        const auto rt = juce::jmax ((1.0f - alpha) * circuit::kVolumePot, 1.0f);
        const auto c  = circuit::kBrightCap * brightAmount;
        const auto k  = (float) (2.0 * sampleRate);

        const auto nb0 = rb * (1.0f + c * rt * k);
        const auto nb1 = rb * (1.0f - c * rt * k);
        const auto na0 = rt + rb + c * rt * rb * k;
        const auto na1 = rt + rb - c * rt * rb * k;

        b0 = nb0 / na0;
        b1 = nb1 / na0;
        a1 = na1 / na0;
    }

    double sampleRate = 48000.0;
    juce::SmoothedValue<float> volume { 5.0f }, bright { 1.0f };
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
    int countdown = 0;
};
} // namespace dumble
