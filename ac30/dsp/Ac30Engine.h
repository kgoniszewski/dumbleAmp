#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_dsp/juce_dsp.h>

#include "PowerSectionAC30.h"
#include "PreampAC30.h"
#include "ReverbFx.h"
#include "dsp/CabinetIR.h"
#include "dsp/DCBlocker.h"
#include "dsp/RealtimeSafety.h"

namespace ac30
{
/** Everything the audio thread needs from the parameters for one block (plain values, no strings). */
struct Ac30Settings
{
    float inputGainDb = 0.0f;
    PreampControls preamp {};
    ReverbControls reverb {};
    PowerControls power {};
    float outputDb = -6.0f;
    bool cabOn = true;
    int oversamplingIndex = 1; // 0 = 2x, 1 = 4x, 2 = 8x
};

/**
    The complete mono AC30C2:

      PreChain  (base rate)  : input gain -> 20 Hz HPF
      AmpChain  (oversampled): PreampAC30 (V1, Normal / Top Boost, V2 + stack, U1B mixer)
                               -> ReverbFx (FX loop bypass, reverb driver / tank / recovery, U5B sum)
                               -> PowerSectionAC30 (V3 LTP, Tone Cut, Master, tremolo, 4 x EL84, OT, B+)
      PostChain (base rate)  : DC blocker -> cabinet IR -> output gain

    Three Oversampling objects (2x/4x/8x) and three AmpChains are prepared up front, so switching the
    factor on the audio thread never allocates (fade out one block, reset, fade in the next), as in
    the Dumble SSS engine (source/dsp/AmpEngine.cpp).
*/
class Ac30Engine
{
public:
    static constexpr int kNumOversamplingChoices = 3;

    using PreChain  = juce::dsp::ProcessorChain<juce::dsp::Gain<float>, juce::dsp::IIR::Filter<float>>;
    using AmpChain  = juce::dsp::ProcessorChain<PreampAC30, ReverbFx, PowerSectionAC30>;
    using PostChain = juce::dsp::ProcessorChain<dumble::DCBlocker, dumble::CabinetIR, juce::dsp::Gain<float>>;

    enum AmpIndex  { preamp, reverb, powerSection };
    enum PostIndex { dcBlocker, cabinet, outputGain };

    /** Allocates everything. Call from prepareToPlay() only. */
    void prepare (double sampleRate, int maximumBlockSize, int initialOversamplingIndex);
    void reset() noexcept;

    /** Audio thread: forwards parameter targets to all processors. */
    void setSettings (const Ac30Settings& settings) noexcept;

    /** Audio thread: processes a mono block in place. Any length is accepted (sub-blocked). */
    void process (float* samples, int numSamples) noexcept DUMBLE_NONBLOCKING;

    /** Latency (integer samples at base rate) for a given oversampling choice. Any thread. */
    int getLatencySamples (int oversamplingIndex) const noexcept;
    int getActiveOversamplingIndex() const noexcept { return activeOs.load (std::memory_order_relaxed); }

    dumble::CabinetIR& getCabinet() noexcept { return post.get<cabinet>(); }
    const AmpChain& getAmpChain (int index) const noexcept { return amp[(size_t) index]; }

private:
    void processChunk (float* samples, int numSamples) noexcept DUMBLE_NONBLOCKING;
    static void applyRamp (float* samples, int numSamples, float from, float to) noexcept;

    enum class Transition { none, fadingOut, fadingIn };

    PreChain pre;
    std::array<AmpChain, kNumOversamplingChoices> amp;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, kNumOversamplingChoices> oversamplers;
    PostChain post;

    std::array<std::atomic<int>, kNumOversamplingChoices> latencies {};
    std::atomic<int> activeOs { 1 };
    int requestedOs = 1;
    Transition transition = Transition::none;

    int maxBlockSize = 0;
    bool prepared = false;
};
} // namespace ac30
