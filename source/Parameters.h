#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/AmpEngine.h"

namespace dumble
{
namespace ParamIDs
{
    inline constexpr const char* inputGain    = "inputGain";
    inline constexpr const char* volume       = "volume";
    inline constexpr const char* treble       = "treble";
    inline constexpr const char* middle       = "middle";
    inline constexpr const char* bass         = "bass";
    inline constexpr const char* presence     = "presence";
    inline constexpr const char* master       = "master";
    inline constexpr const char* output       = "output";
    inline constexpr const char* bright       = "bright";
    inline constexpr const char* deep         = "deep";
    inline constexpr const char* cabOn        = "cabOn";
    inline constexpr const char* oversampling = "oversampling";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/**
    Raw parameter pointers resolved once (by ID) at construction. On the audio thread only
    relaxed atomic loads happen — no string lookups, no locks.
*/
struct ParamRefs
{
    explicit ParamRefs (juce::AudioProcessorValueTreeState& apvts);

    AmpSettings load() const noexcept;

    std::atomic<float>* inputGain;
    std::atomic<float>* volume;
    std::atomic<float>* treble;
    std::atomic<float>* middle;
    std::atomic<float>* bass;
    std::atomic<float>* presence;
    std::atomic<float>* master;
    std::atomic<float>* output;
    std::atomic<float>* bright;
    std::atomic<float>* deep;
    std::atomic<float>* cabOn;
    std::atomic<float>* oversampling;
};
} // namespace dumble
