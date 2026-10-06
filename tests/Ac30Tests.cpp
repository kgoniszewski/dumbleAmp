#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "AllocationGuard.h"
#include "dsp/Ac30Engine.h"

using namespace ac30;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 64;

void run (Ac30Engine& engine, std::vector<float>& buffer)
{
    for (size_t pos = 0; pos < buffer.size(); pos += kBlock)
        engine.process (buffer.data() + pos, (int) std::min<size_t> (kBlock, buffer.size() - pos));
}

std::vector<float> sine (double freq, float amp, double seconds, double fs = kFs)
{
    std::vector<float> x ((size_t) (seconds * fs));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / fs);
    return x;
}

double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (auto i = from; i < to; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (to - from));
}

/** Amplitude of one frequency component (single-bin DFT over a whole number of periods). */
double toneAmplitude (const std::vector<float>& x, size_t from, size_t count, double freq, double fs)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const auto ph = 2.0 * juce::MathConstants<double>::pi * freq * (double) (from + i) / fs;
        re += x[from + i] * std::cos (ph);
        im += x[from + i] * std::sin (ph);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (double) count;
}

Ac30Settings cleanSettings (int osIndex = 1)
{
    Ac30Settings s;
    s.cabOn = false;
    s.outputDb = 0.0f;
    s.oversamplingIndex = osIndex;
    return s;
}
} // namespace

//==============================================================================
class Ac30OperatingPointTests final : public juce::UnitTest
{
public:
    Ac30OperatingPointTests() : juce::UnitTest ("AC30C2 operating point", "AC30") {}

    void runTest() override
    {
        beginTest ("Supplies, tube bias and EL84 idle match the AC30C2 design values");

        Ac30Engine engine;
        engine.setSettings (cleanSettings());
        engine.prepare (kFs, kBlock, 1);

        const auto& chain = engine.getAmpChain (1);
        const auto& power = chain.get<Ac30Engine::powerSection>();
        const auto& s = power.getSupplies();
        const auto op = chain.get<Ac30Engine::preamp>().getOperatingPoint();
        const auto pi = power.getPhaseInverterDC();
        const auto& el84 = power.getPowerAmp();

        logMessage ("B+1..5: " + juce::String (s.b1, 1) + " " + juce::String (s.b2, 1) + " " + juce::String (s.b3, 1)
                    + " " + juce::String (s.b4, 1) + " " + juce::String (s.b5, 1));
        logMessage ("V1 TB plate " + juce::String (op.v1PlateTB, 1) + ", Normal plate " + juce::String (op.v1PlateN, 1)
                    + ", cathode " + juce::String (op.v1Cathode, 3));
        logMessage ("V2a plate " + juce::String (op.v2aPlate, 1) + ", cathode " + juce::String (op.v2aCathode, 3)
                    + ", CF cathode " + juce::String (op.cfCathode, 1));
        logMessage ("PI plates " + juce::String (pi.plateA, 1) + " / " + juce::String (pi.plateB, 1)
                    + ", cathode " + juce::String (pi.cathode, 2));
        logMessage ("EL84 cathode " + juce::String (el84.getCathodeVoltageDC(), 2) + " V, plate "
                    + juce::String (el84.getIdlePlateCurrent() * 1000.0, 1) + " mA, screen "
                    + juce::String (el84.getIdleScreenCurrent() * 1000.0, 2) + " mA per tube");

        expect (s.b1 > 320.0 && s.b1 < 370.0);
        expect (s.b1 > s.b2 && s.b2 > s.b3 && s.b2 > s.b4 && s.b4 > s.b5);
        expectWithinAbsoluteError (el84.getCathodeVoltageDC(), 10.0, 1.5);     // ~10 V on R119 [AIK]
        const auto perTube = el84.getIdlePlateCurrent() + el84.getIdleScreenCurrent();
        expectWithinAbsoluteError (perTube, 0.050, 0.008);                         // ~50 mA per tube
        expect (op.v1Cathode > 0.8 && op.v1Cathode < 2.5);
        expect (op.v1PlateTB > 80.0 && op.v1PlateTB < s.b5);
        // the follower draws ~3 mA through R21 at a low plate-cathode voltage: it sits at its grid voltage
        expectWithinAbsoluteError (op.cfCathode, op.v2aPlate, 3.0);
        expect (pi.plateA > 150.0 && pi.plateA < s.b3);
    }
};

static Ac30OperatingPointTests ac30OperatingPointTests;

//==============================================================================
class Ac30SignalTests final : public juce::UnitTest
{
public:
    Ac30SignalTests() : juce::UnitTest ("AC30C2 signal path", "AC30") {}

    void runTest() override
    {
        beginTest ("Silence in -> (almost) silence out, no NaN, at every oversampling factor");
        for (int os = 0; os < 3; ++os)
        {
            Ac30Engine engine;
            auto settings = cleanSettings (os);
            engine.setSettings (settings);
            engine.prepare (kFs, kBlock, os);

            std::vector<float> x ((size_t) kFs, 0.0f);
            run (engine, x);
            const auto r = rms (x, x.size() / 2, x.size());
            logMessage (juce::String (2 << os) + "x idle rms " + juce::String (r, 8));
            expect (r < 1.0e-3);
        }

        beginTest ("Top Boost: a 100 mV, 1 kHz tone comes out, louder with Volume");
        {
            double previous = 0.0;
            for (float vol : { 2.0f, 5.0f, 8.0f })
            {
                Ac30Engine engine;
                auto settings = cleanSettings();
                settings.preamp.topBoostVolume = vol;
                engine.setSettings (settings);
                engine.prepare (kFs, kBlock, 1);

                auto x = sine (1000.0, 0.1f, 1.0);
                run (engine, x);
                const auto a = toneAmplitude (x, 24000, 24000, 1000.0, kFs);
                logMessage ("TB volume " + juce::String (vol) + ": 1 kHz amplitude " + juce::String (a, 4));
                expect (std::isfinite (a) && a > previous);
                previous = a;
            }
        }
    }
};

static Ac30SignalTests ac30SignalTests;
