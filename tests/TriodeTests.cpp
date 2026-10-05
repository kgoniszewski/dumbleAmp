#include <juce_core/juce_core.h>

#include "dsp/PhaseInverter.h"
#include "dsp/PowerAmp6550.h"
#include "dsp/TriodeStage.h"

using namespace dumble;

class TriodeTests final : public juce::UnitTest
{
public:
    TriodeTests() : juce::UnitTest ("Tube stages", "Dumble") {}

    void runTest() override
    {
        constexpr double fs = 192000.0;

        beginTest ("12AX7 common-cathode operating point is in the expected range");
        {
            TriodeStage stage;
            stage.setCircuit (circuit::kV1a, k12AX7);
            stage.prepare ({ fs, 512, 1 });

            const auto vp = stage.getPlateVoltageDC();
            const auto vk = stage.getCathodeVoltageDC();
            logMessage ("V1a: Vp = " + juce::String (vp, 1) + " V, Vk = " + juce::String (vk, 2) + " V, Ip = "
                        + juce::String ((circuit::kV1a.bPlus - vp) / circuit::kV1a.rPlate * 1000.0f, 3) + " mA");

            expect (vp > 150.0f && vp < 240.0f, "plate voltage out of range");
            expect (vk > 0.8f && vk < 2.5f, "cathode voltage out of range");
        }

        beginTest ("Small-signal gain of a bypassed 12AX7 stage is ~30..70 and inverting");
        {
            TriodeStage stage;
            stage.setCircuit (circuit::kV1a, k12AX7);
            stage.prepare ({ fs, 512, 1 });

            // settle, then measure correlation with a 1 kHz, 10 mV sine
            double sumXY = 0.0, sumXX = 0.0;
            for (int i = 0; i < (int) fs; ++i)
            {
                const auto x = 0.01f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / (float) fs);
                const auto y = stage.processSample (x);

                if (i > (int) fs / 2)
                {
                    sumXY += (double) x * y;
                    sumXX += (double) x * x;
                }
            }

            const auto gain = sumXY / sumXX;
            logMessage ("V1a gain @1 kHz = " + juce::String (gain, 2));
            expect (gain < -30.0 && gain > -70.0, "unexpected stage gain");
        }

        beginTest ("Triode stage stays finite and bounded under extreme input");
        {
            TriodeStage stage;
            stage.setCircuit (circuit::kV1b, k12AX7);
            stage.prepare ({ fs, 512, 1 });

            juce::Random rng (1234);
            bool ok = true;

            for (int i = 0; i < 200000; ++i)
            {
                const auto y = stage.processSample ((rng.nextFloat() * 2.0f - 1.0f) * 200.0f);
                ok = ok && std::isfinite (y) && std::abs (y) < circuit::kPreampBplus;
            }

            expect (ok);
        }

        beginTest ("Phase inverter LUT: quiescent at zero, asymmetric anti-phase outputs");
        {
            PhaseInverter pi;
            pi.prepare();

            const auto zero = pi.processSample (0.0f);
            const auto pos = pi.processSample (0.5f);
            const auto neg = pi.processSample (-0.5f);

            logMessage ("PI +0.5 V -> a = " + juce::String (pos.a, 2) + " V, b = " + juce::String (pos.b, 2) + " V");
            expectWithinAbsoluteError (zero.a, 0.0f, 1.0e-3f);
            expectWithinAbsoluteError (zero.b, 0.0f, 1.0e-3f);
            expect (pos.a < 0.0f && pos.b > 0.0f, "plates must swing in anti-phase");
            expect (neg.a > 0.0f && neg.b < 0.0f, "plates must swing in anti-phase");
            expect (std::abs (pos.a) > 5.0f, "PI gain too low");
        }

        beginTest ("6550 fixed bias reaches the idle current target");
        {
            PowerAmp6550 amp;
            amp.prepare (fs);

            const auto perTube = amp.getIdleTotalCurrent() / (2.0f * (float) circuit::kPowerTubesPerSide);
            logMessage ("6550 bias = " + juce::String (amp.getBiasVoltage(), 2) + " V, idle/tube = "
                        + juce::String (perTube * 1000.0f, 1) + " mA");
            expectWithinAbsoluteError (perTube, circuit::kIdleCurrentPerTube, 0.002f);
            expect (amp.getBiasVoltage() < -10.0f && amp.getBiasVoltage() > -90.0f);
        }
    }
};

static TriodeTests triodeTests;
