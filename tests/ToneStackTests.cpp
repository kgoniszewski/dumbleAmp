#include <array>
#include <complex>

#include <juce_core/juce_core.h>

#include "dsp/ToneStackTMB.h"

using namespace dumble;
using cplx = std::complex<double>;

namespace
{
/**
    Independent reference: modified nodal analysis of the TMB netlist at s = j*w.

        in --R4-- n1 --C2-- n2          in --C1-- n4 --(1-t)R1-- out --t*R1-- n2
                  n1 --C3-- n3          n2 --l*R2-- n3 --m*R3-- gnd
*/
cplx referenceResponse (double freq, double t, double m, double l)
{
    using namespace circuit;
    const cplx s (0.0, 2.0 * juce::MathConstants<double>::pi * freq);
    const double rMin = 1.0e-3;

    const auto g = [rMin] (double r) { return cplx (1.0 / std::max (r, rMin), 0.0); };
    const auto yc = [&s] (double c) { return s * c; };

    enum { n1, n2, n3, n4, out, N };
    std::array<std::array<cplx, N>, N> a {};
    std::array<cplx, N> b {};
    const cplx vin (1.0, 0.0);

    // element between two unknown nodes
    const auto stamp = [&a] (int i, int j, cplx y)
    {
        a[(size_t) i][(size_t) i] += y;
        a[(size_t) j][(size_t) j] += y;
        a[(size_t) i][(size_t) j] -= y;
        a[(size_t) j][(size_t) i] -= y;
    };
    // element between an unknown node and the source
    const auto stampSource = [&a, &b, vin] (int i, cplx y)
    {
        a[(size_t) i][(size_t) i] += y;
        b[(size_t) i] += y * vin;
    };

    stampSource (n1, g (kTsR4));
    stamp (n1, n2, yc (kTsC2));
    stamp (n1, n3, yc (kTsC3));
    stampSource (n4, yc (kTsC1));
    stamp (n4, out, g ((1.0 - t) * kTsR1));
    stamp (out, n2, g (t * kTsR1));
    stamp (n2, n3, g (l * kTsR2));
    a[n3][n3] += g (m * kTsR3); // to ground

    // Gaussian elimination with partial pivoting
    for (int col = 0; col < N; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < N; ++r)
            if (std::abs (a[(size_t) r][(size_t) col]) > std::abs (a[(size_t) pivot][(size_t) col]))
                pivot = r;

        std::swap (a[(size_t) col], a[(size_t) pivot]);
        std::swap (b[(size_t) col], b[(size_t) pivot]);

        for (int r = col + 1; r < N; ++r)
        {
            const auto f = a[(size_t) r][(size_t) col] / a[(size_t) col][(size_t) col];
            for (int c = col; c < N; ++c)
                a[(size_t) r][(size_t) c] -= f * a[(size_t) col][(size_t) c];
            b[(size_t) r] -= f * b[(size_t) col];
        }
    }

    std::array<cplx, N> x {};
    for (int r = N - 1; r >= 0; --r)
    {
        auto acc = b[(size_t) r];
        for (int c = r + 1; c < N; ++c)
            acc -= a[(size_t) r][(size_t) c] * x[(size_t) c];
        x[(size_t) r] = acc / a[(size_t) r][(size_t) r];
    }

    return x[out];
}

cplx analogResponse (double freq, double t, double m, double l)
{
    const auto c = ToneStackTMB::analogCoefficients (t, m, l);
    const cplx s (0.0, 2.0 * juce::MathConstants<double>::pi * freq);
    return (c.b1 * s + c.b2 * s * s + c.b3 * s * s * s) / (c.a0 + c.a1 * s + c.a2 * s * s + c.a3 * s * s * s);
}

double db (cplx h) { return 20.0 * std::log10 (std::max (std::abs (h), 1.0e-12)); }
} // namespace

class ToneStackTests final : public juce::UnitTest
{
public:
    ToneStackTests() : juce::UnitTest ("Tone stack (TMB)", "Dumble") {}

    void runTest() override
    {
        const double settings[][3] = { { 0.5, 0.5, 0.5 }, { 1.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 },
                                       { 0.8, 0.2, 0.3 }, { 0.2, 0.7, 0.9 }, { 0.6, 0.4, 0.05 } };
        const double freqs[] = { 30, 60, 100, 200, 400, 800, 1600, 3200, 6400, 12000 };

        beginTest ("Closed-form transfer function matches numeric nodal analysis of the netlist");
        {
            double worst = 0.0;
            for (const auto& st : settings)
                for (auto f : freqs)
                    worst = std::max (worst, std::abs (db (analogResponse (f, st[0], st[1], st[2]))
                                                     - db (referenceResponse (f, st[0], st[1], st[2]))));

            logMessage ("max |analog - MNA| = " + juce::String (worst, 4) + " dB");
            expectLessThan (worst, 0.01);
        }

        beginTest ("Digital filter (bilinear, 4x rate) tracks the analog response below 10 kHz");
        {
            constexpr double fs = 192000.0;
            ToneStackTMB ts;

            double worst = 0.0;

            for (const auto& k : { std::array<float, 3> { 5.0f, 5.0f, 5.0f },
                                   std::array<float, 3> { 10.0f, 0.0f, 10.0f },
                                   std::array<float, 3> { 2.0f, 8.0f, 3.0f } })
            {
                ts.setTreble (k[0]);
                ts.setMiddle (k[1]);
                ts.setBass (k[2]);
                ts.prepare ({ fs, 512, 1 });

                std::array<double, 4> bz, az;
                ts.getDigitalCoefficients (bz, az);

                double t, m, l;
                ToneStackTMB::knobsToWipers (k[0], k[1], k[2], t, m, l);

                for (auto f : freqs)
                {
                    if (f > 10000.0)
                        continue;

                    const auto w = 2.0 * juce::MathConstants<double>::pi * f / fs;
                    const auto z1 = std::polar (1.0, -w);
                    const auto num = bz[0] + bz[1] * z1 + bz[2] * z1 * z1 + bz[3] * z1 * z1 * z1;
                    const auto den = az[0] + az[1] * z1 + az[2] * z1 * z1 + az[3] * z1 * z1 * z1;
                    worst = std::max (worst, std::abs (db (num / den) - db (analogResponse (f, t, m, l))));
                }
            }

            logMessage ("max |digital - analog| = " + juce::String (worst, 4) + " dB");
            expectLessThan (worst, 0.5);
        }
    }
};

static ToneStackTests toneStackTests;
