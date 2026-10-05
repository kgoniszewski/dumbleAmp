#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_dsp/juce_dsp.h>

#include "BrightVolume.h"
#include "CabinetIR.h"
#include "DCBlocker.h"
#include "PowerSection.h"
#include "RealtimeSafety.h"
#include "SpringReverb.h"
#include "ToneStackTMB.h"
#include "TriodeStage.h"

namespace dumble
{
/** Everything the audio thread needs from the parameters for one block (plain values, no strings). */
struct AmpSettings
{
    float inputGainDb = 0.0f;
    float volume = 5.0f, treble = 6.0f, middle = 5.0f, bass = 4.0f;
    float presence = 4.0f, master = 7.0f, reverb = 0.0f;
    float outputDb = -6.0f;
    bool bright = true, deep = false, cabOn = true;
    int oversamplingIndex = 1; // 0 = 2x, 1 = 4x, 2 = 8x
};

/**
    The complete mono amplifier:

      PreChain  (base rate)  : input gain -> 20 Hz HPF
      AmpChain  (oversampled): V1a -> Volume/Bright -> V1b -> TMB -> spring reverb mix -> V2a -> PI/Power/NFB
      PostChain (base rate)  : DC blocker -> cabinet IR -> output gain

    Three Oversampling objects (2x/4x/8x) and three AmpChains are fully prepared up front, so
    switching factor on the audio thread never allocates: the old path fades out over one block,
    the new path is reset and fades in over the next.
*/
class AmpEngine
{
public:
    static constexpr int kNumOversamplingChoices = 3;

    using PreChain  = juce::dsp::ProcessorChain<juce::dsp::Gain<float>, juce::dsp::IIR::Filter<float>>;
    using AmpChain  = juce::dsp::ProcessorChain<TriodeStage, BrightVolume, TriodeStage,
                                                ToneStackTMB, SpringReverb, TriodeStage, PowerSection>;
    using PostChain = juce::dsp::ProcessorChain<DCBlocker, CabinetIR, juce::dsp::Gain<float>>;

    enum AmpIndex  { v1a, volumeStage, v1b, toneStack, reverbTank, v2a, powerSection };
    enum PostIndex { dcBlocker, cabinet, outputGain };

    AmpEngine();

    /** Allocates everything. Call from prepareToPlay() only. */
    void prepare (double sampleRate, int maximumBlockSize, int initialOversamplingIndex);
    void reset() noexcept;

    /** Audio thread: forwards parameter targets to all processors. */
    void setSettings (const AmpSettings& settings) noexcept;

    /** Audio thread: processes a mono block in place. Any length is accepted (sub-blocked). */
    void process (float* samples, int numSamples) noexcept DUMBLE_NONBLOCKING;

    /** Latency (integer samples at base rate) for a given oversampling choice. Any thread. */
    int getLatencySamples (int oversamplingIndex) const noexcept;
    int getActiveOversamplingIndex() const noexcept { return activeOs.load (std::memory_order_relaxed); }

    CabinetIR& getCabinet() noexcept { return post.get<cabinet>(); }
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
} // namespace dumble
