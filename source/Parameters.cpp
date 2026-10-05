#include "Parameters.h"

namespace dumble
{
namespace
{
    constexpr int kVersion = 1;

    std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const juce::String& name, float defaultValue)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, kVersion }, name,
            juce::NormalisableRange<float> { 0.0f, 10.0f, 0.01f },
            defaultValue,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
    }

    std::unique_ptr<juce::AudioParameterFloat> decibels (const char* id, const juce::String& name,
                                                         float minDb, float maxDb, float defaultDb)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, kVersion }, name,
            juce::NormalisableRange<float> { minDb, maxDb, 0.1f },
            defaultDb,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; }));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    const AmpSettings d;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (decibels (ParamIDs::inputGain, "Input",  -24.0f, 24.0f, d.inputGainDb),
                knob (ParamIDs::volume,   "Volume",   d.volume),
                knob (ParamIDs::treble,   "Treble",   d.treble),
                knob (ParamIDs::middle,   "Middle",   d.middle),
                knob (ParamIDs::bass,     "Bass",     d.bass),
                knob (ParamIDs::presence, "Presence", d.presence),
                knob (ParamIDs::master,   "Master",   d.master),
                decibels (ParamIDs::output, "Output", -36.0f, 12.0f, d.outputDb));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::bright, kVersion }, "Bright", d.bright),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::deep,   kVersion }, "Deep",   d.deep),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::cabOn,  kVersion }, "Cabinet", d.cabOn));

    // Changing the factor changes latency -> not automatable.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::oversampling, kVersion }, "Oversampling",
        juce::StringArray { "2x", "4x", "8x" }, d.oversamplingIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    return layout;
}

ParamRefs::ParamRefs (juce::AudioProcessorValueTreeState& apvts)
    : inputGain    (apvts.getRawParameterValue (ParamIDs::inputGain)),
      volume       (apvts.getRawParameterValue (ParamIDs::volume)),
      treble       (apvts.getRawParameterValue (ParamIDs::treble)),
      middle       (apvts.getRawParameterValue (ParamIDs::middle)),
      bass         (apvts.getRawParameterValue (ParamIDs::bass)),
      presence     (apvts.getRawParameterValue (ParamIDs::presence)),
      master       (apvts.getRawParameterValue (ParamIDs::master)),
      output       (apvts.getRawParameterValue (ParamIDs::output)),
      bright       (apvts.getRawParameterValue (ParamIDs::bright)),
      deep         (apvts.getRawParameterValue (ParamIDs::deep)),
      cabOn        (apvts.getRawParameterValue (ParamIDs::cabOn)),
      oversampling (apvts.getRawParameterValue (ParamIDs::oversampling))
{
    jassert (inputGain != nullptr && volume != nullptr && treble != nullptr && middle != nullptr
             && bass != nullptr && presence != nullptr && master != nullptr && output != nullptr
             && bright != nullptr && deep != nullptr && cabOn != nullptr && oversampling != nullptr);
}

AmpSettings ParamRefs::load() const noexcept
{
    constexpr auto r = std::memory_order_relaxed;

    AmpSettings s;
    s.inputGainDb       = inputGain->load (r);
    s.volume            = volume->load (r);
    s.treble            = treble->load (r);
    s.middle            = middle->load (r);
    s.bass              = bass->load (r);
    s.presence          = presence->load (r);
    s.master            = master->load (r);
    s.outputDb          = output->load (r);
    s.bright            = bright->load (r) >= 0.5f;
    s.deep              = deep->load (r) >= 0.5f;
    s.cabOn             = cabOn->load (r) >= 0.5f;
    s.oversamplingIndex = juce::roundToInt (oversampling->load (r));
    return s;
}
} // namespace dumble
