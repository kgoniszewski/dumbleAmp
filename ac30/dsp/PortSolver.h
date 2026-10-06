#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "NodalNetwork.h"
#include "dsp/KorenTable.h"
#include "dsp/TriodeModel.h"

namespace ac30
{
/**
    Device side of a NodalNetwork stage: n nonlinear port voltages v with device currents i(v)
    injected into the network. Solves  F(v) = v - v_oc - Z i(v) = 0  by Newton,
    J = I - Z G,  G = di/dv, with a predictor warm start, step limiting and an iteration cap
    (bounded worst-case cost on the audio thread).
*/
template <int N>
struct PortSolver
{
    using Vec = std::array<double, (size_t) (N)>;
    using Mat = std::array<double, (size_t) (N * N)>;

    std::array<int, (size_t) N> port {};
    int n = N;
    Vec v {}, vPrev {}, i {};
    Mat G {};
    double maxStep = 20.0;   // volts per Newton step
    double tolerance = 2.0e-3;  // volts: the accepted step is followed by its first-order current update
    int maxIterations = 6;
    long long totalIterations = 0, totalSolves = 0; // diagnostics
    Mat zCache {};
    unsigned zVersion = ~0u;

    /** dev (v, i, G): fills currents and Jacobian (G is cleared before every call). */
    template <typename Net, typename Devices>
    int solve (Net& net, Devices&& dev, bool predict = true) noexcept
    {
        Vec voc {};

        for (int p = 0; p < n; ++p)
            net.setCurrent (port[(size_t) p], 0.0);

        if (net.getVersion() != zVersion)
        {
            zVersion = net.getVersion();
            for (int p = 0; p < n; ++p)
                for (int q = 0; q < n; ++q)
                    zCache[(size_t) (p * N + q)] = net.impedance (port[(size_t) p], port[(size_t) q]);
        }
        const auto& z = zCache;

        for (int p = 0; p < n; ++p)
            voc[(size_t) p] = net.portVoltage (port[(size_t) p]);

        // linear predictor from the last two samples
        if (predict)
            for (int p = 0; p < n; ++p)
            {
                const auto guess = 2.0 * v[(size_t) p] - vPrev[(size_t) p];
                vPrev[(size_t) p] = v[(size_t) p];
                v[(size_t) p] = guess;
            }

        int it = 0;
        bool converged = false;
        Vec step {};
        for (; it < maxIterations; ++it)
        {
            G.fill (0.0);
            dev (v, i, G);

            Mat j {};
            Vec f {};
            residualAndJacobian (z, voc, f, j);

            if (! solveLinear<N> (j, f, n))
                break;

            double largest = 0.0;
            for (int p = 0; p < n; ++p)
            {
                const auto d = std::max (-maxStep, std::min (maxStep, f[(size_t) p]));
                step[(size_t) p] = -d;
                v[(size_t) p] -= d;
                largest = std::max (largest, std::abs (d));
            }

            if (largest < tolerance)
            {
                ++it;
                converged = true;
                break;
            }
        }

        // device currents at the final point: once converged, the last step is small and the
        // first-order update of the last evaluation is exact to O(step^2); otherwise evaluate again
        if (converged)
        {
            for (int p = 0; p < n; ++p)
                for (int c = 0; c < n; ++c)
                    i[(size_t) p] += G[(size_t) (p * N + c)] * step[(size_t) c];
        }
        else
        {
            G.fill (0.0);
            dev (v, i, G);
        }

        for (int p = 0; p < n; ++p)
            net.setCurrent (port[(size_t) p], i[(size_t) p]);

        totalIterations += it;
        ++totalSolves;
        return it;
    }

    /** F = v - v_oc - Z i,  J = I - Z G. G is sparse (each device touches 2..3 ports): only its
        non-zero entries are multiplied in, which keeps an iteration ~O(n * nnz) instead of O(n^3). */
    void residualAndJacobian (const Mat& z, const Vec& voc, Vec& f, Mat& j) const noexcept
    {
        for (int r = 0; r < n; ++r)
        {
            double zi = 0.0;
            for (int c = 0; c < n; ++c)
                zi += z[(size_t) (r * N + c)] * i[(size_t) c];
            f[(size_t) r] = v[(size_t) r] - voc[(size_t) r] - zi;
            j[(size_t) (r * N + r)] = 1.0;
        }

        for (int k = 0; k < n; ++k)
            for (int c = 0; c < n; ++c)
            {
                const auto gkc = G[(size_t) (k * N + c)];
                if (std::abs (gkc) <= 0.0)
                    continue;
                for (int r = 0; r < n; ++r)
                    j[(size_t) (r * N + c)] -= z[(size_t) (r * N + k)] * gkc;
            }
    }

    /** Damped DC solve (not real-time): uses the network's DC transfer. Leaves the currents set. */
    template <typename Net, typename Devices>
    void solveDC (Net& net, Devices&& dev, int iterations = 400) noexcept
    {
        Vec voc {};
        Mat z {};

        for (int p = 0; p < n; ++p)
            net.setCurrent (port[(size_t) p], 0.0);

        net.buildDC();
        for (int p = 0; p < n; ++p)
        {
            voc[(size_t) p] = net.dcPortVoltage (port[(size_t) p]);
            for (int q = 0; q < n; ++q)
                z[(size_t) (p * N + q)] = net.dcImpedance (port[(size_t) p], port[(size_t) q]);
        }

        for (int it = 0; it < iterations; ++it)
        {
            G.fill (0.0);
            dev (v, i, G);

            Mat j {};
            Vec f {};
            residualAndJacobian (z, voc, f, j);

            if (! solveLinear<N> (j, f, n))
                break;

            for (int p = 0; p < n; ++p)
                v[(size_t) p] -= 0.5 * std::max (-5.0, std::min (5.0, f[(size_t) p]));
        }

        G.fill (0.0);
        dev (v, i, G);
        for (int p = 0; p < n; ++p)
            net.setCurrent (port[(size_t) p], i[(size_t) p]);

        vPrev = v;
    }
};

//==============================================================================
/**
    Grid diode of the Koren SPICE models: an ideal diode (IS = 1 nA) in series with RGI, solved
    exactly. With w = W(x) (Lambert W), I = Vt/R w - IS, x = IS R / Vt exp((V + IS R) / Vt);
    w + ln w = ln x is solved by Newton in the log domain (no overflow). Returns the current,
    writes dI/dV. Matters where a grid sits near conduction (the Top Boost cathode follower).
*/
inline double gridDiode (double v, double rgi, double& dIdV) noexcept
{
    constexpr double is = 1.0e-9, vt = 0.025852;
    if (v < -0.3)
    {
        dIdV = 0.0;
        return 0.0;
    }

    const auto lnx = std::log (is * rgi / vt) + (v + is * rgi) / vt;
    auto w = lnx > 1.0 ? lnx - std::log (lnx) : std::exp (lnx);
    for (int it = 0; it < 4; ++it)
        w -= (w + std::log (w) - lnx) / (1.0 + 1.0 / w);

    const auto i = std::fmax (0.0, vt / rgi * w - is);
    dIdV = 1.0 / (rgi + vt / (i + is));
    return i;
}

//==============================================================================
/** Adds a triode (plate, grid, cathode port indices into the solver vectors; -1 = node not a port,
    with its voltage given) to the device currents. Grid conduction through RGI is included. */
struct TriodeDevice
{
    const dumble::KorenTable* table = nullptr;
    dumble::KorenTriodeParams params = dumble::k12AX7;

    template <int N>
    void add (const std::array<double, (size_t) (N)>& v, std::array<double, (size_t) (N)>& i, std::array<double, (size_t) (N * N)>& G,
              int pp, int pg, int pk, double fixedPlate = 0.0, double fixedGrid = 0.0, double fixedCathode = 0.0,
              double* plateCurrent = nullptr, double* gridCurrentOut = nullptr) const noexcept
    {
        const auto vp = pp >= 0 ? v[(size_t) pp] : fixedPlate;
        const auto vg = pg >= 0 ? v[(size_t) pg] : fixedGrid;
        const auto vk = pk >= 0 ? v[(size_t) pk] : fixedCathode;

        const auto t = table != nullptr ? table->evaluate ((float) (vg - vk), (float) (vp - vk))
                                        : dumble::korenTriode (params, (float) (vg - vk), (float) (vp - vk));
        double gI = 0.0;
        const auto ig = gridDiode (vg - vk, (double) params.rgi, gI);

        const double ip = t.ip, gG = t.dIdVgk, gP = t.dIdVpk;

        if (plateCurrent != nullptr)    *plateCurrent = ip;
        if (gridCurrentOut != nullptr)  *gridCurrentOut = ig;

        // currents injected into the nodes: plate -ip, grid -ig, cathode +ip + ig
        const auto addG = [&G] (int r, int c, double value)
        {
            if (r >= 0 && c >= 0)
                G[(size_t) (r * N + c)] += value;
        };

        if (pp >= 0)
        {
            i[(size_t) pp] -= ip;
            addG (pp, pp, -gP);
            addG (pp, pg, -gG);
            addG (pp, pk, gG + gP);
        }
        if (pg >= 0)
        {
            i[(size_t) pg] -= ig;
            addG (pg, pg, -gI);
            addG (pg, pk, gI);
        }
        if (pk >= 0)
        {
            i[(size_t) pk] += ip + ig;
            addG (pk, pp, gP);
            addG (pk, pg, gG + gI);
            addG (pk, pk, -(gG + gP + gI));
        }
    }
};

/** Grid diode only (e.g. a power-tube grid whose cathode is solved elsewhere): current into the grid. */
template <int N>
inline double addGridDiode (const std::array<double, (size_t) (N)>& v, std::array<double, (size_t) (N)>& i, std::array<double, (size_t) (N * N)>& G,
                            int pg, double cathodeVolts, double scale, float rgi) noexcept
{
    double d = 0.0;
    const auto ig = scale * gridDiode (v[(size_t) pg] - cathodeVolts, (double) rgi, d);
    i[(size_t) pg] -= ig;
    G[(size_t) (pg * N + pg)] -= scale * d;
    return ig;
}

/** Soft-saturating op-amp output: linear up to 80 % of the swing, smooth knee into the rails. */
inline float opAmpClip (float x, float swing) noexcept
{
    const auto knee = 0.8f * swing;
    const auto a = std::abs (x);
    if (a <= knee)
        return x;
    const auto room = swing - knee;
    const auto over = (a - knee) / room;
    return std::copysign (knee + room * over / std::sqrt (1.0f + over * over), x);
}
} // namespace ac30
