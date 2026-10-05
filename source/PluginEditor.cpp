#include "PluginEditor.h"

using namespace dumble;

DumbleAudioProcessorEditor::DumbleAudioProcessorEditor (DumbleAudioProcessor& p)
    : AudioProcessorEditor (&p), ampProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    addKnob (volume,   ParamIDs::volume,    "VOLUME");
    addKnob (treble,   ParamIDs::treble,    "TREBLE");
    addKnob (middle,   ParamIDs::middle,    "MIDDLE");
    addKnob (bass,     ParamIDs::bass,      "BASS");
    addKnob (presence, ParamIDs::presence,  "PRESENCE");
    addKnob (master,   ParamIDs::master,    "MASTER");
    addKnob (input,    ParamIDs::inputGain, "INPUT");
    addKnob (output,   ParamIDs::output,    "OUTPUT");

    addToggle (bright,  brightAttachment,  ParamIDs::bright, "BRIGHT");
    addToggle (deep,    deepAttachment,    ParamIDs::deep,   "DEEP");
    addToggle (cabinet, cabinetAttachment, ParamIDs::cabOn,  "CABINET");

    // Items must exist before the attachment is created (choice index -> item id + 1).
    oversampling.addItemList ({ "2x", "4x", "8x" }, 1);
    addAndMakeVisible (oversampling);
    oversamplingAttachment = std::make_unique<ComboBoxAttachment> (ampProcessor.getValueTreeState(),
                                                                   ParamIDs::oversampling, oversampling);
    oversamplingLabel.setText ("OVERSAMPLING", juce::dontSendNotification);
    oversamplingLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (oversamplingLabel);

    loadIrButton.onClick = [this] { chooseImpulseResponse(); };
    defaultIrButton.onClick = [this]
    {
        ampProcessor.loadCabinetImpulseResponse ({});
        irName.setText (ampProcessor.getCabinetName(), juce::dontSendNotification);
    };
    addAndMakeVisible (loadIrButton);
    addAndMakeVisible (defaultIrButton);

    irName.setText (ampProcessor.getCabinetName(), juce::dontSendNotification);
    irName.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (irName);

    cpuLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cpuLabel);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    setResizable (true, true);
    setResizeLimits (720, 320, 1600, 700);
    getConstrainer()->setFixedAspectRatio (900.0 / 400.0);
    setSize (900, 400);

    startTimerHz (30);
}

DumbleAudioProcessorEditor::~DumbleAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DumbleAudioProcessorEditor::addKnob (Knob& k, const char* paramID, const juce::String& text)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<SliderAttachment> (ampProcessor.getValueTreeState(), paramID, k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);
}

void DumbleAudioProcessorEditor::addToggle (juce::ToggleButton& b, std::unique_ptr<ButtonAttachment>& attachment,
                                            const char* paramID, const juce::String& text)
{
    b.setButtonText (text);
    addAndMakeVisible (b);
    attachment = std::make_unique<ButtonAttachment> (ampProcessor.getValueTreeState(), paramID, b);
}

void DumbleAudioProcessorEditor::chooseImpulseResponse()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Select a cabinet impulse response",
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                       "*.wav;*.aif;*.aiff");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (file.existsAsFile())
                                  {
                                      ampProcessor.loadCabinetImpulseResponse (file);
                                      irName.setText (ampProcessor.getCabinetName(), juce::dontSendNotification);
                                  }
                              });
}

void DumbleAudioProcessorEditor::timerCallback()
{
    inputMeter.setLevel (ampProcessor.getInputPeak());
    outputMeter.setLevel (ampProcessor.getOutputPeak());
    cpuLabel.setText ("DSP " + juce::String (ampProcessor.getCpuLoad() * 100.0, 1) + "%", juce::dontSendNotification);
}

void DumbleAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.drawFaceplate (g, getLocalBounds(), "Steel String Singer");
}

void DumbleAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / 900.0f;
    auto area = getLocalBounds().reduced (juce::roundToInt (16 * scale));

    area.removeFromTop (juce::roundToInt (56 * scale)); // name plate

    // main control row (panel order of the SSS)
    auto row = area.removeFromTop (juce::roundToInt (190 * scale));
    auto switches = row.removeFromRight (juce::roundToInt (110 * scale));
    const auto knobWidth = row.getWidth() / 6;

    for (auto* k : { &volume, &treble, &middle, &bass, &presence, &master })
    {
        auto cell = row.removeFromLeft (knobWidth).reduced (juce::roundToInt (4 * scale));
        k->label.setBounds (cell.removeFromTop (juce::roundToInt (22 * scale)));
        k->slider.setBounds (cell);
    }

    switches.removeFromTop (juce::roundToInt (40 * scale));
    bright.setBounds (switches.removeFromTop (juce::roundToInt (40 * scale)));
    deep.setBounds (switches.removeFromTop (juce::roundToInt (40 * scale)));

    // studio row
    area.removeFromTop (juce::roundToInt (12 * scale));
    auto studio = area;

    for (auto* k : { &input, &output })
    {
        auto cell = studio.removeFromLeft (juce::roundToInt (100 * scale));
        k->label.setBounds (cell.removeFromTop (juce::roundToInt (18 * scale)));
        k->slider.setBounds (cell);
    }

    auto meters = studio.removeFromLeft (juce::roundToInt (40 * scale)).reduced (juce::roundToInt (4 * scale));
    inputMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2).reduced (1, 0));
    outputMeter.setBounds (meters.reduced (1, 0));

    studio.removeFromLeft (juce::roundToInt (16 * scale));
    auto cab = studio.removeFromLeft (juce::roundToInt (300 * scale));
    const auto lineH = juce::roundToInt (28 * scale);
    cabinet.setBounds (cab.removeFromTop (lineH));
    auto buttons = cab.removeFromTop (lineH);
    loadIrButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2));
    defaultIrButton.setBounds (buttons.reduced (2));
    irName.setBounds (cab.removeFromTop (lineH));

    auto osArea = studio.removeFromRight (juce::roundToInt (160 * scale));
    oversamplingLabel.setBounds (osArea.removeFromTop (lineH));
    oversampling.setBounds (osArea.removeFromTop (lineH).reduced (8, 0));
    cpuLabel.setBounds (osArea.removeFromTop (lineH));
}
