#pragma once

#include <juce_dsp/juce_dsp.h>

namespace dumble
{
/**
    Speaker cabinet stage: thin wrapper around juce::dsp::Convolution (ProcessorChain element).

    Threading: loadImpulseResponse() on juce::dsp::Convolution is wait-free; the IR is prepared
    (resampled, normalised, partitioned) on JUCE's background thread and swapped in without
    blocking the audio thread. The load* methods here must be called from the message thread,
    never from processBlock().

    Level: with Normalise::yes, juce::dsp::Convolution scales the IR to an energy of 0.125^2 (-18 dB)
    *after* resampling it to the processing rate. Two consequences, both undone by outputGain:
      - the cabinet would sit 18 dB below the amp (x8 restores unit IR energy), and
      - its gain would grow by 3 dB per doubling of the sample rate (more taps of the same
        energy-normalised amplitude): sqrt(kDefaultIrRate / fs).
    Result: unit IR energy at 48 kHz, the same level at every rate, for the built-in and user IRs.
*/
class CabinetIR
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        convolution.prepare (spec);
        outputGain = kNormalisationMakeUp * (float) std::sqrt (kDefaultIrRate / spec.sampleRate);
    }

    void reset() noexcept { convolution.reset(); }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        convolution.process (context);

        if (! context.isBypassed)
            context.getOutputBlock().multiplyBy (outputGain);
    }

    /** Loads a built-in, procedurally generated 2x12 open-back style response. Message thread only. */
    void loadDefaultImpulseResponse()
    {
        convolution.loadImpulseResponse (createDefaultImpulseResponse (kDefaultIrRate), kDefaultIrRate,
                                         juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::yes);
    }

    /** Loads a user IR (wav/aiff). Message thread only. */
    void loadImpulseResponse (const juce::File& file)
    {
        convolution.loadImpulseResponse (file,
                                         juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::yes,
                                         0,
                                         juce::dsp::Convolution::Normalise::yes);
    }

    int getLatency() const { return convolution.getLatency(); }

    static juce::AudioBuffer<float> createDefaultImpulseResponse (double sampleRate)
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;

        constexpr int length = 2048;
        juce::AudioBuffer<float> ir (1, length);
        ir.clear();
        ir.setSample (0, 0, 1.0f);

        // Broad strokes of a 2x12 with ceramic speakers: low resonance, mid dip, upper-mid
        // presence peak, steep cone break-up roll-off.
        const juce::dsp::IIR::Coefficients<float>::Ptr stages[] {
            Coeffs::makeHighPass (sampleRate, 75.0f, 1.1f),
            Coeffs::makePeakFilter (sampleRate, 115.0f, 1.2f, juce::Decibels::decibelsToGain (4.0f)),
            Coeffs::makePeakFilter (sampleRate, 450.0f, 0.8f, juce::Decibels::decibelsToGain (-3.5f)),
            Coeffs::makePeakFilter (sampleRate, 2400.0f, 1.4f, juce::Decibels::decibelsToGain (5.0f)),
            Coeffs::makeLowPass (sampleRate, 5200.0f, 0.9f),
            Coeffs::makeLowPass (sampleRate, 6500.0f, 0.7f),
        };

        auto* data = ir.getWritePointer (0);

        for (const auto& c : stages)
        {
            juce::dsp::IIR::Filter<float> f (c);
            for (int i = 0; i < length; ++i)
                data[i] = f.processSample (data[i]);
        }

        // short fade-out to avoid truncation clicks
        constexpr int fade = 256;
        for (int i = 0; i < fade; ++i)
            data[length - fade + i] *= 1.0f - (float) i / (float) fade;

        return ir;
    }

private:
    static constexpr double kDefaultIrRate = 48000.0;
    juce::dsp::Convolution convolution;
    static constexpr float kNormalisationMakeUp = 8.0f; // 1 / 0.125, see the class comment
    float outputGain = 1.0f;
};
} // namespace dumble
