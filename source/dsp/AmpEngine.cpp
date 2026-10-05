#include "AmpEngine.h"

namespace dumble
{
AmpEngine::AmpEngine()
{
    for (auto& chain : amp)
    {
        chain.get<v1a>().setCircuit (circuit::kV1a, k12AX7);
        chain.get<v1b>().setCircuit (circuit::kV1b, k12AX7);
        chain.get<v2a>().setCircuit (circuit::kV2a, k12AX7);
    }
}

void AmpEngine::prepare (double sampleRate, int maximumBlockSize, int initialOversamplingIndex)
{
    maxBlockSize = juce::jmax (1, maximumBlockSize);
    const juce::dsp::ProcessSpec baseSpec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    pre.prepare (baseSpec);
    pre.get<0>().setRampDurationSeconds (0.05);
    pre.get<1>().coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 20.0f);

    post.prepare (baseSpec);
    post.get<outputGain>().setRampDurationSeconds (0.05);

    for (size_t i = 0; i < (size_t) kNumOversamplingChoices; ++i)
    {
        const auto factorLog2 = i + 1; // 2x, 4x, 8x

        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
            1, factorLog2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true /* max quality */, true /* integer latency */);
        oversamplers[i]->initProcessing ((size_t) maxBlockSize);

        const juce::dsp::ProcessSpec osSpec { sampleRate * (double) (1 << factorLog2),
                                              (juce::uint32) (maxBlockSize << factorLog2), 1 };
        amp[i].prepare (osSpec);

        latencies[i].store ((int) std::lround (oversamplers[i]->getLatencyInSamples()), std::memory_order_relaxed);
    }

    const auto initial = juce::jlimit (0, kNumOversamplingChoices - 1, initialOversamplingIndex);
    activeOs.store (initial, std::memory_order_relaxed);
    requestedOs = initial;
    transition = Transition::none;
    prepared = true;

    reset();
}

void AmpEngine::reset() noexcept
{
    pre.reset();
    post.reset();

    for (size_t i = 0; i < (size_t) kNumOversamplingChoices; ++i)
    {
        if (oversamplers[i] != nullptr)
            oversamplers[i]->reset();

        amp[i].reset();
    }
}

void AmpEngine::setSettings (const AmpSettings& s) noexcept
{
    pre.get<0>().setGainDecibels (s.inputGainDb);

    for (auto& chain : amp)
    {
        auto& vol = chain.get<volumeStage>();
        vol.setVolume (s.volume);
        vol.setBright (s.bright);

        auto& ts = chain.get<toneStack>();
        ts.setTreble (s.treble);
        ts.setMiddle (s.middle);
        ts.setBass (s.bass);

        auto& pwr = chain.get<powerSection>();
        pwr.setMaster (s.master);
        pwr.setPresence (s.presence);
        pwr.setDeep (s.deep);
    }

    post.setBypassed<cabinet> (! s.cabOn);
    post.get<outputGain>().setGainDecibels (s.outputDb);

    requestedOs = juce::jlimit (0, kNumOversamplingChoices - 1, s.oversamplingIndex);
}

int AmpEngine::getLatencySamples (int oversamplingIndex) const noexcept
{
    return latencies[(size_t) juce::jlimit (0, kNumOversamplingChoices - 1, oversamplingIndex)].load (std::memory_order_relaxed);
}

void AmpEngine::process (float* samples, int numSamples) noexcept DUMBLE_NONBLOCKING
{
    jassert (prepared);

    // Hosts may exceed the size announced in prepareToPlay: sub-block instead of reallocating.
    for (int offset = 0; offset < numSamples; offset += maxBlockSize)
        processChunk (samples + offset, juce::jmin (maxBlockSize, numSamples - offset));
}

void AmpEngine::processChunk (float* samples, int numSamples) noexcept DUMBLE_NONBLOCKING
{
    float* channels[] = { samples };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);

    // oversampling switch state machine (no allocation: every path was prepared up front)
    auto active = activeOs.load (std::memory_order_relaxed);

    if (transition == Transition::fadingOut)
    {
        active = requestedOs;
        oversamplers[(size_t) active]->reset();
        amp[(size_t) active].reset();
        activeOs.store (active, std::memory_order_relaxed);
        transition = Transition::fadingIn;
    }
    else if (transition == Transition::fadingIn)
    {
        transition = Transition::none;
    }

    if (transition == Transition::none && requestedOs != active)
        transition = Transition::fadingOut;

    pre.process (context);

    auto& os = *oversamplers[(size_t) active];
    auto upBlock = os.processSamplesUp (block);
    amp[(size_t) active].process (juce::dsp::ProcessContextReplacing<float> (upBlock));
    os.processSamplesDown (block);

    if (transition == Transition::fadingOut)
        applyRamp (samples, numSamples, 1.0f, 0.0f);
    else if (transition == Transition::fadingIn)
        applyRamp (samples, numSamples, 0.0f, 1.0f);

    post.process (context);

    // last line of defence: never hand NaN/Inf or absurd levels to the interface
    for (int i = 0; i < numSamples; ++i)
    {
        const auto v = samples[i];
        samples[i] = std::isfinite (v) ? juce::jlimit (-4.0f, 4.0f, v) : 0.0f;
    }
}

void AmpEngine::applyRamp (float* samples, int numSamples, float from, float to) noexcept
{
    const auto step = (to - from) / (float) juce::jmax (1, numSamples);
    auto g = from;

    for (int i = 0; i < numSamples; ++i)
    {
        g += step;
        samples[i] *= g;
    }
}
} // namespace dumble
