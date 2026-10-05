#pragma once

#include <algorithm>
#include <cmath>

namespace dumble
{
/**
    Potentiometer tapers as defined in the reconstruction's potentiometer_standard.lib.
    "ratio" is the fraction of the track between the wiper and the B (ground) end:
        R(A-wiper) = Rtot * (1 - ratio),  R(wiper-B) = Rtot * ratio.
*/
inline double potLinear (float knob0to10) noexcept
{
    return std::clamp ((double) knob0to10 * 0.1, 1.0e-5, 0.99999);
}

/** pot_pow with Rtap = 10 % of Rtot at half rotation (audio taper): ratio = w^(ln 0.1 / ln 0.5). */
inline double potAudio (float knob0to10) noexcept
{
    static constexpr double exponent = 3.3219280948873622; // ln(0.1) / ln(0.5)
    return std::clamp (std::pow (potLinear (knob0to10), exponent), 1.0e-5, 0.99999);
}

/** Switch contact resistances used in the networks. */
inline constexpr double kSwitchClosed = 1.0;   // ohm
inline constexpr double kSwitchOpen   = 1.0e10; // ohm
} // namespace dumble
