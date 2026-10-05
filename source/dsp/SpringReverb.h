#pragma once

#include <array>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "BrightVolume.h"
#include "OnePole.h"

namespace dumble
{
/**
    Tube-driven spring reverb, mixed back in at the dry/reverb mixing node in front of V2a
    (ProcessorChain element; dry path is untouched, Reverb = 0 is bit-transparent).

        dry ───────────────────────────────────────────────┬──> out
         └─ driver (12AT7 + transformer: soft saturation)   │
              └─ 2-spring tank ─ recovery ─ × Reverb knob ──┘

    Spring model after Välimäki, Parker & Abel ("Parametric spring reverberation effect",
    JAES 2010), simplified: per spring, a cascade of stretched first-order allpasses
    (z^-K) produces the characteristic dispersive "chirp", inside a damped feedback delay loop.

    The tank's bandwidth is ~4.5 kHz, so it runs at a decimated internal rate (~40-80 kHz)
    even when the amp runs at 8x: 4th-order anti-alias low-pass, keep every D-th sample,
    sample-and-hold back up, same 4th-order low-pass. All buffers sized in prepare().
*/
class SpringReverb
{
public:
    static constexpr int kAllpassStages = 40;
    static constexpr int kMaxStretch = 8;

    void setAmount (float knob0to10) noexcept { amount.setTargetValue (audioTaper (knob0to10)); }

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
        amount.reset (sampleRate, 0.05);
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
        amount.setCurrentAndTargetValue (amount.getTargetValue());
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    float processSample (float dry) noexcept
    {
        const auto mix = amount.getNextValue();

        if (mix <= 0.0f && ! amount.isSmoothing() && tailIsSilent())
            return dry;

        // driver: 12AT7 into the reverb transformer, saturating on hot signals
        auto band = dry;
        for (auto& section : antiAlias)
            band = section.processSample (band);

        if (++phase >= decimation)
        {
            phase = 0;
            const auto driven = std::tanh (band * kDriveGain);
            auto wet = 0.5f * (springs[0].process (driven) + springs[1].process (driven));
            wet = outputHighpass.processHighpass (wet);
            held = wet;
        }

        auto wet = held;
        for (auto& section : reconstruction)
            wet = section.processSample (wet);

        return dry + mix * kRecoveryGain * wet;
    }

private:
    static constexpr float kDriveGain = 0.08f;   // volts at the stack output -> driver grid
    static constexpr float kRecoveryGain = 14.0f; // recovery stage, back to volts at the mixer

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
    juce::SmoothedValue<float> amount { 0.0f };
    double sampleRate = 48000.0;
    int decimation = 1, phase = 0;
    float held = 0.0f;
};
} // namespace dumble
