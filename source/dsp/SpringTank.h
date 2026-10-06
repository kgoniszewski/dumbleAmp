#pragma once

#include <array>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "OnePole.h"

namespace dumble
{
/**
    Reverb driver + spring tank of the SSS #002 (U25/U26 2x5751 in parallel into the reverb
    transformer, 8 Ohm tank input; tank output into the U28 recovery stage).

        driver grid volts -> 5751 pair + transformer (soft saturation) -> secondary volts
                          -> 2-spring tank -> tank output volts

    Spring model after Valimaki, Parker & Abel ("Parametric spring reverberation effect",
    JAES 2010), simplified: per spring, a cascade of stretched first-order allpasses (z^-K) gives the
    dispersive "chirp", inside a damped feedback delay loop.

    The tank's bandwidth is ~4.5 kHz, so it runs at a decimated internal rate (~40-80 kHz) at every
    oversampling factor: 4th-order anti-alias low-pass, keep every D-th sample, sample-and-hold back
    up, same 4th-order low-pass. All buffers are sized in prepare().
*/
class SpringTank
{
public:
    static constexpr int kAllpassStages = 40;
    static constexpr int kMaxStretch = 8;

    // Driver: the 5751 pair cuts off ~2.5 V below its bias; its plate swing through the 2 H : 0.5 mH
    // transformer (ratio 1:63) gives ~1.6 V at the tank input. The reconstruction models the tank as
    // out = in/3 delayed; real tanks are ~-25 dB, so the output is scaled to 0.1 of the drive.
    static constexpr float kDriverSaturation = 2.5f;  // grid volts
    static constexpr float kSecondaryPeak    = 1.6f;  // volts at the tank input
    static constexpr float kTankOutputGain   = 0.1f;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        decimation = juce::jmax (1, (int) (sampleRate / 40000.0));
        const auto internalRate = sampleRate / (double) decimation;

        for (auto* f : { &antiAlias, &reconstruction })
            for (auto& section : *f)
                section.filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 4500.0f, section.q);

        // ~4.4 kHz chirp transition -> stretch factor K
        const auto stretch = juce::jlimit (1, kMaxStretch, (int) (internalRate / (2.0 * 4400.0)));

        const double loopSeconds[] = { 0.0563, 0.0687 };
        for (size_t s = 0; s < springs.size(); ++s)
            springs[s].prepare (internalRate, loopSeconds[s], stretch);

        outputHighpass.setCutoff (120.0f, internalRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto* f : { &antiAlias, &reconstruction })
            for (auto& section : *f)
                section.reset();

        for (auto& s : springs)
            s.reset();

        outputHighpass.reset();
        phase = 0;
        held = 0.0f;
        secondary = 0.0f;
    }

    /** Driver grid volts in, tank output volts out (at the recovery grid). */
    float processSample (float driverGrid) noexcept
    {
        if (std::abs (driverGrid) < 1.0e-9f && tailIsSilent())
        {
            secondary = 0.0f;
            return 0.0f;
        }

        secondary = kSecondaryPeak * std::tanh (driverGrid / kDriverSaturation);

        auto band = secondary;
        for (auto& section : antiAlias)
            band = section.processSample (band);

        if (++phase >= decimation)
        {
            phase = 0;
            auto wet = 0.5f * (springs[0].process (band) + springs[1].process (band));
            held = outputHighpass.processHighpass (wet);
        }

        auto wet = held;
        for (auto& section : reconstruction)
            wet = section.processSample (wet);

        return kTankOutputGain * wet;
    }

    /** Transformer secondary (tank input) voltage of the last sample: fed back to U20 via R41. */
    float getSecondaryVoltage() const noexcept { return secondary; }

    /** True once the tank's tail has decayed to silence (processing may then be skipped). */
    bool isQuiet() const noexcept { return tailIsSilent(); }

private:
    bool tailIsSilent() const noexcept
    {
        return std::abs (held) < 1.0e-7f && springs[0].isQuiet() && springs[1].isQuiet();
    }

    class Spring
    {
    public:
        void prepare (double rate, double loopSeconds, int stretchFactor)
        {
            stretch = stretchFactor;
            loop.assign ((size_t) juce::jmax (1, (int) (loopSeconds * rate)), 0.0f);
            damping.setCutoff (3800.0f, rate);
            reset();
        }

        void reset() noexcept
        {
            std::fill (loop.begin(), loop.end(), 0.0f);
            for (auto& s : stages)
            {
                s.x.fill (0.0f);
                s.y.fill (0.0f);
            }
            writePos = 0;
            stagePos = 0;
            damping.reset();
            energy = 0.0f;
        }

        float process (float input) noexcept
        {
            const auto delayed = loop[(size_t) writePos];
            auto v = input + kFeedback * damping.processLowpass (delayed);

            // dispersive chirp: H(z) = (a + z^-K) / (1 + a z^-K), cascaded
            for (auto& s : stages)
            {
                const auto xk = s.x[(size_t) stagePos];
                const auto yk = s.y[(size_t) stagePos];
                const auto y = kAllpassCoeff * v + xk - kAllpassCoeff * yk;
                s.x[(size_t) stagePos] = v;
                s.y[(size_t) stagePos] = y;
                v = y;
            }

            stagePos = (stagePos + 1) % stretch;
            loop[(size_t) writePos] = v;
            writePos = (writePos + 1) % (int) loop.size();

            energy = 0.999f * energy + 0.001f * v * v;
            return v;
        }

        bool isQuiet() const noexcept { return energy < 1.0e-14f; }

    private:
        static constexpr float kAllpassCoeff = 0.62f;
        static constexpr float kFeedback = -0.72f;

        struct Stage
        {
            std::array<float, kMaxStretch> x {}, y {};
        };

        std::array<Stage, kAllpassStages> stages {};
        std::vector<float> loop;
        OnePole damping;
        int stretch = 1, writePos = 0, stagePos = 0;
        float energy = 0.0f;
    };

    struct Section
    {
        float q;
        juce::dsp::IIR::Filter<float> filter {};

        float processSample (float x) noexcept { return filter.processSample (x); }
        void reset() noexcept { filter.reset(); }
    };

    // 4th-order Butterworth = two biquads with Q 0.541 / 1.307
    std::array<Section, 2> antiAlias { Section { 0.5412f }, Section { 1.3066f } };
    std::array<Section, 2> reconstruction { Section { 0.5412f }, Section { 1.3066f } };

    std::array<Spring, 2> springs;
    OnePole outputHighpass;
    double sampleRate = 48000.0;
    int decimation = 1, phase = 0;
    float held = 0.0f, secondary = 0.0f;
};
} // namespace dumble
