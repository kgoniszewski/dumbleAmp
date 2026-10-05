#pragma once

#include <array>
#include <cmath>

#include "TriodeModel.h"

namespace dumble
{
/** Uniform 1-D table of a smooth function and its exact derivative, read with cubic Hermite interpolation. */
template <int Size>
class HermiteTable
{
public:
    template <typename Fn>
    void build (float xMin, float xMax, Fn&& valueAndSlope) noexcept
    {
        lo = xMin;
        step = (xMax - xMin) / (float) (Size - 1);
        invStep = 1.0f / step;
        for (int i = 0; i < Size; ++i)
        {
            double slope = 0.0;
            const auto v = valueAndSlope ((double) xMin + (double) i * step, slope);
            values[(size_t) i] = (float) v;
            slopes[(size_t) i] = (float) (slope * step); // pre-scaled to the cell width
        }
    }

    /** Value at x (clamped to the table range); writes dValue/dx. */
    float evaluate (float x, float& derivative) const noexcept
    {
        const auto pos = std::min (std::max ((x - lo) * invStep, 0.0f), (float) (Size - 1) - 1.0e-3f);
        const auto i = (int) pos;
        const auto t = pos - (float) i;
        const auto p0 = values[(size_t) i], p1 = values[(size_t) i + 1];
        const auto m0 = slopes[(size_t) i], m1 = slopes[(size_t) i + 1];
        const auto t2 = t * t, t3 = t2 * t;

        derivative = ((6.0f * t2 - 6.0f * t) * (p0 - p1) + (3.0f * t2 - 4.0f * t + 1.0f) * m0
                      + (3.0f * t2 - 2.0f * t) * m1) * invStep;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 + (t3 - 2.0f * t2 + t) * m0
             + (-2.0f * t3 + 3.0f * t2) * p1 + (t3 - t2) * m1;
    }

    float evaluate (float x) const noexcept
    {
        float unused = 0.0f;
        return evaluate (x, unused);
    }

private:
    std::array<float, Size> values {}, slopes {};
    float lo = 0.0f, step = 1.0f, invStep = 1.0f;
};

/**
    Tabulated Koren beam-tetrode model for the power section. The grid term factorises exactly into
    one-dimensional functions, so no 2-D table is needed:

        gridTerm(Vg1, Vg2) = Vg2^ex * F(Vg1 / Vg2),  F(s) = 2 / kg1 * (softplus(kp (1/mu + s)) / kp)^ex
        Ig2(Vg1, Vg2)      = P(Vg2 / mu + Vg1) / kg2, P(x) = x^ex
        Ip                 = gridTerm * A(Vpk),       A(v) = atan(v / kvb)

    Built once (static initialisation, outside the audio thread when first used from prepare()).
*/
class PentodeTable
{
public:
    static const PentodeTable& forTube6L6GC()
    {
        static const PentodeTable t (k6L6GC);
        return t;
    }

    explicit PentodeTable (const KorenPentodeParams& tube) : p (tube)
    {
        const double mu = p.mu, ex = p.ex, kp = p.kp, kg1 = p.kg1, kvb = p.kvb;

        gridShape.build (kSMin, kSMax, [=] (double s, double& slope)
        {
            const auto z = kp * (1.0 / mu + s);
            const auto sp = z > 30.0 ? z : std::log1p (std::exp (z));
            const auto sig = 1.0 / (1.0 + std::exp (-z));
            const auto e = sp / kp;
            slope = e > 0.0 ? 2.0 / kg1 * ex * std::pow (e, ex - 1.0) * sig : 0.0;
            return 2.0 / kg1 * std::pow (e, ex);
        });

        power.build (0.0f, kPowMax, [=] (double x, double& slope)
        {
            slope = x > 0.0 ? ex * std::pow (x, ex - 1.0) : 0.0;
            return std::pow (x, ex);
        });

        plate.build (0.0f, kPlateMax, [=] (double v, double& slope)
        {
            slope = 1.0 / (kvb * (1.0 + (v / kvb) * (v / kvb)));
            return std::atan (v / kvb);
        });
    }

    /** Grid-dependent factor, equivalent to korenPentodeGridTerm(). */
    float gridTerm (float vg1k, float vg2k) const noexcept
    {
        const auto g2 = std::max (vg2k, 1.0f);
        const auto s = vg1k / g2;
        if (s < kSMin || s > kSMax || g2 > kPowMax)
            return korenPentodeGridTerm (p, vg1k, vg2k);
        return power.evaluate (g2) * gridShape.evaluate (s);
    }

    /** Screen current, equivalent to korenScreenCurrent(). */
    float screenCurrent (float vg1k, float vg2k) const noexcept
    {
        const auto x = vg2k / p.mu + vg1k;
        if (x <= 0.0f)
            return 0.0f;
        if (x > kPowMax)
            return korenScreenCurrent (p, vg1k, vg2k);
        return power.evaluate (x) / p.kg2;
    }

    /** atan(Vpk / kvb) and its derivative, for Vpk in [0, kPlateMax]. */
    float plateFactor (float vpk, float& derivative) const noexcept { return plate.evaluate (vpk, derivative); }

    static constexpr float kSMin = -0.5f, kSMax = 0.25f, kPowMax = 512.0f, kPlateMax = 1024.0f;

private:
    KorenPentodeParams p;
    HermiteTable<7501> gridShape;  // ds = 1e-4
    HermiteTable<10241> power;     // dx = 0.05 V
    HermiteTable<4097> plate;      // dv = 0.25 V
};
} // namespace dumble
