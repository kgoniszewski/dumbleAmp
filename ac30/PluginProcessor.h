#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/Ac30Engine.h"

class Ac30AudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    Ac30AudioProcessor();
    ~Ac30AudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; } // spring reverb

    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    /** Message thread only. An empty file restores the built-in cabinet. */
    void loadCabinetImpulseResponse (const juce::File& file);
    juce::String getCabinetName() const;

    float getInputPeak() const noexcept  { return inputPeak.load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak.load (std::memory_order_relaxed); }
    double getCpuLoad() const noexcept   { return loadMeasurer.getLoadAsProportion(); }

private:
    void timerCallback() override;
    static float decayPeak (std::atomic<float>& peak, float newPeak) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    ac30::ParamRefs params;
    ac30::Ac30Engine engine;

    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    int reportedLatency = -1;

    juce::File cabinetFile;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Ac30AudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ac30AudioProcessor)
};
