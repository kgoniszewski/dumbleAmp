#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/AmpLookAndFeel.h"
#include "ui/LevelMeter.h"

class DumbleAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit DumbleAudioProcessorEditor (DumbleAudioProcessor&);
    ~DumbleAudioProcessorEditor() override;

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

    void addKnob (Knob&, const char* paramID, const juce::String& text);
    void addToggle (juce::ToggleButton&, std::unique_ptr<ButtonAttachment>&, const char* paramID, const juce::String& text);
    void chooseImpulseResponse();
    void timerCallback() override;

    DumbleAudioProcessor& ampProcessor;
    dumble::ui::AmpLookAndFeel lookAndFeel;

    Knob volume, treble, middle, bass, presence, master, input, output;
    juce::ToggleButton bright, deep, cabinet;
    std::unique_ptr<ButtonAttachment> brightAttachment, deepAttachment, cabinetAttachment;

    juce::ComboBox oversampling;
    juce::Label oversamplingLabel;
    std::unique_ptr<ComboBoxAttachment> oversamplingAttachment;

    juce::TextButton loadIrButton { "Load IR..." }, defaultIrButton { "Built-in" };
    juce::Label irName, cpuLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    dumble::ui::LevelMeter inputMeter, outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DumbleAudioProcessorEditor)
};
