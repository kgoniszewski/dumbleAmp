#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>

#include "BrightVolume.h"
#include "CircuitConstants.h"
#include "OnePole.h"

namespace dumble
{
/**
    Passive Fender-style Treble/Middle/Bass tone stack, modelled with its exact 3rd-order
    transfer function (method of D. T. Yeh and J. O. Smith, "Discretization of the '59 Fender
    Bassman Tone Stack", DAFx-06), re-derived for the blackface/SSS wiring and component values.

        H(s) = (b1 s + b2 s^2 + b3 s^3) / (a0 + a1 s + a2 s^2 + a3 s^3)

    The analog coefficients are mapped with the bilinear transform and run in a transposed
    direct form II. Recomputed in sub-blocks only while a pot is moving.
*/
class ToneStackTMB
{
public:
    static constexpr int kCoefficientUpdateInterval = 32;

    struct AnalogCoefficients
    {
        double b1, b2, b3, a0, a1, a2, a3;
    };

    /**
        Pot positions as wiper fractions (0..1) -> analog coefficients.

        Derived symbolically (nodal analysis) for the blackface/SSS wiring, where the bass and
        mid pots are rheostats:

            in --R4-- n1 --C2-- n2        in --C1-- n4 --(1-t)R1-- out --t*R1-- n2
                      n1 --C3-- n3        n2 --l*R2-- n3 --m*R3-- gnd

        (Yeh & Smith's Bassman 5F6A formulas differ: there the mid pot is a true potentiometer.)
    */
    static AnalogCoefficients analogCoefficients (double t, double m, double l) noexcept
    {
        using namespace circuit;
        const double R1 = kTsR1, R2 = kTsR2, R3 = kTsR3, R4 = kTsR4;
        const double C1 = kTsC1, C2 = kTsC2, C3 = kTsC3;

        AnalogCoefficients c {};

        c.b1 = C1 * R1 * t + C1 * R2 * l + C1 * R3 * m + C2 * R2 * l + C2 * R3 * m + C3 * R3 * m;

        c.b2 = C1 * C2 * R1 * R2 * l + C1 * C2 * R1 * R3 * m + C1 * C2 * R1 * R4 * t
             + C1 * C2 * R2 * R4 * l + C1 * C2 * R3 * R4 * m + C1 * C3 * R1 * R3 * m
             + C1 * C3 * R1 * R4 * t + C1 * C3 * R2 * R3 * l * m + C1 * C3 * R2 * R4 * l
             + C1 * C3 * R3 * R4 * m + C2 * C3 * R2 * R3 * l * m;

        c.b3 = C1 * C2 * C3 * R2 * l * (R1 * R3 * m + R1 * R4 * t + R3 * R4 * m);

        c.a0 = 1.0;

        c.a1 = C1 * R1 + C1 * R2 * l + C1 * R3 * m + C2 * R2 * l + C2 * R3 * m + C2 * R4
             + C3 * R3 * m + C3 * R4;

        c.a2 = C1 * C2 * R1 * R2 * l + C1 * C2 * R1 * R3 * m + C1 * C2 * R1 * R4
             + C1 * C2 * R2 * R4 * l + C1 * C2 * R3 * R4 * m + C1 * C3 * R1 * R3 * m
             + C1 * C3 * R1 * R4 + C1 * C3 * R2 * R3 * l * m + C1 * C3 * R2 * R4 * l
             + C1 * C3 * R3 * R4 * m + C2 * C3 * R2 * R3 * l * m + C2 * C3 * R2 * R4 * l;

        c.a3 = C1 * C2 * C3 * R2 * l * (R1 * R3 * m + R1 * R4 + R3 * R4 * m);

        return c;
    }

    /** Knob (0..10) -> wiper fractions: treble/mid linear, bass audio taper. */
    static void knobsToWipers (float treble, float middle, float bass, double& t, double& m, double& l) noexcept
    {
        t = juce::jlimit (0.0, 1.0, (double) treble * 0.1);
        m = juce::jlimit (0.0, 1.0, (double) middle * 0.1);
        l = (double) audioTaper (bass);
    }

    void setTreble (float knob) noexcept { treble.setTargetValue (knob); }
    void setMiddle (float knob) noexcept { middle.setTargetValue (knob); }
    void setBass   (float knob) noexcept { bass.setTargetValue (knob); }

    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        sampleRate = spec.sampleRate;
        for (auto* s : { &treble, &middle, &bass })
            s->reset (sampleRate, 0.03);
        reset();
    }

    void reset() noexcept
    {
        for (auto* s : { &treble, &middle, &bass })
            s->setCurrentAndTargetValue (s->getTargetValue());

        updateCoefficients (treble.getCurrentValue(), middle.getCurrentValue(), bass.getCurrentValue());
        z.fill (0.0);
        countdown = 0;
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept
        {
            if ((treble.isSmoothing() || middle.isSmoothing() || bass.isSmoothing()) && --countdown <= 0)
            {
                countdown = kCoefficientUpdateInterval;
                updateCoefficients (treble.getNextValue(), middle.getNextValue(), bass.getNextValue());
                treble.skip (kCoefficientUpdateInterval - 1);
                middle.skip (kCoefficientUpdateInterval - 1);
                bass.skip (kCoefficientUpdateInterval - 1);
            }

            // transposed direct form II, 3rd order
            // (double precision: at 8x oversampling the poles sit very close to z = 1)
            const double xd = x;
            const double y = b[0] * xd + z[0];
            z[0] = b[1] * xd - a[1] * y + z[1];
            z[1] = b[2] * xd - a[2] * y + z[2];
            z[2] = b[3] * xd - a[3] * y;
            return (float) y;
        });
    }

    /** Digital coefficients currently in use (b0..b3, a0..a3 with a0 == 1). */
    void getDigitalCoefficients (std::array<double, 4>& bOut, std::array<double, 4>& aOut) const noexcept
    {
        bOut = b;
        aOut = a;
    }

private:
    void updateCoefficients (float trebleKnob, float middleKnob, float bassKnob) noexcept
    {
        double t, m, l;
        knobsToWipers (trebleKnob, middleKnob, bassKnob, t, m, l);
        const auto ac = analogCoefficients (t, m, l);

        const auto c  = 2.0 * sampleRate;
        const auto c2 = c * c;
        const auto c3 = c2 * c;

        // bilinear transform, s = c (1 - z^-1) / (1 + z^-1)
        const double B0 =  ac.b1 * c + ac.b2 * c2 + ac.b3 * c3;
        const double B1 =  ac.b1 * c - ac.b2 * c2 - 3.0 * ac.b3 * c3;
        const double B2 = -ac.b1 * c - ac.b2 * c2 + 3.0 * ac.b3 * c3;
        const double B3 = -ac.b1 * c + ac.b2 * c2 - ac.b3 * c3;

        const double A0 = ac.a0 + ac.a1 * c + ac.a2 * c2 + ac.a3 * c3;
        const double A1 = 3.0 * ac.a0 + ac.a1 * c - ac.a2 * c2 - 3.0 * ac.a3 * c3;
        const double A2 = 3.0 * ac.a0 - ac.a1 * c - ac.a2 * c2 + 3.0 * ac.a3 * c3;
        const double A3 = ac.a0 - ac.a1 * c + ac.a2 * c2 - ac.a3 * c3;

        b = { B0 / A0, B1 / A0, B2 / A0, B3 / A0 };
        a = { 1.0,     A1 / A0, A2 / A0, A3 / A0 };
    }

    double sampleRate = 48000.0;
    juce::SmoothedValue<float> treble { 5.0f }, middle { 5.0f }, bass { 5.0f };
    std::array<double, 4> b {}, a { 1.0, 0.0, 0.0, 0.0 };
    std::array<double, 3> z {};
    int countdown = 0;
};
} // namespace dumble
