#pragma once

#include <array>
#include <cmath>

namespace dumble
{
/**
    Small linear R/L/C network solved by modified nodal analysis, discretised with trapezoidal
    companion models. Used for every passive section of the SSS #002 schematic (tone stack,
    volume/bright/deep network, High/Low filters with the 300 mH choke, master/accent).

    The network is driven by one voltage source (e.g. a tube plate) through a tiny series
    resistance. Because the network is linear, the current it draws from the source is an affine
    function of the source voltage for the current sample — sourceCurrentAffine() returns that
    function, so a nonlinear tube stage can include the exact network load in its own Newton solve
    before step() commits the sample.

    Runtime form: with h the K companion-source histories of the reactive elements, every node
    voltage is x = P h + q vs. build() derives P, q and the branch matrix D (v = D h + d vs) from the
    nodal inverse, so step() costs O(K^2) instead of O(N^2) and node voltages are evaluated lazily.

    Real-time contract: all storage is std::array; build() (matrix inversion, O(N^3)) only runs when
    a component value changes and never allocates.
*/
template <int MaxNodes, int MaxElements>
class LinearNetwork
{
public:
    static constexpr int ground = -1;

    int addResistor (int a, int b, double ohms)      { return add (Type::resistor, a, b, ohms); }
    int addCapacitor (int a, int b, double farads)   { return add (Type::capacitor, a, b, farads); }
    int addInductor (int a, int b, double henries)   { return add (Type::inductor, a, b, henries); }

    void setNumNodes (int n) noexcept   { numNodes = n; }
    void setSourceNode (int node) noexcept { sourceNode = node; }

    /** Changes a component value; call build() afterwards (may be done on the audio thread). */
    void setValue (int element, double value) noexcept
    {
        auto& e = elements[(size_t) element];
        if (std::abs (e.value - value) > 1.0e-12 * std::abs (value))
        {
            e.value = value;
            dirty = true;
        }
    }

    void prepare (double sampleRate) noexcept
    {
        T = 1.0 / sampleRate;
        dirty = true;
        build();
    }

    void build() noexcept
    {
        if (! dirty)
            return;

        dirty = false;
        std::array<double, MaxNodes * MaxNodes> a {};

        for (int i = 0; i < numElements; ++i)
        {
            auto& e = elements[(size_t) i];
            e.g = conductance (e);
            stamp (a, e.a, e.b, e.g);
        }

        if (sourceNode >= 0)
            a[(size_t) (sourceNode * MaxNodes + sourceNode)] += kSourceConductance;

        std::array<double, MaxNodes * MaxNodes> inv {};
        invertInto (a, inv);
        deriveStateSpace (inv);
    }

    /** Initialise all reactive elements to the DC steady state for a constant source voltage. */
    void initialiseDC (double sourceVolts) noexcept
    {
        // DC: capacitors open, inductors short (tiny resistance)
        std::array<double, MaxNodes * MaxNodes> a {};
        for (int i = 0; i < numElements; ++i)
        {
            const auto& e = elements[(size_t) i];
            const auto g = e.type == Type::resistor ? 1.0 / e.value
                         : e.type == Type::inductor ? 1.0e3 : 1.0e-12;
            stamp (a, e.a, e.b, g);
        }
        if (sourceNode >= 0)
            a[(size_t) (sourceNode * MaxNodes + sourceNode)] += kSourceConductance;

        std::array<double, MaxNodes * MaxNodes> inv {};
        invertInto (a, inv);

        std::array<double, MaxNodes> xdc {};
        if (sourceNode >= 0)
            for (int n = 0; n < numNodes; ++n)
                xdc[(size_t) n] = inv[(size_t) (n * MaxNodes + sourceNode)] * kSourceConductance * sourceVolts;

        const auto at = [&xdc] (int node) { return node < 0 ? 0.0 : xdc[(size_t) node]; };

        for (int k = 0; k < numReactive; ++k)
        {
            const auto& e = elements[(size_t) reactive[(size_t) k]];
            const auto v = at (e.a) - at (e.b);
            h[(size_t) k] = e.type == Type::capacitor ? e.g * v             // i = 0
                                                      : v * 1.0e3 - e.g * v; // i_dc - g*v with v ~ 0
            branchV[(size_t) k] = v;
        }

        hUsed = h;
        lastSource = sourceVolts;
    }

    void reset() noexcept
    {
        h.fill (0.0);
        hUsed.fill (0.0);
        branchV.fill (0.0);
        lastSource = 0.0;
    }

    /** Current drawn from the source this sample: i(vs) = alpha * vs + beta. */
    void sourceCurrentAffine (double& alpha, double& beta) const noexcept
    {
        alpha = sourceAlpha;
        beta = sourceNode >= 0 ? -kSourceConductance * dotP (sourceNode, h) : 0.0;
    }

    /** Advance one sample with the given source voltage. */
    void step (double sourceVolts) noexcept
    {
        hUsed = h;
        lastSource = sourceVolts;

        for (int k = 0; k < numReactive; ++k)
        {
            const auto* row = &D[(size_t) (k * MaxElements)];
            double v = d[(size_t) k] * sourceVolts;
            for (int j = 0; j < numReactive; ++j)
                v += row[j] * hUsed[(size_t) j];

            branchV[(size_t) k] = v;
            // capacitor: i = g v - h, h' = g v + i;  inductor: i = g v + h, h' = i + g v
            h[(size_t) k] = twoG[(size_t) k] * v + hSign[(size_t) k] * hUsed[(size_t) k];
        }
    }

    /** Node voltage of the last step(). */
    double voltage (int node) const noexcept
    {
        return node < 0 ? 0.0 : dotP (node, hUsed) + q[(size_t) node] * lastSource;
    }

    /** Current through an element from its node a to node b (valid after step()). */
    double current (int element) const noexcept
    {
        const auto& e = elements[(size_t) element];
        if (e.type == Type::resistor)
            return (voltage (e.a) - voltage (e.b)) / e.value;

        // after step() the history holds g*v + i (cap) or i + g*v (inductor)
        const auto k = (size_t) e.slot;
        return h[k] - e.g * branchV[k];
    }

private:
    static constexpr double kSourceConductance = 1.0; // 1 ohm source resistance

    enum class Type { resistor, capacitor, inductor };

    struct Element
    {
        Type type = Type::resistor;
        int a = ground, b = ground, slot = -1;
        double value = 1.0, g = 0.0;
    };

    int add (Type type, int a, int b, double value)
    {
        auto& e = elements[(size_t) numElements];
        e.type = type;
        e.a = a;
        e.b = b;
        e.value = value;

        if (type != Type::resistor)
        {
            e.slot = numReactive;
            reactive[(size_t) numReactive++] = numElements;
        }

        dirty = true;
        return numElements++;
    }

    double conductance (const Element& e) const noexcept
    {
        switch (e.type)
        {
            case Type::resistor:  return 1.0 / e.value;
            case Type::capacitor: return 2.0 * e.value / T;
            case Type::inductor:  return T / (2.0 * e.value);
        }
        return 0.0;
    }

    /** x = P h + q vs, v_branch = D h + d vs, from the nodal inverse. */
    void deriveStateSpace (const std::array<double, MaxNodes * MaxNodes>& inv) noexcept
    {
        const auto invAt = [&inv] (int row, int col) { return col < 0 ? 0.0 : inv[(size_t) (row * MaxNodes + col)]; };

        for (int n = 0; n < numNodes; ++n)
        {
            for (int k = 0; k < numReactive; ++k)
            {
                const auto& e = elements[(size_t) reactive[(size_t) k]];
                // a capacitor's history is injected into node a, an inductor's is drawn from it
                const auto sign = e.type == Type::capacitor ? 1.0 : -1.0;
                P[(size_t) (n * MaxElements + k)] = sign * (invAt (n, e.a) - invAt (n, e.b));
            }
            q[(size_t) n] = sourceNode >= 0 ? kSourceConductance * invAt (n, sourceNode) : 0.0;
        }

        const auto pAt = [this] (int node, int k) { return node < 0 ? 0.0 : P[(size_t) (node * MaxElements + k)]; };
        const auto qAt = [this] (int node) { return node < 0 ? 0.0 : q[(size_t) node]; };

        for (int k = 0; k < numReactive; ++k)
        {
            const auto& e = elements[(size_t) reactive[(size_t) k]];
            for (int j = 0; j < numReactive; ++j)
                D[(size_t) (k * MaxElements + j)] = pAt (e.a, j) - pAt (e.b, j);
            d[(size_t) k] = qAt (e.a) - qAt (e.b);
            twoG[(size_t) k] = 2.0 * e.g;
            hSign[(size_t) k] = e.type == Type::capacitor ? -1.0 : 1.0;
        }

        sourceAlpha = sourceNode >= 0 ? kSourceConductance * (1.0 - q[(size_t) sourceNode]) : 0.0;
    }

    double dotP (int node, const std::array<double, MaxElements>& state) const noexcept
    {
        const auto* row = &P[(size_t) (node * MaxElements)];
        double acc = 0.0;
        for (int k = 0; k < numReactive; ++k)
            acc += row[k] * state[(size_t) k];
        return acc;
    }

    static void stamp (std::array<double, MaxNodes * MaxNodes>& a, int i, int j, double g) noexcept
    {
        if (i >= 0) a[(size_t) (i * MaxNodes + i)] += g;
        if (j >= 0) a[(size_t) (j * MaxNodes + j)] += g;
        if (i >= 0 && j >= 0)
        {
            a[(size_t) (i * MaxNodes + j)] -= g;
            a[(size_t) (j * MaxNodes + i)] -= g;
        }
    }

    void invertInto (std::array<double, MaxNodes * MaxNodes>& a, std::array<double, MaxNodes * MaxNodes>& inv) const noexcept
    {
        const int n = numNodes;
        inv.fill (0.0);
        for (int i = 0; i < n; ++i)
            inv[(size_t) (i * MaxNodes + i)] = 1.0;

        for (int col = 0; col < n; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs (a[(size_t) (r * MaxNodes + col)]) > std::abs (a[(size_t) (pivot * MaxNodes + col)]))
                    pivot = r;

            if (pivot != col)
                for (int c = 0; c < n; ++c)
                {
                    std::swap (a[(size_t) (col * MaxNodes + c)], a[(size_t) (pivot * MaxNodes + c)]);
                    std::swap (inv[(size_t) (col * MaxNodes + c)], inv[(size_t) (pivot * MaxNodes + c)]);
                }

            const auto d0 = a[(size_t) (col * MaxNodes + col)];
            if (std::abs (d0) < 1.0e-300)
                continue;

            const auto invD = 1.0 / d0;
            for (int c = 0; c < n; ++c)
            {
                a[(size_t) (col * MaxNodes + c)] *= invD;
                inv[(size_t) (col * MaxNodes + c)] *= invD;
            }

            for (int r = 0; r < n; ++r)
            {
                if (r == col)
                    continue;
                const auto f = a[(size_t) (r * MaxNodes + col)];
                if (f == 0.0)
                    continue;
                for (int c = 0; c < n; ++c)
                {
                    a[(size_t) (r * MaxNodes + c)] -= f * a[(size_t) (col * MaxNodes + c)];
                    inv[(size_t) (r * MaxNodes + c)] -= f * inv[(size_t) (col * MaxNodes + c)];
                }
            }
        }
    }

    std::array<Element, MaxElements> elements {};
    std::array<int, MaxElements> reactive {};
    std::array<double, MaxNodes * MaxElements> P {};
    std::array<double, MaxElements * MaxElements> D {};
    std::array<double, MaxNodes> q {};
    std::array<double, MaxElements> d {}, twoG {}, hSign {}, h {}, hUsed {}, branchV {};
    int numElements = 0, numReactive = 0, numNodes = 0, sourceNode = ground;
    double T = 1.0 / 48000.0, lastSource = 0.0, sourceAlpha = 0.0;
    bool dirty = true;
};
} // namespace dumble
