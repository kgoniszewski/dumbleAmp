#pragma once

#include <vector>

#include "TriodeModel.h"

namespace dumble
{
/**
    Pre-tabulated Koren triode model: plate current and both partial derivatives on a dense
    (Vgk, Vpk) grid, read back with bilinear interpolation. Replaces ~5 transcendental calls per
    Newton iteration with a handful of loads and FMAs.

    The tables are built once per tube type (thread-safe static initialisation) the first time a
    stage is prepared — never on the audio thread. Points outside the grid fall back to the
    analytic model, so behaviour at the extremes is unchanged.
*/
class KorenTable
{
public:
    static constexpr float kVgkMin = -12.0f, kVgkMax = 4.0f, kVgkStep = 0.02f;
    static constexpr float kVpkMin = 0.0f,   kVpkMax = 330.0f, kVpkStep = 1.0f;

    explicit KorenTable (const KorenTriodeParams& p)
        : params (p),
          numVgk ((int) ((kVgkMax - kVgkMin) / kVgkStep) + 1),
          numVpk ((int) ((kVpkMax - kVpkMin) / kVpkStep) + 1),
          table ((size_t) numVgk * (size_t) numVpk)
    {
        for (int i = 0; i < numVgk; ++i)
            for (int j = 0; j < numVpk; ++j)
                table[index (i, j)] = korenTriode (params, kVgkMin + (float) i * kVgkStep, kVpkMin + (float) j * kVpkStep);
    }

    /** Shared, lazily built table for the 12AX7. */
    static const KorenTable& get12AX7()
    {
        static const KorenTable instance (k12AX7);
        return instance;
    }

    TriodeCurrent evaluate (float vgk, float vpk) const noexcept
    {
        const auto x = (vgk - kVgkMin) * (1.0f / kVgkStep);
        const auto y = (vpk - kVpkMin) * (1.0f / kVpkStep);

        if (! (x >= 0.0f && y >= 0.0f && x < (float) (numVgk - 1) && y < (float) (numVpk - 1)))
            return korenTriode (params, vgk, vpk);

        const auto i = (int) x, j = (int) y;
        const auto fx = x - (float) i, fy = y - (float) j;

        const auto& a = table[index (i, j)];
        const auto& b = table[index (i + 1, j)];
        const auto& c = table[index (i, j + 1)];
        const auto& d = table[index (i + 1, j + 1)];

        const auto lerp2 = [fx, fy] (float va, float vb, float vc, float vd) noexcept
        {
            const auto top = va + fx * (vb - va);
            const auto bottom = vc + fx * (vd - vc);
            return top + fy * (bottom - top);
        };

        return { lerp2 (a.ip, b.ip, c.ip, d.ip),
                 lerp2 (a.dIdVgk, b.dIdVgk, c.dIdVgk, d.dIdVgk),
                 lerp2 (a.dIdVpk, b.dIdVpk, c.dIdVpk, d.dIdVpk) };
    }

private:
    size_t index (int i, int j) const noexcept { return (size_t) j * (size_t) numVgk + (size_t) i; }

    KorenTriodeParams params;
    int numVgk, numVpk;
    std::vector<TriodeCurrent> table;
};
} // namespace dumble
