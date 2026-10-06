#pragma once

#include <array>
#include <cmath>

namespace ac30
{
/**
    Linear R/L/C network with several ports, discretised with trapezoidal companion models
    (nodal "K-method" form). Generalises dumble::LinearNetwork, which has a single voltage source.

    A port is a current injected into one node. Two kinds:
      - voltage source: the node is tied to an ideal-ish source through kSourceConductance (Norton
        form); its injection is g * V (setSource),
      - current port: the injection is a device current (tube plate, cathode, grid diode, ...).

    With h the companion histories of the K reactive elements and u the port injections,
    every node voltage is affine:   x = P h + Q u.
    For the nonlinear devices this gives, per sample, the classic port equation
        v_port = v_oc + Z i,   v_oc = P_port h + Q_port u_fixed,   Z = Q_port,port,
    which the stage solves with Newton before step() commits the sample.

    Real-time contract: std::array storage only; build() (O(N^3) inversion) runs only when a
    component value changes (pots, every 32 samples while a knob moves) and never allocates.
*/
template <int MaxNodes, int MaxElements, int MaxPorts>
class NodalNetwork
{
public:
    static constexpr int ground = -1;
    static constexpr double kSourceConductance = 1.0; // 1 ohm source resistance

    int addResistor (int a, int b, double ohms)    { return add (Type::resistor, a, b, ohms); }
    int addCapacitor (int a, int b, double farads) { return add (Type::capacitor, a, b, farads); }
    int addInductor (int a, int b, double henries) { return add (Type::inductor, a, b, henries); }

    /** Current injected into `node` (from ground). Returns the port index. */
    int addCurrentPort (int node)
    {
        ports[(size_t) numPorts] = { node, false };
        return numPorts++;
    }

    /** Ideal voltage source at `node` (through 1 ohm). Returns the port index. */
    int addVoltageSource (int node)
    {
        ports[(size_t) numPorts] = { node, true };
        dirty = true;
        return numPorts++;
    }

    void setNumNodes (int n) noexcept { numNodes = n; }

    /** Incremented by every build(): lets a solver cache its port impedances. */
    unsigned getVersion() const noexcept { return version; }
    int getNumPorts() const noexcept  { return numPorts; }

    void setValue (int element, double value) noexcept
    {
        auto& e = elements[(size_t) element];
        if (std::abs (e.value - value) > 1.0e-12 * std::abs (value))
        {
            e.value = value;
            dirty = true;
        }
    }

    double getValue (int element) const noexcept { return elements[(size_t) element].value; }

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
        ++version;
        Matrix a {};
        stampAll (a, false);
        Matrix inv {};
        invertInto (a, inv);
        deriveStateSpace (inv);
    }

    //==========================================================================
    // Per-sample use: set the fixed injections, read open-circuit port voltages, solve the devices,
    // set their currents, step().

    void setSource (int port, double volts) noexcept   { u[(size_t) port] = kSourceConductance * volts; }
    void setCurrent (int port, double amps) noexcept   { u[(size_t) port] = amps; }
    double getInjection (int port) const noexcept      { return u[(size_t) port]; }

    /** Port voltage with all injections at their present values (set device currents to 0 first for v_oc). */
    double portVoltage (int port) const noexcept { return nodeVoltage (ports[(size_t) port].node, h); }

    /** dV(node of port p) / dI(port q). */
    double impedance (int p, int q) const noexcept
    {
        const auto n = ports[(size_t) p].node;
        return n < 0 ? 0.0 : Q[(size_t) (n * MaxPorts + q)];
    }

    /** Commit one sample with the present injections. */
    void step() noexcept
    {
        hUsed = h;
        uUsed = u;

        for (int k = 0; k < numReactive; ++k)
        {
            const auto* dRow = &D[(size_t) (k * MaxElements)];
            const auto* eRow = &E[(size_t) (k * MaxPorts)];
            double v = 0.0;
            for (int j = 0; j < numReactive; ++j)
                v += dRow[j] * hUsed[(size_t) j];
            for (int p = 0; p < numPorts; ++p)
                v += eRow[p] * uUsed[(size_t) p];

            branchV[(size_t) k] = v;
            h[(size_t) k] = twoG[(size_t) k] * v + hSign[(size_t) k] * hUsed[(size_t) k];
        }
    }

    /** Node voltage of the last step(). */
    double voltage (int node) const noexcept
    {
        if (node < 0)
            return 0.0;
        double acc = 0.0;
        const auto* pRow = &P[(size_t) (node * MaxElements)];
        for (int k = 0; k < numReactive; ++k)
            acc += pRow[k] * hUsed[(size_t) k];
        const auto* qRow = &Q[(size_t) (node * MaxPorts)];
        for (int p = 0; p < numPorts; ++p)
            acc += qRow[p] * uUsed[(size_t) p];
        return acc;
    }

    /** Current through an element from node a to node b (valid after step()). */
    double current (int element) const noexcept
    {
        const auto& e = elements[(size_t) element];
        if (e.type == Type::resistor)
            return (voltage (e.a) - voltage (e.b)) / e.value;
        const auto k = (size_t) e.slot;
        return h[k] - e.g * branchV[k];
    }

    //==========================================================================
    // DC (capacitors open, inductors shorted). Not real-time: used by prepare()/reset().

    /** Builds the DC transfer x = Qdc u. Call before dcPortVoltage()/dcImpedance(). */
    void buildDC() noexcept
    {
        Matrix a {};
        stampAll (a, true);
        Matrix inv {};
        invertInto (a, inv);

        for (int n = 0; n < numNodes; ++n)
            for (int p = 0; p < numPorts; ++p)
                Qdc[(size_t) (n * MaxPorts + p)] = invAt (inv, n, ports[(size_t) p].node);
    }

    double dcNodeVoltage (int node) const noexcept
    {
        if (node < 0)
            return 0.0;
        double acc = 0.0;
        for (int p = 0; p < numPorts; ++p)
            acc += Qdc[(size_t) (node * MaxPorts + p)] * u[(size_t) p];
        return acc;
    }

    double dcPortVoltage (int port) const noexcept { return dcNodeVoltage (ports[(size_t) port].node); }

    double dcImpedance (int p, int q) const noexcept
    {
        const auto n = ports[(size_t) p].node;
        return n < 0 ? 0.0 : Qdc[(size_t) (n * MaxPorts + q)];
    }

    /** Sets every reactive element to the DC steady state of the present injections (call after build()). */
    void initialiseFromDC() noexcept
    {
        for (int k = 0; k < numReactive; ++k)
        {
            const auto& e = elements[(size_t) reactive[(size_t) k]];
            const auto v = dcNodeVoltage (e.a) - dcNodeVoltage (e.b);
            // capacitor: i = 0 -> h = g v;  inductor: v ~ 0, i = v * gShort -> h = i + g v
            h[(size_t) k] = e.type == Type::capacitor ? e.g * v : v * kInductorShort + e.g * v;
            branchV[(size_t) k] = v;
        }
        hUsed = h;
        uUsed = u;
    }

    void reset() noexcept
    {
        h.fill (0.0);
        hUsed.fill (0.0);
        branchV.fill (0.0);
        u.fill (0.0);
        uUsed.fill (0.0);
    }

private:
    using Matrix = std::array<double, (size_t) (MaxNodes * MaxNodes)>;
    static constexpr double kInductorShort = 1.0e3;
    static constexpr double kCapacitorOpen = 1.0e-12;

    enum class Type { resistor, capacitor, inductor };

    struct Element
    {
        Type type = Type::resistor;
        int a = ground, b = ground, slot = -1;
        double value = 1.0, g = 0.0;
    };

    struct Port
    {
        int node = ground;
        bool source = false;
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

    void stampAll (Matrix& a, bool dc) noexcept
    {
        for (int i = 0; i < numElements; ++i)
        {
            auto& e = elements[(size_t) i];
            e.g = conductance (e);
            const auto g = ! dc ? e.g
                         : e.type == Type::resistor ? e.g
                         : e.type == Type::inductor ? kInductorShort : kCapacitorOpen;
            stamp (a, e.a, e.b, g);
        }

        for (int p = 0; p < numPorts; ++p)
            if (ports[(size_t) p].source && ports[(size_t) p].node >= 0)
            {
                const auto n = ports[(size_t) p].node;
                a[(size_t) (n * MaxNodes + n)] += kSourceConductance;
            }
    }

    static double invAt (const Matrix& inv, int row, int col) noexcept
    {
        return (row < 0 || col < 0) ? 0.0 : inv[(size_t) (row * MaxNodes + col)];
    }

    void deriveStateSpace (const Matrix& inv) noexcept
    {
        for (int n = 0; n < numNodes; ++n)
        {
            for (int k = 0; k < numReactive; ++k)
            {
                const auto& e = elements[(size_t) reactive[(size_t) k]];
                // a capacitor's history is injected into node a, an inductor's is drawn from it
                const auto sign = e.type == Type::capacitor ? 1.0 : -1.0;
                P[(size_t) (n * MaxElements + k)] = sign * (invAt (inv, n, e.a) - invAt (inv, n, e.b));
            }
            for (int p = 0; p < numPorts; ++p)
                Q[(size_t) (n * MaxPorts + p)] = invAt (inv, n, ports[(size_t) p].node);
        }

        const auto pAt = [this] (int node, int k) { return node < 0 ? 0.0 : P[(size_t) (node * MaxElements + k)]; };
        const auto qAt = [this] (int node, int p) { return node < 0 ? 0.0 : Q[(size_t) (node * MaxPorts + p)]; };

        for (int k = 0; k < numReactive; ++k)
        {
            const auto& e = elements[(size_t) reactive[(size_t) k]];
            for (int j = 0; j < numReactive; ++j)
                D[(size_t) (k * MaxElements + j)] = pAt (e.a, j) - pAt (e.b, j);
            for (int p = 0; p < numPorts; ++p)
                E[(size_t) (k * MaxPorts + p)] = qAt (e.a, p) - qAt (e.b, p);
            twoG[(size_t) k] = 2.0 * e.g;
            hSign[(size_t) k] = e.type == Type::capacitor ? -1.0 : 1.0;
        }
    }

    double nodeVoltage (int node, const std::array<double, (size_t) (MaxElements)>& hist) const noexcept
    {
        if (node < 0)
            return 0.0;
        double acc = 0.0;
        const auto* pRow = &P[(size_t) (node * MaxElements)];
        for (int k = 0; k < numReactive; ++k)
            acc += pRow[k] * hist[(size_t) k];
        const auto* qRow = &Q[(size_t) (node * MaxPorts)];
        for (int p = 0; p < numPorts; ++p)
            acc += qRow[p] * u[(size_t) p];
        return acc;
    }

    static void stamp (Matrix& a, int i, int j, double g) noexcept
    {
        if (i >= 0) a[(size_t) (i * MaxNodes + i)] += g;
        if (j >= 0) a[(size_t) (j * MaxNodes + j)] += g;
        if (i >= 0 && j >= 0)
        {
            a[(size_t) (i * MaxNodes + j)] -= g;
            a[(size_t) (j * MaxNodes + i)] -= g;
        }
    }

    void invertInto (Matrix& a, Matrix& inv) const noexcept
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
                if (std::abs (f) <= 0.0)
                    continue;
                for (int c = 0; c < n; ++c)
                {
                    a[(size_t) (r * MaxNodes + c)] -= f * a[(size_t) (col * MaxNodes + c)];
                    inv[(size_t) (r * MaxNodes + c)] -= f * inv[(size_t) (col * MaxNodes + c)];
                }
            }
        }
    }

    std::array<Element, (size_t) (MaxElements)> elements {};
    std::array<int, (size_t) (MaxElements)> reactive {};
    std::array<Port, (size_t) (MaxPorts)> ports {};
    std::array<double, (size_t) (MaxNodes * MaxElements)> P {};
    std::array<double, (size_t) (MaxNodes * MaxPorts)> Q {}, Qdc {};
    std::array<double, (size_t) (MaxElements * MaxElements)> D {};
    std::array<double, (size_t) (MaxElements * MaxPorts)> E {};
    std::array<double, (size_t) (MaxElements)> twoG {}, hSign {}, h {}, hUsed {}, branchV {};
    std::array<double, (size_t) (MaxPorts)> u {}, uUsed {};
    int numElements = 0, numReactive = 0, numNodes = 0, numPorts = 0;
    double T = 1.0 / 48000.0;
    unsigned version = 0;
    bool dirty = true;
};

//==============================================================================
/** Dense n x n linear solve (partial pivoting), n <= Max. Returns false if singular. */
template <int Max>
inline bool solveLinear (std::array<double, (size_t) (Max * Max)>& a, std::array<double, (size_t) Max>& b, int n) noexcept
{
    for (int col = 0; col < n; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < n; ++r)
            if (std::abs (a[(size_t) (r * Max + col)]) > std::abs (a[(size_t) (pivot * Max + col)]))
                pivot = r;

        const auto d0 = a[(size_t) (pivot * Max + col)];
        if (std::abs (d0) < 1.0e-300)
            return false;

        if (pivot != col)
        {
            for (int c = col; c < n; ++c)
                std::swap (a[(size_t) (col * Max + c)], a[(size_t) (pivot * Max + c)]);
            std::swap (b[(size_t) col], b[(size_t) pivot]);
        }

        const auto inv = 1.0 / d0;
        for (int r = col + 1; r < n; ++r)
        {
            const auto f = a[(size_t) (r * Max + col)] * inv;
            if (std::abs (f) <= 0.0)
                continue;
            for (int c = col; c < n; ++c)
                a[(size_t) (r * Max + c)] -= f * a[(size_t) (col * Max + c)];
            b[(size_t) r] -= f * b[(size_t) col];
        }
    }

    for (int r = n - 1; r >= 0; --r)
    {
        auto s = b[(size_t) r];
        for (int c = r + 1; c < n; ++c)
            s -= a[(size_t) (r * Max + c)] * b[(size_t) c];
        b[(size_t) r] = s / a[(size_t) (r * Max + r)];
    }

    return true;
}
} // namespace ac30
