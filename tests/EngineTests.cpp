#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "AllocationGuard.h"
#include "dsp/AmpEngine.h"

using namespace dumble;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 64;

AmpSettings crankedSettings (int osIndex)
{
    AmpSettings s;
    s.preamp.volume = 8.0f;
    s.preamp.treble = 7.0f;
    s.preamp.middle = 5.0f;
    s.preamp.bass = 5.0f;
    s.master = 9.0f;
    s.cabOn = false;
    s.outputDb = 0.0f;
    s.oversamplingIndex = osIndex;
    return s;
}

void run (AmpEngine& engine, std::vector<float>& buffer)
{
    for (size_t pos = 0; pos < buffer.size(); pos += kBlock)
        engine.process (buffer.data() + pos, (int) std::min<size_t> (kBlock, buffer.size() - pos));
}

/** Ratio (dB) of non-harmonic energy to harmonic energy for a pure tone through the amp. */
double aliasRatioDb (int osIndex, juce::StringArray* peaks = nullptr, double peakFloor = 1.0e9)
{
    constexpr int order = 14, n = 1 << order;
    const int bin = 1365; // ~4 kHz, exactly on a bin
    const auto f0 = (double) bin * kFs / (double) n;

    AmpEngine engine;
    engine.prepare (kFs, kBlock, osIndex);
    // edge-of-breakup: the SSS's natural habitat
    auto settings = crankedSettings (osIndex);
    settings.preamp.volume = 5.0f;
    settings.master = 7.0f;
    engine.setSettings (settings);
    engine.reset();

    std::vector<float> x ((size_t) (n * 3));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f0 * (double) i / kFs);

    run (engine, x);

    std::vector<float> fftData ((size_t) (2 * n), 0.0f);
    juce::dsp::WindowingFunction<float> window ((size_t) n, juce::dsp::WindowingFunction<float>::blackmanHarris, false);
    std::copy (x.end() - n, x.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) n);

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    double harmonic = 0.0, other = 0.0;
    for (int k = 2; k < n / 2; ++k)
    {
        const auto p = (double) fftData[(size_t) k] * fftData[(size_t) k];
        bool isHarmonic = false;

        for (int h = 1; h * bin < n / 2 + 8; ++h)
            isHarmonic = isHarmonic || std::abs (k - h * bin) <= 6;

        (isHarmonic ? harmonic : other) += p;

        if (! isHarmonic && peaks != nullptr && p > peakFloor)
            peaks->add (juce::String (10.0 * std::log10 (p), 1).paddedLeft ('0', 6) + " dB @ " + juce::String (k * kFs / n, 0) + " Hz");
    }

    return 10.0 * std::log10 (other / harmonic);
}
} // namespace

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Amp engine", "Dumble") {}

    void runTest() override
    {
        beginTest ("Output is finite and bounded for noise bursts at +12 dBFS (all factors)");
        for (int os = 0; os < AmpEngine::kNumOversamplingChoices; ++os)
        {
            AmpEngine engine;
            engine.prepare (kFs, kBlock, os);
            engine.setSettings (crankedSettings (os));

            juce::Random rng (42);
            std::vector<float> x ((size_t) kFs);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (i / 4800) % 2 == 0 ? (rng.nextFloat() * 2.0f - 1.0f) * 4.0f : 0.0f;

            run (engine, x);

            bool ok = true;
            for (auto v : x)
                ok = ok && std::isfinite (v) && std::abs (v) <= 4.0f;
            expect (ok, "non-finite or unbounded output at factor index " + juce::String (os));
        }

        beginTest ("Higher oversampling factors reduce aliasing");
        {
            juce::StringArray peaks;
            const auto a2 = aliasRatioDb (0), a4 = aliasRatioDb (1), a8 = aliasRatioDb (2, &peaks, 1.0);
            peaks.sortNatural();
            for (int i = peaks.size(); --i >= juce::jmax (0, peaks.size() - 5);)
                logMessage ("  strongest 8x non-harmonic: " + peaks[i]);
            logMessage ("alias/harmonic energy: 2x " + juce::String (a2, 1) + " dB, 4x " + juce::String (a4, 1)
                        + " dB, 8x " + juce::String (a8, 1) + " dB");
            // the power section always runs at >= 176 kHz internally, so 2x and 4x are close
            expect (a4 < a2 + 1.0, "4x must not alias more than 2x");
            expect (a8 < a4 - 3.0, "8x should alias clearly less than 4x");
            expectLessThan (a2, -25.0);
            expectLessThan (a8, -45.0);
        }

        beginTest ("Cabinet level does not depend on the host sample rate");
        {
            const auto cabGainDb = [] (double rate)
            {
                AmpEngine engine;
                engine.getCabinet().loadDefaultImpulseResponse();
                engine.prepare (rate, kBlock, 1);
                auto s = crankedSettings (1);
                s.preamp.volume = 2.0f; // clean: compare linear gain only
                s.cabOn = true;
                engine.setSettings (s);

                // let the convolution's background thread swap in its IR
                std::vector<float> warm ((size_t) rate / 10, 0.0f);
                for (int i = 0; i < 40; ++i)
                {
                    run (engine, warm);
                    juce::Thread::sleep (5);
                }

                std::vector<float> x ((size_t) rate * 2);
                for (size_t i = 0; i < x.size(); ++i)
                    x[i] = 0.01f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) i / rate);
                run (engine, x);

                double acc = 0.0;
                for (auto i = x.size() / 2; i < x.size(); ++i)
                    acc += (double) x[i] * x[i];
                return 10.0 * std::log10 (acc / (double) (x.size() / 2)) - 20.0 * std::log10 (0.01 / std::sqrt (2.0));
            };

            const auto ref = cabGainDb (48000.0);
            for (double rate : { 44100.0, 96000.0, 192000.0 })
            {
                const auto g = cabGainDb (rate);
                logMessage (juce::String (rate / 1000.0, 1) + " kHz: gain " + juce::String (g, 2) + " dB (48 kHz: "
                            + juce::String (ref, 2) + " dB)");
                expectWithinAbsoluteError (g, ref, 0.5);
            }
        }

        beginTest ("Latency is reported per factor and is an integer number of samples");
        {
            AmpEngine engine;
            engine.prepare (kFs, kBlock, 1);
            for (int os = 0; os < AmpEngine::kNumOversamplingChoices; ++os)
            {
                logMessage ("latency index " + juce::String (os) + " = " + juce::String (engine.getLatencySamples (os)) + " samples");
                expect (engine.getLatencySamples (os) >= 0);
            }
        }

        beginTest ("Switching oversampling while running is click-free and allocation-free");
        {
            AmpEngine engine;
            engine.getCabinet().loadDefaultImpulseResponse();
            engine.prepare (kFs, kBlock, 1);
            auto settings = crankedSettings (1);
            settings.cabOn = true;
            settings.preamp.reverbSend = 5.0f;
            settings.preamp.reverbReturn = 5.0f;
            engine.setSettings (settings);

            // let the convolution's background thread swap in its IR before measuring
            std::vector<float> warm ((size_t) kFs, 0.0f);
            for (int i = 0; i < 20; ++i)
            {
                run (engine, warm);
                juce::Thread::sleep (10);
            }

            const auto total = (size_t) kFs * 2;
            std::vector<float> y (total);
            float maxStepSteady = 0.0f, maxStepSwitch = 0.0f;
            float previous = 0.0f;

            test::AllocationGuard::resetCounts();
            {
                test::AllocationGuard guard;

                for (size_t pos = 0, blockIndex = 0; pos < total; pos += kBlock, ++blockIndex)
                {
                    for (size_t i = 0; i < kBlock; ++i)
                        y[pos + i] = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) (pos + i) / kFs);

                    // switch factor every 0.25 s and wiggle knobs, all from the "audio thread"
                    settings.oversamplingIndex = (int) ((pos / (size_t) (kFs / 4)) % 3);
                    settings.preamp.treble = 5.0f + 3.0f * (float) std::sin ((double) blockIndex * 0.01);
                    settings.preamp.volume = 6.0f + 2.0f * (float) std::sin ((double) blockIndex * 0.007);
                    engine.setSettings (settings);
                    engine.process (y.data() + pos, kBlock);

                    const auto switching = (pos % (size_t) (kFs / 4)) < (size_t) (4 * kBlock) && pos > (size_t) kFs / 4;
                    for (size_t i = 0; i < kBlock; ++i)
                    {
                        const auto step = std::abs (y[pos + i] - previous);
                        previous = y[pos + i];
                        (switching ? maxStepSwitch : maxStepSteady) = std::max (switching ? maxStepSwitch : maxStepSteady, step);
                    }
                }
            }

            logMessage ("max sample step: steady " + juce::String (maxStepSteady, 4) + ", around switches "
                        + juce::String (maxStepSwitch, 4));
            expect (maxStepSwitch <= maxStepSteady * 1.5f + 1.0e-3f, "click on oversampling switch");
            expectEquals ((int) test::AllocationGuard::getAllocationCount(), 0, "heap allocation on the audio path");
            expectEquals ((int) test::AllocationGuard::getDeallocationCount(), 0, "heap deallocation on the audio path");
        }

        beginTest ("Spring reverb: transparent at 0, audible decaying tail, stable");
        for (int os = 0; os < AmpEngine::kNumOversamplingChoices; ++os)
        {
            const auto render = [os] (float reverbKnob)
            {
                AmpEngine engine;
                engine.prepare (kFs, kBlock, os);
                auto s = crankedSettings (os);
                s.preamp.volume = 4.0f;
                s.preamp.reverbSend = reverbKnob;
                s.preamp.reverbReturn = reverbKnob;
                engine.setSettings (s);
                engine.reset();

                std::vector<float> x ((size_t) kFs * 3, 0.0f);
                for (size_t i = 0; i < 480; ++i) // 10 ms burst
                    x[i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 500.0 * (double) i / kFs);
                run (engine, x);
                return x;
            };

            const auto rms = [] (const std::vector<float>& y, double from, double to)
            {
                double acc = 0.0;
                const auto a = (size_t) (from * kFs), b = (size_t) (to * kFs);
                for (auto i = a; i < b; ++i)
                    acc += (double) y[i] * y[i];
                return std::sqrt (acc / (double) (b - a));
            };

            const auto dry = render (0.0f);
            const auto wet = render (7.0f);

            const auto dryTail = rms (dry, 0.2, 0.6), wetTail = rms (wet, 0.2, 0.6), lateTail = rms (wet, 2.5, 3.0);
            logMessage (juce::String (2 << os) + "x: tail RMS 0.2-0.6 s dry " + juce::String (dryTail, 6) + ", wet "
                        + juce::String (wetTail, 6) + ", 2.5-3 s wet " + juce::String (lateTail, 8));

            expect (wetTail > 20.0 * dryTail + 1.0e-5, "reverb tail missing");
            expect (lateTail < wetTail * 0.05, "reverb tail does not decay");

            bool finite = true;
            for (auto v : wet)
                finite = finite && std::isfinite (v);
            expect (finite);
        }

        beginTest ("CPU benchmark (64-sample blocks @ 48 kHz)");
        for (int os = 0; os < AmpEngine::kNumOversamplingChoices; ++os)
        {
            AmpEngine engine;
            engine.getCabinet().loadDefaultImpulseResponse();
            engine.prepare (kFs, kBlock, os);
            auto s = crankedSettings (os);
            s.cabOn = true;
            s.preamp.reverbSend = 5.0f;
            s.preamp.reverbReturn = 5.0f;
            engine.setSettings (s);

            std::vector<float> x ((size_t) kFs * 5);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * (double) i / kFs);

            const auto start = juce::Time::getHighResolutionTicks();
            run (engine, x);
            const auto seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);

            const auto load = seconds / 5.0 * 100.0;
            logMessage (juce::String (2 << os) + "x: " + juce::String (load, 2) + "% of one core");
            expectLessThan (load, 100.0, "slower than real time");
        }
    }
};

static EngineTests engineTests;
