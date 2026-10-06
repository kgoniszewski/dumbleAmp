#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Ac30Engine.h"

namespace ac30
{
namespace ParamIDs
{
    inline constexpr const char* inputGain    = "inputGain";
    inline constexpr const char* input        = "input";
    inline constexpr const char* normalVolume = "normalVolume";
    inline constexpr const char* tbVolume     = "tbVolume";
    inline constexpr const char* treble       = "treble";
    inline constexpr const char* bass         = "bass";
    inline constexpr const char* reverbTone   = "reverbTone";
    inline constexpr const char* reverbLevel  = "reverbLevel";
    inline constexpr const char* reverbOn     = "reverbOn";
    inline constexpr const char* tremSpeed    = "tremSpeed";
    inline constexpr const char* tremDepth    = "tremDepth";
    inline constexpr const char* tremOn       = "tremOn";
    inline constexpr const char* toneCut      = "toneCut";
    inline constexpr const char* master       = "master";
    inline constexpr const char* output       = "output";
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

    Ac30Settings load() const noexcept;

    std::atomic<float>* inputGain;
    std::atomic<float>* input;
    std::atomic<float>* normalVolume;
    std::atomic<float>* tbVolume;
    std::atomic<float>* treble;
    std::atomic<float>* bass;
    std::atomic<float>* reverbTone;
    std::atomic<float>* reverbLevel;
    std::atomic<float>* reverbOn;
    std::atomic<float>* tremSpeed;
    std::atomic<float>* tremDepth;
    std::atomic<float>* tremOn;
    std::atomic<float>* toneCut;
    std::atomic<float>* master;
    std::atomic<float>* output;
    std::atomic<float>* cabOn;
    std::atomic<float>* oversampling;
};
} // namespace ac30
