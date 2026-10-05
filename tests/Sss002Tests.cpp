#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "Sss002SpiceReference.h"
#include "dsp/MasterStage.h"
#include "dsp/Preamp002.h"

using namespace dumble;

namespace
{
struct Harmonics { double fundamental, thdPercent; };

/** First 10 harmonics over an integer number of periods (DFT), like ngspice's .fourier. */
Harmonics analyse (const std::vector<float>& y, double fs, double freq, size_t start)
{
    const auto periods = 20.0;
    const auto n = (size_t) std::llround (periods * fs / freq);
    double h[11] {};

    for (int k = 1; k <= 10; ++k)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const auto ph = 2.0 * juce::MathConstants<double>::pi * freq * k * (double) i / fs;
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

Harmonics simulate (const Sss002Config& c, float amplitude, float freq)
{
    constexpr double fs = 192000.0;
    Preamp002 preamp;
    MasterStage master;

    PreampControls pc;
    pc.treble = c.treble; pc.middle = c.middle; pc.bass = c.bass; pc.volume = c.volume;
    pc.high = c.high; pc.low = c.low; pc.bright = c.bright; pc.deep = c.deep;
    preamp.setControls (pc);
    master.setMaster (c.master);
    master.setAccent (c.accent);

    preamp.prepare ({ fs, 512, 1 });
    master.prepare ({ fs, 512, 1 });

    const auto settle = (size_t) (1.2 * fs);
    const auto total = settle + (size_t) (21.0 * fs / freq);
    std::vector<float> y (total);

    for (size_t i = 0; i < total; ++i)
    {
        const auto x = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / fs);
        y[i] = master.processSample (preamp.processSample (x));
    }

    return analyse (y, fs, freq, settle);
}
} // namespace

/**
    The plugin's SSS #002 preamp against an ngspice simulation of the reconstruction's circuit
    (spice/gen_sss002_refs.py: real Koren models with inter-electrode capacitances and grid diodes).
*/
class Sss002Tests final : public juce::UnitTest
{
public:
    Sss002Tests() : juce::UnitTest ("SSS #002 vs ngspice", "Dumble") {}

    void runTest() override
    {
        beginTest ("DC operating points match ngspice .op (and the reconstruction's annotations)");
        {
            Preamp002 preamp;
            preamp.prepare ({ 192000.0, 512, 1 });
            const auto op = preamp.getOperatingPoint();
            logMessage ("V1 Vp " + juce::String (op.v1Plate, 2) + " Vk " + juce::String (op.v1Cathode, 3)
                        + " | U37 Vp " + juce::String (op.u37Plate, 2) + " | U38 Vk " + juce::String (op.u38Cathode, 2));
            expectWithinAbsoluteError (op.v1Plate,    208.848f, 0.3f);
            expectWithinAbsoluteError (op.v1Cathode,  2.1615f,  0.02f);
            expectWithinAbsoluteError (op.v4Plate,    208.844f, 0.3f);
            expectWithinAbsoluteError (op.u37Plate,   207.050f, 0.3f);
            expectWithinAbsoluteError (op.u38Cathode, 207.496f, 0.5f);
        }

        beginTest ("Small-signal response and distortion match ngspice");
        double worstDb = 0.0, worstThd = 0.0;
        for (const auto& ref : kSss002Cases)
        {
            const auto& c = kSss002Configs[ref.config];
            const auto h = simulate (c, ref.amplitude, ref.frequency);
            const auto errDb = 20.0 * std::log10 (h.fundamental / ref.fundamental);
            const auto thdRatio = h.thdPercent / ref.thdPercent;

            logMessage (juce::String (c.name) + " " + juce::String (ref.amplitude) + " V @ " + juce::String (ref.frequency)
                        + " Hz: H1 " + juce::String (h.fundamental, 5) + " (SPICE " + juce::String (ref.fundamental, 5) + ", "
                        + juce::String (errDb, 2) + " dB), THD " + juce::String (h.thdPercent, 3) + " % (SPICE "
                        + juce::String (ref.thdPercent, 3) + " %)");

            worstDb = std::max (worstDb, std::abs (errDb));
            expectLessThan (std::abs (errDb), 0.75, "fundamental off");

            if (ref.thdPercent > 0.5) // below that both are dominated by numerical floor
            {
                worstThd = std::max (worstThd, std::abs (thdRatio - 1.0));
                expectWithinAbsoluteError (thdRatio, 1.0, 0.25, "THD off");
            }
        }
        logMessage ("worst |H1 error| = " + juce::String (worstDb, 2) + " dB, worst THD error = "
                    + juce::String (100.0 * worstThd, 1) + " %");
    }
};

static Sss002Tests sss002Tests;
