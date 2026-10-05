#pragma once

#include <cstring>
#include <vector>

#include "TriodeModel.h"

namespace dumble
{
/**
    Pre-tabulated Koren triode model: plate current and both partial derivatives on a dense
    (Vgk, Vpk) grid, read back with bicubic Hermite interpolation. Replaces ~5 transcendental calls per
    Newton iteration with a handful of loads and FMAs.

    The tables are built once per tube type (thread-safe static initialisation) the first time a
    stage is prepared — never on the audio thread. Points outside the grid fall back to the
    analytic model, so behaviour at the extremes is unchanged.
*/
class KorenTable
{
public:
    static constexpr float kVgkMin = -10.0f, kVgkMax = 4.0f, kVgkStep = 0.02f;
    static constexpr float kVpkMin = 0.0f,   kVpkMax = 500.0f, kVpkStep = 1.0f;

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

    /** Shared, lazily built table for a known tube type, or nullptr (use the analytic model). */
    static const KorenTable* forTube (const KorenTriodeParams& p)
    {
        if (same (p, k5751))  { static const KorenTable t (k5751);  return &t; }
        if (same (p, k7025))  { static const KorenTable t (k7025);  return &t; }
        if (same (p, k12AX7)) { static const KorenTable t (k12AX7); return &t; }
        return nullptr;
    }

    /**
        Bicubic-Hermite read-out using the stored partial derivatives as slopes: the surface is C1
        (no derivative kinks), so small signals see no interpolation distortion.
    */
    TriodeCurrent evaluate (float vgk, float vpk) const noexcept
    {
        const auto x = (vgk - kVgkMin) * (1.0f / kVgkStep);
        const auto y = (vpk - kVpkMin) * (1.0f / kVpkStep);

        if (! (x >= 0.0f && y >= 0.0f && x < (float) (numVgk - 1) && y < (float) (numVpk - 1)))
            return korenTriode (params, vgk, vpk);

        const auto i = (int) x, j = (int) y;
        const auto u = x - (float) i, w = y - (float) j;

        const auto& a = table[index (i, j)];
        const auto& b = table[index (i + 1, j)];
        const auto& c = table[index (i, j + 1)];
        const auto& d = table[index (i + 1, j + 1)];

        // cubic Hermite basis on [0, 1]
        const auto hermite = [] (float t, float p0, float p1, float m0, float m1, float& slope) noexcept
        {
            const auto t2 = t * t, t3 = t2 * t;
            slope = (6.0f * t2 - 6.0f * t) * p0 + (3.0f * t2 - 4.0f * t + 1.0f) * m0
                  + (-6.0f * t2 + 6.0f * t) * p1 + (3.0f * t2 - 2.0f * t) * m1;
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 + (t3 - 2.0f * t2 + t) * m0
                 + (-2.0f * t3 + 3.0f * t2) * p1 + (t3 - t2) * m1;
        };

        // along Vgk on both Vpk rows (slopes scaled to the cell width)
        float sRow0 = 0.0f, sRow1 = 0.0f;
        const auto row0 = hermite (u, a.ip, b.ip, a.dIdVgk * kVgkStep, b.dIdVgk * kVgkStep, sRow0);
        const auto row1 = hermite (u, c.ip, d.ip, c.dIdVgk * kVgkStep, d.dIdVgk * kVgkStep, sRow1);

        // along Vpk between the rows, slopes linearly blended along Vgk
        const auto m0 = (a.dIdVpk + u * (b.dIdVpk - a.dIdVpk)) * kVpkStep;
        const auto m1 = (c.dIdVpk + u * (d.dIdVpk - c.dIdVpk)) * kVpkStep;
        float sCol = 0.0f;
        const auto ip = hermite (w, row0, row1, m0, m1, sCol);

        return { ip,
                 (sRow0 + w * (sRow1 - sRow0)) * (1.0f / kVgkStep),
                 sCol * (1.0f / kVpkStep) };
    }

private:
    static bool same (const KorenTriodeParams& a, const KorenTriodeParams& b) noexcept
    {
        return std::memcmp (&a, &b, sizeof (KorenTriodeParams)) == 0;
    }

    size_t index (int i, int j) const noexcept { return (size_t) j * (size_t) numVgk + (size_t) i; }

    KorenTriodeParams params;
    int numVgk, numVpk;
    std::vector<TriodeCurrent> table;
};
} // namespace dumble
