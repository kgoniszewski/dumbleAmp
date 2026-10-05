#pragma once

#include <algorithm>
#include <cmath>

namespace dumble
{
// Koren vacuum tube models ("Improved VT models for SPICE simulations", N. Koren).
// All functions are pure, allocation free and noexcept — safe for the audio thread.

struct KorenTriodeParams
{
    float mu, ex, kg1, kp, kvb;
};

inline constexpr KorenTriodeParams k12AX7 { 100.0f, 1.4f, 1060.0f, 600.0f, 300.0f };
inline constexpr KorenTriodeParams k12AT7 { 60.0f, 1.35f, 460.0f, 300.0f, 300.0f };

struct KorenPentodeParams
{
    float mu, ex, kg1, kp, kvb;
};

inline constexpr KorenPentodeParams k6550 { 7.9f, 1.35f, 890.0f, 60.0f, 24.0f };

namespace detail
{
    inline float softplus (float z) noexcept
    {
        // log(1 + e^z) without overflow (logf is markedly cheaper than log1pf; for tiny e^z the
        // first-order term is exact to float precision)
        const auto ez = std::exp (std::min (z, 30.0f));
        return z > 30.0f ? z : (ez < 1.0e-4f ? ez : std::log (1.0f + ez));
    }
}

/** Plate current and its partial derivatives for a triode. */
struct TriodeCurrent
{
    float ip;      // plate current [A]
    float dIdVgk;  // dIp/dVgk [S]
    float dIdVpk;  // dIp/dVpk [S]
};

inline TriodeCurrent korenTriode (const KorenTriodeParams& p, float vgk, float vpk) noexcept
{
    const auto u = std::max (vpk, 0.0f);
    const auto r = std::sqrt (p.kvb + u * u);
    const auto z = p.kp * (1.0f / p.mu + vgk / r);

    // one exp() shared by softplus and its derivative (the logistic sigmoid)
    const auto ez = std::exp (std::min (z, 30.0f));
    const auto sp = z > 30.0f ? z : (ez < 1.0e-4f ? ez : std::log (1.0f + ez));
    const auto e1 = u / p.kp * sp;

    if (e1 <= 1.0e-9f)
        return { 0.0f, 0.0f, 0.0f };

    const auto sg = ez / (1.0f + ez);
    const auto e1PowM1 = std::exp ((p.ex - 1.0f) * std::log (e1));
    const auto ip = 2.0f * e1PowM1 * e1 / p.kg1;
    const auto dIdE1 = 2.0f * p.ex * e1PowM1 / p.kg1;

    const auto dE1dVgk = u * sg / r;
    const auto dE1dVpk = vpk > 0.0f ? sp / p.kp - u * u * vgk * sg / (r * r * r) : 0.0f;

    return { ip, dIdE1 * dE1dVgk, dIdE1 * dE1dVpk };
}

/** Grid current of a triode/pentode modelled as a soft diode (grid conduction). */
inline float gridCurrent (float vgk, float& dIdV) noexcept
{
    constexpr float kGrid = 2.0e-4f; // A / V^1.5

    if (vgk <= 0.0f)
    {
        dIdV = 0.0f;
        return 0.0f;
    }

    const auto s = std::sqrt (vgk);
    dIdV = 1.5f * kGrid * s;
    return kGrid * vgk * s;
}

/** Grid-dependent factor of the Koren beam-tetrode/pentode model: Ip = gridTerm * atan(Vpk / kvb). */
inline float korenPentodeGridTerm (const KorenPentodeParams& p, float vg1k, float vg2k) noexcept
{
    const auto g2 = std::max (vg2k, 1.0f);
    const auto z = p.kp * (1.0f / p.mu + vg1k / g2);
    const auto e1 = g2 / p.kp * detail::softplus (z);

    return e1 <= 1.0e-9f ? 0.0f : 2.0f * std::pow (e1, p.ex) / p.kg1;
}

/** Koren beam-tetrode/pentode plate current. */
inline float korenPentode (const KorenPentodeParams& p, float vg1k, float vg2k, float vpk) noexcept
{
    return korenPentodeGridTerm (p, vg1k, vg2k) * std::atan (std::max (vpk, 0.0f) / p.kvb);
}

} // namespace dumble
