#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr const char* kCabinetFileProperty = "cabinetFile";
}

DumbleAudioProcessor::DumbleAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "DumbleSSS", dumble::createParameterLayout()),
      params (apvts)
{
    engine.getCabinet().loadDefaultImpulseResponse();

    // Latency changes (oversampling factor) are reported from the message thread only.
    startTimerHz (10);
}

DumbleAudioProcessor::~DumbleAudioProcessor()
{
    stopTimer();
}

//==============================================================================
void DumbleAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto settings = params.load();

    engine.prepare (sampleRate, samplesPerBlock, settings.oversamplingIndex);
    engine.setSettings (settings);
    engine.reset();

    loadMeasurer.reset (sampleRate, samplesPerBlock);

    reportedLatency = engine.getLatencySamples (settings.oversamplingIndex);
    setLatencySamples (reportedLatency);
}

void DumbleAudioProcessor::releaseResources()
{
    engine.reset();
}

bool DumbleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The amp is mono in. Output is mono, or stereo with the mono signal duplicated
    // (some AU hosts insist on a stereo output for effects).
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    return in == juce::AudioChannelSet::mono()
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void DumbleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // REAL-TIME CONTRACT: no allocation, no locks, no logging, no I/O, no message-thread calls.
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer (loadMeasurer, buffer.getNumSamples());

    const auto numSamples = buffer.getNumSamples();
    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    if (numSamples == 0 || numOut == 0)
        return;

    if (numIn == 0)
    {
        buffer.clear();
        return;
    }

    auto* mono = buffer.getWritePointer (0);

    decayPeak (inputPeak, buffer.getMagnitude (0, 0, numSamples));

    engine.setSettings (params.load());
    engine.process (mono, numSamples);

    decayPeak (outputPeak, buffer.getMagnitude (0, 0, numSamples));

    for (int ch = 1; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
}

float DumbleAudioProcessor::decayPeak (std::atomic<float>& peak, float newPeak) noexcept
{
    const auto decayed = peak.load (std::memory_order_relaxed) * 0.9f;
    const auto value = juce::jmax (decayed, newPeak);
    peak.store (value, std::memory_order_relaxed);
    return value;
}

//==============================================================================
void DumbleAudioProcessor::timerCallback()
{
    const auto latency = engine.getLatencySamples (params.load().oversamplingIndex);

    if (latency != reportedLatency)
    {
        reportedLatency = latency;
        setLatencySamples (latency);
        updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    }
}

//==============================================================================
void DumbleAudioProcessor::loadCabinetImpulseResponse (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    cabinetFile = file;
    apvts.state.setProperty (kCabinetFileProperty, file.getFullPathName(), nullptr);

    if (file.existsAsFile())
        engine.getCabinet().loadImpulseResponse (file);
    else
        engine.getCabinet().loadDefaultImpulseResponse();
}

juce::String DumbleAudioProcessor::getCabinetName() const
{
    return cabinetFile.existsAsFile() ? cabinetFile.getFileNameWithoutExtension()
                                      : juce::String ("Built-in 2x12");
}

//==============================================================================
void DumbleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DumbleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const auto path = apvts.state.getProperty (kCabinetFileProperty).toString();
            const auto file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();

            // hosts may restore state off the message thread: hop over before touching the IR
            juce::MessageManager::callAsync ([safeThis = juce::WeakReference<DumbleAudioProcessor> (this), file]
            {
                if (auto* p = safeThis.get())
                    p->loadCabinetImpulseResponse (file);
            });
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* DumbleAudioProcessor::createEditor()
{
    return new DumbleAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DumbleAudioProcessor();
}
