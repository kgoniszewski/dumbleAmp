#include "PluginEditor.h"

using namespace ac30;

Ac30AudioProcessorEditor::Ac30AudioProcessorEditor (Ac30AudioProcessor& p)
    : AudioProcessorEditor (&p), ampProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    addKnob (normalVolume, ParamIDs::normalVolume, "VOLUME", true);
    addKnob (tbVolume,     ParamIDs::tbVolume,     "VOLUME", true);
    addKnob (treble,       ParamIDs::treble,       "TREBLE", true);
    addKnob (bass,         ParamIDs::bass,         "BASS",   true);
    addKnob (reverbTone,   ParamIDs::reverbTone,   "TONE",   true);
    addKnob (reverbLevel,  ParamIDs::reverbLevel,  "LEVEL",  true);
    addKnob (tremSpeed,    ParamIDs::tremSpeed,    "SPEED",  true);
    addKnob (tremDepth,    ParamIDs::tremDepth,    "DEPTH",  true);
    addKnob (toneCut,      ParamIDs::toneCut,      "TONE CUT", true);
    addKnob (master,       ParamIDs::master,       "MASTER", true);
    addKnob (input,        ParamIDs::inputGain,    "INPUT",  false);
    addKnob (output,       ParamIDs::output,       "OUTPUT", false);

    addToggle (reverbOn, reverbOnAttachment, ParamIDs::reverbOn, "REVERB");
    addToggle (tremOn,   tremOnAttachment,   ParamIDs::tremOn,   "TREMOLO");
    addToggle (cabinet,  cabinetAttachment,  ParamIDs::cabOn,    "CABINET");

    addChoice (inputJack, inputJackLabel, inputJackAttachment, ParamIDs::input,
               { "Normal Hi", "Normal Lo", "Top Boost Hi", "Top Boost Lo", "Both (jumper)" }, "INPUT JACK");
    addChoice (oversampling, oversamplingLabel, oversamplingAttachment, ParamIDs::oversampling,
               { "2x", "4x", "8x" }, "OVERSAMPLING");

    loadIrButton.onClick = [this] { chooseImpulseResponse(); };
    defaultIrButton.onClick = [this]
    {
        ampProcessor.loadCabinetImpulseResponse ({});
        irName.setText (ampProcessor.getCabinetName(), juce::dontSendNotification);
    };
    addAndMakeVisible (loadIrButton);
    addAndMakeVisible (defaultIrButton);

    for (auto* l : { &irName, &cpuLabel })
    {
        l->setColour (juce::Label::textColourId, lookAndFeel.cream);
        addAndMakeVisible (*l);
    }
    irName.setText (ampProcessor.getCabinetName(), juce::dontSendNotification);
    irName.setJustificationType (juce::Justification::centredLeft);
    cpuLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    setResizable (true, true);
    setResizeLimits (800, 360, 1800, 810);
    getConstrainer()->setFixedAspectRatio (1000.0 / 450.0);
    setSize (1000, 450);

    startTimerHz (30);
}

Ac30AudioProcessorEditor::~Ac30AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Ac30AudioProcessorEditor::addKnob (Knob& k, const char* paramID, const juce::String& text, bool onPanel)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    if (! onPanel)
        k.slider.setColour (juce::Slider::textBoxTextColourId, lookAndFeel.cream);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<SliderAttachment> (ampProcessor.getValueTreeState(), paramID, k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    if (! onPanel)
        k.label.setColour (juce::Label::textColourId, lookAndFeel.cream);
    addAndMakeVisible (k.label);
}

void Ac30AudioProcessorEditor::addToggle (juce::ToggleButton& b, std::unique_ptr<ButtonAttachment>& attachment,
                                          const char* paramID, const juce::String& text)
{
    b.setButtonText (text);
    b.setColour (juce::ToggleButton::textColourId, lookAndFeel.cream);
    b.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffe8b04a));
    b.setColour (juce::ToggleButton::tickDisabledColourId, lookAndFeel.cream.withAlpha (0.6f));
    addAndMakeVisible (b);
    attachment = std::make_unique<ButtonAttachment> (ampProcessor.getValueTreeState(), paramID, b);
}

void Ac30AudioProcessorEditor::addChoice (juce::ComboBox& box, juce::Label& label, std::unique_ptr<ComboBoxAttachment>& attachment,
                                          const char* paramID, const juce::StringArray& items, const juce::String& text)
{
    // Items must exist before the attachment is created (choice index -> item id + 1).
    box.addItemList (items, 1);
    addAndMakeVisible (box);
    attachment = std::make_unique<ComboBoxAttachment> (ampProcessor.getValueTreeState(), paramID, box);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, lookAndFeel.cream);
    addAndMakeVisible (label);
}

void Ac30AudioProcessorEditor::chooseImpulseResponse()
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

void Ac30AudioProcessorEditor::timerCallback()
{
    inputMeter.setLevel (ampProcessor.getInputPeak());
    outputMeter.setLevel (ampProcessor.getOutputPeak());
    cpuLabel.setText ("DSP " + juce::String (ampProcessor.getCpuLoad() * 100.0, 1) + "%", juce::dontSendNotification);
}

void Ac30AudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto s = scale();
    lookAndFeel.drawCabinet (g, getLocalBounds(), panelArea.getHeight(), s);

    for (const auto& [area, text] : { std::pair { sectionNormal, "NORMAL" }, std::pair { sectionTopBoost, "TOP BOOST" },
                                      std::pair { sectionReverb, "REVERB" }, std::pair { sectionTremolo, "TREMOLO" },
                                      std::pair { sectionMaster, "MASTER" } })
        lookAndFeel.drawSectionLabel (g, area, text, s);

    lookAndFeel.drawNameplate (g, nameplate, s);
}

void Ac30AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (px (12));

    // copper control panel: 10 knobs in the order of the real panel
    panelArea = area.removeFromTop (px (190));
    auto panel = panelArea.reduced (px (8), px (6));
    const auto knobW = panel.getWidth() / 10;

    const auto section = [&] (juce::Rectangle<int>& label, std::initializer_list<Knob*> knobs)
    {
        auto strip = panel.removeFromLeft (knobW * (int) knobs.size());
        label = strip.removeFromTop (px (22));
        for (auto* k : knobs)
        {
            auto cell = strip.removeFromLeft (knobW).reduced (px (3), 0);
            k->label.setBounds (cell.removeFromTop (px (20)));
            k->slider.setBounds (cell);
        }
    };

    section (sectionNormal,   { &normalVolume });
    section (sectionTopBoost, { &tbVolume, &treble, &bass });
    section (sectionReverb,   { &reverbTone, &reverbLevel });
    section (sectionTremolo,  { &tremSpeed, &tremDepth });
    section (sectionMaster,   { &toneCut, &master });

    // grille area: name plate, then the studio controls
    area.removeFromTop (px (10));
    nameplate = area.removeFromTop (px (70));
    auto studio = area.reduced (px (12), px (6));
    const auto lineH = px (26);

    for (auto* k : { &input, &output })
    {
        auto cell = studio.removeFromLeft (px (96));
        k->label.setBounds (cell.removeFromTop (px (18)));
        k->slider.setBounds (cell.removeFromTop (px (110)));
    }

    auto meters = studio.removeFromLeft (px (36)).reduced (px (4)).withHeight (px (120));
    inputMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2).reduced (1, 0));
    outputMeter.setBounds (meters.reduced (1, 0));
    studio.removeFromLeft (px (16));

    auto jack = studio.removeFromLeft (px (170));
    inputJackLabel.setBounds (jack.removeFromTop (lineH));
    inputJack.setBounds (jack.removeFromTop (lineH).reduced (px (6), 0));
    jack.removeFromTop (px (8));
    reverbOn.setBounds (jack.removeFromTop (lineH));
    tremOn.setBounds (jack.removeFromTop (lineH));
    studio.removeFromLeft (px (16));

    auto cab = studio.removeFromLeft (px (250));
    cabinet.setBounds (cab.removeFromTop (lineH));
    auto buttons = cab.removeFromTop (lineH);
    loadIrButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2));
    defaultIrButton.setBounds (buttons.reduced (2));
    irName.setBounds (cab.removeFromTop (lineH));

    auto osArea = studio.removeFromRight (px (150));
    oversamplingLabel.setBounds (osArea.removeFromTop (lineH));
    oversampling.setBounds (osArea.removeFromTop (lineH).reduced (px (8), 0));
    cpuLabel.setBounds (osArea.removeFromTop (lineH));
}
