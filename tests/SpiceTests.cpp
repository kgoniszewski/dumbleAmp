#include <vector>

#include <juce_core/juce_core.h>

#include "dsp/TriodeStage.h"

using namespace dumble;

namespace
{
struct Harmonics
{
    double fundamental;
    double thdPercent;
};

/** Amplitudes of the first 10 harmonics of a 1 kHz tone over an integer number of periods (DFT). */
Harmonics analyse (const std::vector<float>& y, double fs, size_t start)
{
    const auto periodSamples = fs / 1000.0;
    const auto n = (size_t) (periodSamples * 20.0); // 20 ms = 20 periods

    double h[11] {};
    for (int k = 1; k <= 10; ++k)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const auto ph = 2.0 * juce::MathConstants<double>::pi * 1000.0 * k * (double) i / fs;
            re += y[start + i] * std::cos (ph);
            im += y[start + i] * std::sin (ph);
        }
        h[k] = 2.0 * std::sqrt (re * re + im * im) / (double) n;
    }

    double sum = 0.0;
    for (int k = 2; k <= 10; ++k)
        sum += h[k] * h[k];

    return { h[1], 100.0 * std::sqrt (sum) / h[1] };
}
} // namespace

/**
    Cross-check of TriodeStage against ngspice (spice/v1a_stage.cir): same circuit, same Koren
    model, transient analysis + .fourier. Reference values were produced with ngspice 42.
*/
class SpiceTests final : public juce::UnitTest
{
public:
    SpiceTests() : juce::UnitTest ("SPICE cross-check", "Dumble") {}

    void runTest() override
    {
        struct Reference { float amplitude; double fundamental, thdPercent; };
        const Reference references[] = { { 0.1f, 5.93196, 0.551866 },
                                          { 1.0f, 57.9249, 5.84774 },
                                          { 3.0f, 115.341, 18.8221 } };

        beginTest ("V1a DC operating point matches ngspice .op");
        {
            TriodeStage stage;
            stage.setCircuit (circuit::kV1a, k12AX7);
            stage.prepare ({ 192000.0, 512, 1 });
            expectWithinAbsoluteError (stage.getPlateVoltageDC(), 202.9533f, 0.05f);
            expectWithinAbsoluteError (stage.getCathodeVoltageDC(), 1.455701f, 0.005f);
        }

        beginTest ("V1a 1 kHz fundamental and THD match ngspice .fourier");
        for (const auto& ref : references)
        {
            constexpr double fs = 192000.0;
            TriodeStage stage;
            stage.setCircuit (circuit::kV1a, k12AX7);
            stage.prepare ({ fs, 512, 1 });

            std::vector<float> y ((size_t) (fs * 0.06));
            for (size_t i = 0; i < y.size(); ++i)
                y[i] = stage.processSample (ref.amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) i / fs));

            const auto h = analyse (y, fs, (size_t) (fs * 0.04));
            logMessage ("in " + juce::String (ref.amplitude) + " V: H1 " + juce::String (h.fundamental, 3) + " (SPICE "
                        + juce::String (ref.fundamental, 3) + "), THD " + juce::String (h.thdPercent, 3) + " % (SPICE "
                        + juce::String (ref.thdPercent, 3) + " %)");

            expectWithinAbsoluteError (h.fundamental, ref.fundamental, ref.fundamental * 0.01);
            expectWithinAbsoluteError (h.thdPercent, ref.thdPercent, ref.thdPercent * 0.03 + 0.02);
        }
    }
};

static SpiceTests spiceTests;
