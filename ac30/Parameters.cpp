#include "Parameters.h"

namespace ac30
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

    std::unique_ptr<juce::AudioParameterBool> toggle (const char* id, const juce::String& name, bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, kVersion }, name, defaultValue);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    const Ac30Settings d;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (decibels (ParamIDs::inputGain, "Input", -24.0f, 24.0f, d.inputGainDb));

    // The jack the guitar is plugged into ("Both" = Normal Hi jumpered into Top Boost Hi)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::input, kVersion }, "Input Jack",
        juce::StringArray { "Normal Hi", "Normal Lo", "Top Boost Hi", "Top Boost Lo", "Both" },
        (int) d.preamp.input));

    layout.add (knob (ParamIDs::normalVolume, "Normal Volume",    d.preamp.normalVolume),
                knob (ParamIDs::tbVolume,     "Top Boost Volume", d.preamp.topBoostVolume),
                knob (ParamIDs::treble,       "Treble",           d.preamp.treble),
                knob (ParamIDs::bass,         "Bass",             d.preamp.bass),
                knob (ParamIDs::reverbTone,   "Reverb Tone",      d.reverb.tone),
                knob (ParamIDs::reverbLevel,  "Reverb Level",     d.reverb.level),
                knob (ParamIDs::tremSpeed,    "Tremolo Speed",    d.power.tremSpeed),
                knob (ParamIDs::tremDepth,    "Tremolo Depth",    d.power.tremDepth),
                knob (ParamIDs::toneCut,      "Tone Cut",         d.power.toneCut),
                knob (ParamIDs::master,       "Master Volume",    d.power.master),
                decibels (ParamIDs::output, "Output", -36.0f, 12.0f, d.outputDb));

    layout.add (toggle (ParamIDs::reverbOn, "Reverb On",  d.reverb.on),
                toggle (ParamIDs::tremOn,   "Tremolo On", d.power.tremOn),
                toggle (ParamIDs::cabOn,    "Cabinet",    d.cabOn));

    // Changing the factor changes latency -> not automatable.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::oversampling, kVersion }, "Oversampling",
        juce::StringArray { "2x", "4x", "8x" }, d.oversamplingIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    return layout;
}

ParamRefs::ParamRefs (juce::AudioProcessorValueTreeState& apvts)
    : inputGain    (apvts.getRawParameterValue (ParamIDs::inputGain)),
      input        (apvts.getRawParameterValue (ParamIDs::input)),
      normalVolume (apvts.getRawParameterValue (ParamIDs::normalVolume)),
      tbVolume     (apvts.getRawParameterValue (ParamIDs::tbVolume)),
      treble       (apvts.getRawParameterValue (ParamIDs::treble)),
      bass         (apvts.getRawParameterValue (ParamIDs::bass)),
      reverbTone   (apvts.getRawParameterValue (ParamIDs::reverbTone)),
      reverbLevel  (apvts.getRawParameterValue (ParamIDs::reverbLevel)),
      reverbOn     (apvts.getRawParameterValue (ParamIDs::reverbOn)),
      tremSpeed    (apvts.getRawParameterValue (ParamIDs::tremSpeed)),
      tremDepth    (apvts.getRawParameterValue (ParamIDs::tremDepth)),
      tremOn       (apvts.getRawParameterValue (ParamIDs::tremOn)),
      toneCut      (apvts.getRawParameterValue (ParamIDs::toneCut)),
      master       (apvts.getRawParameterValue (ParamIDs::master)),
      output       (apvts.getRawParameterValue (ParamIDs::output)),
      cabOn        (apvts.getRawParameterValue (ParamIDs::cabOn)),
      oversampling (apvts.getRawParameterValue (ParamIDs::oversampling))
{
    for (auto* p : { inputGain, input, normalVolume, tbVolume, treble, bass, reverbTone, reverbLevel, reverbOn,
                     tremSpeed, tremDepth, tremOn, toneCut, master, output, cabOn, oversampling })
    {
        jassert (p != nullptr);
        juce::ignoreUnused (p);
    }
}

Ac30Settings ParamRefs::load() const noexcept
{
    constexpr auto r = std::memory_order_relaxed;

    Ac30Settings s;
    s.inputGainDb           = inputGain->load (r);
    s.preamp.input          = (InputJack) juce::jlimit (0, 4, juce::roundToInt (input->load (r)));
    s.preamp.normalVolume   = normalVolume->load (r);
    s.preamp.topBoostVolume = tbVolume->load (r);
    s.preamp.treble         = treble->load (r);
    s.preamp.bass           = bass->load (r);
    s.reverb.tone           = reverbTone->load (r);
    s.reverb.level          = reverbLevel->load (r);
    s.reverb.on             = reverbOn->load (r) >= 0.5f;
    s.power.tremSpeed       = tremSpeed->load (r);
    s.power.tremDepth       = tremDepth->load (r);
    s.power.tremOn          = tremOn->load (r) >= 0.5f;
    s.power.toneCut         = toneCut->load (r);
    s.power.master          = master->load (r);
    s.outputDb              = output->load (r);
    s.cabOn                 = cabOn->load (r) >= 0.5f;
    s.oversamplingIndex     = juce::roundToInt (oversampling->load (r));
    return s;
}
} // namespace ac30
