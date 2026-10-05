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
    float vct = 0.0f;    // contact-potential offset added to Vgk
    float rgi = 2000.0f; // grid-current series resistance [ohm]
};

// Parameter sets as used by the SSS #002 LTspice reconstruction (Koren_Tubes.INC).
inline constexpr KorenTriodeParams k12AX7 { 100.0f, 1.4f, 1060.0f, 600.0f, 300.0f };            // original Koren
inline constexpr KorenTriodeParams k5751  { 84.75f, 1.291f, 1137.3f, 415.40f, 1515.2f, 0.40f }; // GE data sheet
inline constexpr KorenTriodeParams k7025  { 102.50f, 1.409f, 1598.8f, 813.82f, 44.9f, 0.50f };  // Sylvania manual

struct KorenPentodeParams
{
    float mu, ex, kg1, kg2, kp, kvb;
    float rgi = 1000.0f;
};

inline constexpr KorenPentodeParams k6L6GC { 9.88f, 1.442f, 1686.6f, 4500.0f, 30.98f, 19.4f }; // GE data sheet

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
    const auto z = p.kp * (1.0f / p.mu + (vgk + p.vct) / r);

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
    const auto dE1dVpk = vpk > 0.0f ? sp / p.kp - u * u * (vgk + p.vct) * sg / (r * r * r) : 0.0f;

    return { ip, dIdE1 * dE1dVgk, dIdE1 * dE1dVpk };
}

/**
    Grid current: the Koren SPICE models use a diode (IS = 1 nA) in series with RGI. Here the diode is
    a smooth knee at ~0.36 V (its drop at ~1 mA) and the current is limited by RGI.
*/
inline float gridCurrent (float vgk, float rgi, float& dIdV) noexcept
{
    constexpr float kKnee = 0.36f, kSoft = 0.05f;
    const auto z = (vgk - kKnee) / kSoft;

    if (z < -20.0f)
    {
        dIdV = 0.0f;
        return 0.0f;
    }

    const auto ez = std::exp (std::min (z, 30.0f));
    dIdV = ez / (1.0f + ez) / rgi;
    return kSoft * (z > 30.0f ? z : std::log (1.0f + ez)) / rgi;
}

/** Grid-dependent factor of the Koren beam-tetrode/pentode model: Ip = gridTerm * atan(Vpk / kvb). */
inline float korenPentodeGridTerm (const KorenPentodeParams& p, float vg1k, float vg2k) noexcept
{
    const auto g2 = std::max (vg2k, 1.0f);
    const auto z = p.kp * (1.0f / p.mu + vg1k / g2);
    const auto e1 = g2 / p.kp * detail::softplus (z);

    return e1 <= 1.0e-9f ? 0.0f : 2.0f * std::pow (e1, p.ex) / p.kg1;
}

/** Koren screen-grid current: Ig2 = (Vg2/mu + Vg1)^ex / kg2. */
inline float korenScreenCurrent (const KorenPentodeParams& p, float vg1k, float vg2k) noexcept
{
    const auto x = vg2k / p.mu + vg1k;
    return x <= 0.0f ? 0.0f : std::pow (x, p.ex) / p.kg2;
}

/** Koren beam-tetrode/pentode plate current. */
inline float korenPentode (const KorenPentodeParams& p, float vg1k, float vg2k, float vpk) noexcept
{
    return korenPentodeGridTerm (p, vg1k, vg2k) * std::atan (std::max (vpk, 0.0f) / p.kvb);
}

} // namespace dumble
