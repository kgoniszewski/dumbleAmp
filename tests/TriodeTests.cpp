#include <juce_core/juce_core.h>

#include "dsp/CathodeFollower.h"
#include "dsp/LinearNetwork.h"
#include "dsp/PhaseInverter.h"
#include "dsp/PowerAmp6L6.h"
#include "dsp/PowerSection.h"
#include "dsp/TriodeStage.h"

using namespace dumble;

class TriodeTests final : public juce::UnitTest
{
public:
    TriodeTests() : juce::UnitTest ("Tube stages", "Dumble") {}

    void runTest() override
    {
        constexpr double fs = 192000.0;

        beginTest ("Koren table matches the analytic model");
        {
            const auto* table = KorenTable::forTube (k7025);
            expect (table != nullptr);
            double worst = 0.0;
            for (float vg = -8.0f; vg < 1.0f; vg += 0.137f)
                for (float vp = 20.0f; vp < 450.0f; vp += 13.3f)
                {
                    const auto a = korenTriode (k7025, vg, vp).ip;
                    const auto b = table->evaluate (vg, vp).ip;
                    worst = std::max (worst, (double) std::abs (a - b) / std::max (1.0e-4, (double) a));
                }
            logMessage ("worst relative error (Ip > 0.1 mA) = " + juce::String (worst * 100.0, 3) + " %");
            expectLessThan (worst, 0.01);
        }

        beginTest ("Triode stage stays finite and bounded under extreme input");
        {
            TriodeStage stage;
            auto values = circuit::kU20;
            stage.setCircuit (values, k7025);
            stage.prepare ({ fs, 512, 1 });

            juce::Random rng (1234);
            bool ok = true;
            for (int i = 0; i < 200000; ++i)
            {
                stage.setCathodeInjection ((rng.nextFloat() - 0.5f) * 1.0e-3f);
                const auto y = stage.processSample ((rng.nextFloat() * 2.0f - 1.0f) * 200.0f);
                ok = ok && std::isfinite (y) && std::abs (y) < circuit::kHT4;
            }
            expect (ok);
        }

        beginTest ("LinearNetwork: RC low-pass matches the analytic response");
        {
            LinearNetwork<3, 4> net;
            net.setNumNodes (2);
            net.setSourceNode (0);
            net.addResistor (0, 1, 10.0e3);
            net.addCapacitor (1, -1, 10.0e-9); // fc = 1.59 kHz
            net.prepare (fs);
            net.reset();

            const double f = 1591.55;
            double peak = 0.0;
            for (int i = 0; i < (int) fs; ++i)
            {
                double a, b;
                net.sourceCurrentAffine (a, b);
                net.step (std::sin (2.0 * juce::MathConstants<double>::pi * f * i / fs));
                if (i > (int) fs / 2)
                    peak = std::max (peak, std::abs (net.voltage (1)));
            }
            logMessage ("|H(fc)| = " + juce::String (peak, 4));
            expectWithinAbsoluteError (peak, 1.0 / std::sqrt (2.0), 0.003);
        }

        beginTest ("Phase inverter LUT: quiescent at zero, near-balanced anti-phase outputs");
        {
            PhaseInverter pi;
            pi.prepare();

            const auto zero = pi.processSample (0.0f);
            const auto pos = pi.processSample (0.5f);
            logMessage ("PI +0.5 V -> a = " + juce::String (pos.a, 2) + " V, b = " + juce::String (pos.b, 2) + " V");
            expectWithinAbsoluteError (zero.a, 0.0f, 1.0e-3f);
            expectWithinAbsoluteError (zero.b, 0.0f, 1.0e-3f);
            expect (pos.a < 0.0f && pos.b > 0.0f, "plates must swing in anti-phase");
            expect (std::abs (pos.a / pos.b) > 0.8f && std::abs (pos.a / pos.b) < 1.25f, "pair should be near-balanced");
            expect (std::abs (pos.a) > 5.0f, "PI gain too low");
        }

        beginTest ("Driver cathode followers bias the 6L6GCs from the -320 V supply");
        {
            PowerAmp6L6 amp;
            amp.prepare (fs);
            logMessage ("6L6GC grid bias = " + juce::String (amp.getGridBias(), 2) + " V, idle/tube = "
                        + juce::String (amp.getIdleCurrentPerTube() * 1000.0f, 1) + " mA");
            expect (amp.getGridBias() < -35.0f && amp.getGridBias() > -50.0f);
            expect (amp.getIdleCurrentPerTube() > 0.010f && amp.getIdleCurrentPerTube() < 0.080f);
        }

        beginTest ("Global NFB (speaker -> PI tail) is negative and stable at 96 / 192 / 384 kHz");
        for (double rate : { 96000.0, 192000.0, 384000.0 })
        {
            const auto gainAt1k = [rate] (float sign, double& tailRms)
            {
                PowerSection p;
                p.prepare ({ rate, 512, 1 });
                p.setFeedbackSign (sign);
                double sxy = 0.0, sxx = 0.0;
                const auto n = (int) rate;
                for (int i = 0; i < n; ++i)
                {
                    const auto x = 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / rate);
                    const auto y = p.processSample (x);
                    if (i > n / 2) { sxy += (double) x * y; sxx += (double) x * x; }
                }
                double acc = 0.0;
                for (int i = 0; i < n / 2; ++i)
                {
                    const auto y = p.processSample (0.0f);
                    if (i > n / 4) acc += (double) y * y;
                }
                tailRms = std::sqrt (acc / (n / 4));
                return std::abs (sxy / sxx);
            };

            double tailClosed = 0.0, tailOpen = 0.0;
            const auto closed = gainAt1k (-1.0f, tailClosed);
            const auto open = gainAt1k (0.0f, tailOpen);
            logMessage (juce::String (rate / 1000.0, 0) + " kHz: open-loop gain " + juce::String (open, 3) + ", closed-loop "
                        + juce::String (closed, 3) + " (" + juce::String (20.0 * std::log10 (open / closed), 1) + " dB NFB), tail "
                        + juce::String (tailClosed, 8));
            expect (closed < open / 1.5, "feedback must reduce the gain");
            expectLessThan (tailClosed, 1.0e-4, "loop must not oscillate");
        }

        beginTest ("Cathode follower tracks its grid with ~unity gain");
        {
            CathodeFollower cf;
            cf.setCircuit ({ circuit::kHT4, circuit::kCfLoad, 0.0f }, k7025);
            cf.prepare (207.0f);
            const auto dc = cf.getCathodeVoltageDC();
            for (int i = 0; i < 100; ++i)
                cf.processSample (209.0f);
            const auto gain = (cf.getCathodeVoltage() - dc) / 2.0f;
            logMessage ("CF Vk(dc) = " + juce::String (dc, 2) + " V, gain = " + juce::String (gain, 4));
            expect (gain > 0.95f && gain < 1.0f);
        }
    }
};

static TriodeTests triodeTests;
