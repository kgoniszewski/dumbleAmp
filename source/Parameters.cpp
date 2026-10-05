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
    const auto& p = d.preamp;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (decibels (ParamIDs::inputGain, "Input",  -24.0f, 24.0f, d.inputGainDb),
                knob (ParamIDs::volume,       "Volume",        p.volume),
                knob (ParamIDs::treble,       "Treble",        p.treble),
                knob (ParamIDs::middle,       "Middle",        p.middle),
                knob (ParamIDs::bass,         "Bass",          p.bass),
                knob (ParamIDs::reverbSend,   "Reverb Send",   p.reverbSend),
                knob (ParamIDs::reverbReturn, "Reverb Return", p.reverbReturn),
                knob (ParamIDs::master,       "Master",        d.master),
                decibels (ParamIDs::output, "Output", -36.0f, 12.0f, d.outputDb));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::bright, kVersion }, "Bright",  p.bright),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::deep,   kVersion }, "Deep",    p.deep),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::accent, kVersion }, "Accent",  d.accent),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::cabOn,  kVersion }, "Cabinet", d.cabOn));

    // 7-position rotary filter switches of the SSS #002 (High: treble caps, Low: ladder taps)
    const juce::StringArray positions { "1", "2", "3", "4", "5", "6", "7" };
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::highFilter, kVersion }, "High", positions, p.high - 1),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::lowFilter,  kVersion }, "Low",  positions, p.low - 1));

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
      reverbSend   (apvts.getRawParameterValue (ParamIDs::reverbSend)),
      reverbReturn (apvts.getRawParameterValue (ParamIDs::reverbReturn)),
      master       (apvts.getRawParameterValue (ParamIDs::master)),
      output       (apvts.getRawParameterValue (ParamIDs::output)),
      bright       (apvts.getRawParameterValue (ParamIDs::bright)),
      deep         (apvts.getRawParameterValue (ParamIDs::deep)),
      accent       (apvts.getRawParameterValue (ParamIDs::accent)),
      highFilter   (apvts.getRawParameterValue (ParamIDs::highFilter)),
      lowFilter    (apvts.getRawParameterValue (ParamIDs::lowFilter)),
      cabOn        (apvts.getRawParameterValue (ParamIDs::cabOn)),
      oversampling (apvts.getRawParameterValue (ParamIDs::oversampling))
{
    for (auto* p : { inputGain, volume, treble, middle, bass, reverbSend, reverbReturn, master, output,
                     bright, deep, accent, highFilter, lowFilter, cabOn, oversampling })
    {
        jassert (p != nullptr);
        juce::ignoreUnused (p);
    }
}

AmpSettings ParamRefs::load() const noexcept
{
    constexpr auto r = std::memory_order_relaxed;

    AmpSettings s;
    s.inputGainDb         = inputGain->load (r);
    s.preamp.volume       = volume->load (r);
    s.preamp.treble       = treble->load (r);
    s.preamp.middle       = middle->load (r);
    s.preamp.bass         = bass->load (r);
    s.preamp.reverbSend   = reverbSend->load (r);
    s.preamp.reverbReturn = reverbReturn->load (r);
    s.preamp.bright       = bright->load (r) >= 0.5f;
    s.preamp.deep         = deep->load (r) >= 0.5f;
    s.preamp.high         = juce::roundToInt (highFilter->load (r)) + 1;
    s.preamp.low          = juce::roundToInt (lowFilter->load (r)) + 1;
    s.master              = master->load (r);
    s.accent              = accent->load (r) >= 0.5f;
    s.outputDb            = output->load (r);
    s.cabOn               = cabOn->load (r) >= 0.5f;
    s.oversamplingIndex   = juce::roundToInt (oversampling->load (r));
    return s;
}
} // namespace dumble
