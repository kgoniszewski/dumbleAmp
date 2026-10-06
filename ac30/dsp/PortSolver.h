#pragma once

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
    double tolerance = 1.0e-5;
    int maxIterations = 6;

    /** dev (v, i, G): fills currents and Jacobian (G is cleared before every call). */
    template <typename Net, typename Devices>
    int solve (Net& net, Devices&& dev, bool predict = true) noexcept
    {
        Vec voc {};
        Mat z {};

        for (int p = 0; p < n; ++p)
            net.setCurrent (port[(size_t) p], 0.0);

        for (int p = 0; p < n; ++p)
        {
            voc[(size_t) p] = net.portVoltage (port[(size_t) p]);
            for (int q = 0; q < n; ++q)
                z[(size_t) (p * N + q)] = net.impedance (port[(size_t) p], port[(size_t) q]);
        }

        // linear predictor from the last two samples
        if (predict)
            for (int p = 0; p < n; ++p)
            {
                const auto guess = 2.0 * v[(size_t) p] - vPrev[(size_t) p];
                vPrev[(size_t) p] = v[(size_t) p];
                v[(size_t) p] = guess;
            }

        int it = 0;
        for (; it < maxIterations; ++it)
        {
            G.fill (0.0);
            dev (v, i, G);

            Mat j {};
            Vec f {};
            for (int r = 0; r < n; ++r)
            {
                double zi = 0.0;
                for (int c = 0; c < n; ++c)
                {
                    zi += z[(size_t) (r * N + c)] * i[(size_t) c];
                    double zg = 0.0;
                    for (int k = 0; k < n; ++k)
                        zg += z[(size_t) (r * N + k)] * G[(size_t) (k * N + c)];
                    j[(size_t) (r * N + c)] = (r == c ? 1.0 : 0.0) - zg;
                }
                f[(size_t) r] = v[(size_t) r] - voc[(size_t) r] - zi;
            }

            if (! solveLinear<N> (j, f, n))
                break;

            double largest = 0.0;
            for (int p = 0; p < n; ++p)
            {
                const auto d = std::fmax (-maxStep, std::fmin (maxStep, f[(size_t) p]));
                v[(size_t) p] -= d;
                largest = std::fmax (largest, std::abs (d));
            }

            if (largest < tolerance)
            {
                ++it;
                break;
            }
        }

        G.fill (0.0);
        dev (v, i, G);
        for (int p = 0; p < n; ++p)
            net.setCurrent (port[(size_t) p], i[(size_t) p]);

        return it;
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
            for (int r = 0; r < n; ++r)
            {
                double zi = 0.0;
                for (int c = 0; c < n; ++c)
                {
                    zi += z[(size_t) (r * N + c)] * i[(size_t) c];
                    double zg = 0.0;
                    for (int k = 0; k < n; ++k)
                        zg += z[(size_t) (r * N + k)] * G[(size_t) (k * N + c)];
                    j[(size_t) (r * N + c)] = (r == c ? 1.0 : 0.0) - zg;
                }
                f[(size_t) r] = v[(size_t) r] - voc[(size_t) r] - zi;
            }

            if (! solveLinear<N> (j, f, n))
                break;

            for (int p = 0; p < n; ++p)
                v[(size_t) p] -= 0.5 * std::fmax (-5.0, std::fmin (5.0, f[(size_t) p]));
        }

        G.fill (0.0);
        dev (v, i, G);
        for (int p = 0; p < n; ++p)
            net.setCurrent (port[(size_t) p], i[(size_t) p]);

        vPrev = v;
    }
};

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
        float dIg = 0.0f;
        const auto ig = (double) dumble::gridCurrent ((float) (vg - vk), params.rgi, dIg);

        const double ip = t.ip, gG = t.dIdVgk, gP = t.dIdVpk, gI = dIg;

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
    float d = 0.0f;
    const auto ig = scale * (double) dumble::gridCurrent ((float) (v[(size_t) pg] - cathodeVolts), rgi, d);
    i[(size_t) pg] -= ig;
    G[(size_t) (pg * N + pg)] -= scale * (double) d;
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
