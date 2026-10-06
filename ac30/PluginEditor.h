#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Ac30LookAndFeel.h"
#include "ui/LevelMeter.h"

class Ac30AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit Ac30AudioProcessorEditor (Ac30AudioProcessor&);
    ~Ac30AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void addKnob (Knob&, const char* paramID, const juce::String& text, bool onPanel);
    void addToggle (juce::ToggleButton&, std::unique_ptr<ButtonAttachment>&, const char* paramID, const juce::String& text);
    void addChoice (juce::ComboBox&, juce::Label&, std::unique_ptr<ComboBoxAttachment>&, const char* paramID,
                    const juce::StringArray& items, const juce::String& text);
    void chooseImpulseResponse();
    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / 1000.0f; }
    int px (float v) const noexcept { return juce::roundToInt (v * scale()); }

    Ac30AudioProcessor& ampProcessor;
    ac30::ui::Ac30LookAndFeel lookAndFeel;

    // panel order of the AC30C2: Normal | Top Boost | Reverb | Tremolo | Master
    Knob normalVolume, tbVolume, treble, bass, reverbTone, reverbLevel, tremSpeed, tremDepth, toneCut, master;
    Knob input, output;

    juce::ToggleButton reverbOn, tremOn, cabinet;
    std::unique_ptr<ButtonAttachment> reverbOnAttachment, tremOnAttachment, cabinetAttachment;

    juce::ComboBox inputJack, oversampling;
    juce::Label inputJackLabel, oversamplingLabel;
    std::unique_ptr<ComboBoxAttachment> inputJackAttachment, oversamplingAttachment;

    juce::TextButton loadIrButton { "Load IR..." }, defaultIrButton { "Built-in" };
    juce::Label irName, cpuLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    dumble::ui::LevelMeter inputMeter, outputMeter;

    juce::Rectangle<int> panelArea, sectionNormal, sectionTopBoost, sectionReverb, sectionTremolo, sectionMaster, nameplate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ac30AudioProcessorEditor)
};
