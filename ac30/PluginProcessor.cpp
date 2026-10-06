#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr const char* kCabinetFileProperty = "cabinetFile";
}

Ac30AudioProcessor::Ac30AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "AC30C2", ac30::createParameterLayout()),
      params (apvts)
{
    engine.getCabinet().loadDefaultImpulseResponse();

    // Latency changes (oversampling factor) are reported from the message thread only.
    startTimerHz (10);
}

Ac30AudioProcessor::~Ac30AudioProcessor()
{
    stopTimer();
}

//==============================================================================
void Ac30AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto settings = params.load();

    engine.prepare (sampleRate, samplesPerBlock, settings.oversamplingIndex);
    engine.setSettings (settings);
    engine.reset();

    loadMeasurer.reset (sampleRate, samplesPerBlock);

    reportedLatency = engine.getLatencySamples (settings.oversamplingIndex);
    setLatencySamples (reportedLatency);
}

void Ac30AudioProcessor::releaseResources()
{
    engine.reset();
}

bool Ac30AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The amp is mono in. Output is mono, or stereo with the mono signal duplicated
    // (some AU hosts insist on a stereo output for effects).
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    return in == juce::AudioChannelSet::mono()
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void Ac30AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

float Ac30AudioProcessor::decayPeak (std::atomic<float>& peak, float newPeak) noexcept
{
    const auto decayed = peak.load (std::memory_order_relaxed) * 0.9f;
    const auto value = juce::jmax (decayed, newPeak);
    peak.store (value, std::memory_order_relaxed);
    return value;
}

//==============================================================================
void Ac30AudioProcessor::timerCallback()
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
void Ac30AudioProcessor::loadCabinetImpulseResponse (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    cabinetFile = file;
    apvts.state.setProperty (kCabinetFileProperty, file.getFullPathName(), nullptr);

    if (file.existsAsFile())
        engine.getCabinet().loadImpulseResponse (file);
    else
        engine.getCabinet().loadDefaultImpulseResponse();
}

juce::String Ac30AudioProcessor::getCabinetName() const
{
    return cabinetFile.existsAsFile() ? cabinetFile.getFileNameWithoutExtension()
                                      : juce::String ("Built-in 2x12 Greenback");
}

//==============================================================================
void Ac30AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void Ac30AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const auto path = apvts.state.getProperty (kCabinetFileProperty).toString();
            const auto file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();

            // hosts may restore state off the message thread: hop over before touching the IR
            juce::MessageManager::callAsync ([safeThis = juce::WeakReference<Ac30AudioProcessor> (this), file]
            {
                if (auto* p = safeThis.get())
                    p->loadCabinetImpulseResponse (file);
            });
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* Ac30AudioProcessor::createEditor()
{
    return new Ac30AudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Ac30AudioProcessor();
}
