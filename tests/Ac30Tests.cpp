#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "AllocationGuard.h"
#include "dsp/Ac30Engine.h"

using namespace ac30;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 64;

void run (Ac30Engine& engine, std::vector<float>& buffer)
{
    for (size_t pos = 0; pos < buffer.size(); pos += kBlock)
        engine.process (buffer.data() + pos, (int) std::min<size_t> (kBlock, buffer.size() - pos));
}

std::vector<float> sine (double freq, float amp, double seconds, double fs = kFs)
{
    std::vector<float> x ((size_t) (seconds * fs));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / fs);
    return x;
}

double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (auto i = from; i < to; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (to - from));
}

/** Amplitude of one frequency component (single-bin DFT over a whole number of periods). */
double toneAmplitude (const std::vector<float>& x, size_t from, size_t count, double freq, double fs)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const auto ph = 2.0 * juce::MathConstants<double>::pi * freq * (double) (from + i) / fs;
        re += x[from + i] * std::cos (ph);
        im += x[from + i] * std::sin (ph);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (double) count;
}

Ac30Settings cleanSettings (int osIndex = 1)
{
    Ac30Settings s;
    s.cabOn = false;
    s.outputDb = 0.0f;
    s.oversamplingIndex = osIndex;
    return s;
}
} // namespace

//==============================================================================
class Ac30OperatingPointTests final : public juce::UnitTest
{
public:
    Ac30OperatingPointTests() : juce::UnitTest ("AC30C2 operating point", "AC30") {}

    void runTest() override
    {
        beginTest ("Supplies, tube bias and EL84 idle match the AC30C2 design values");

        Ac30Engine engine;
        engine.setSettings (cleanSettings());
        engine.prepare (kFs, kBlock, 1);

        const auto& chain = engine.getAmpChain (1);
        const auto& power = chain.get<Ac30Engine::powerSection>();
        const auto& s = power.getSupplies();
        const auto op = chain.get<Ac30Engine::preamp>().getOperatingPoint();
        const auto pi = power.getPhaseInverterDC();
        const auto& el84 = power.getPowerAmp();

        logMessage ("B+1..5: " + juce::String (s.b1, 1) + " " + juce::String (s.b2, 1) + " " + juce::String (s.b3, 1)
                    + " " + juce::String (s.b4, 1) + " " + juce::String (s.b5, 1));
        logMessage ("V1 TB plate " + juce::String (op.v1PlateTB, 1) + ", Normal plate " + juce::String (op.v1PlateN, 1)
                    + ", cathode " + juce::String (op.v1Cathode, 3));
        logMessage ("V2a plate " + juce::String (op.v2aPlate, 1) + ", cathode " + juce::String (op.v2aCathode, 3)
                    + ", CF cathode " + juce::String (op.cfCathode, 1));
        logMessage ("PI plates " + juce::String (pi.plateA, 1) + " / " + juce::String (pi.plateB, 1)
                    + ", cathode " + juce::String (pi.cathode, 2));
        logMessage ("EL84 cathode " + juce::String (el84.getCathodeVoltageDC(), 2) + " V, plate "
                    + juce::String (el84.getIdlePlateCurrent() * 1000.0, 1) + " mA, screen "
                    + juce::String (el84.getIdleScreenCurrent() * 1000.0, 2) + " mA per tube");

        expect (s.b1 > 320.0 && s.b1 < 370.0);
        expect (s.b1 > s.b2 && s.b2 > s.b3 && s.b2 > s.b4 && s.b4 > s.b5);
        expectWithinAbsoluteError (el84.getCathodeVoltageDC(), 10.0, 1.5);     // ~10 V on R119 [AIK]
        const auto perTube = el84.getIdlePlateCurrent() + el84.getIdleScreenCurrent();
        expectWithinAbsoluteError (perTube, 0.050, 0.008);                         // ~50 mA per tube
        expect (op.v1Cathode > 0.8 && op.v1Cathode < 2.5);
        expect (op.v1PlateTB > 80.0 && op.v1PlateTB < s.b5);
        // the follower draws ~3 mA through R21 at a low plate-cathode voltage: it sits at its grid voltage
        expectWithinAbsoluteError (op.cfCathode, op.v2aPlate, 3.0);
        expect (pi.plateA > 150.0 && pi.plateA < s.b3);
    }
};

static Ac30OperatingPointTests ac30OperatingPointTests;

//==============================================================================
class Ac30SignalTests final : public juce::UnitTest
{
public:
    Ac30SignalTests() : juce::UnitTest ("AC30C2 signal path", "AC30") {}

    void runTest() override
    {
        beginTest ("Silence in -> (almost) silence out, no NaN, at every oversampling factor");
        for (int os = 0; os < 3; ++os)
        {
            Ac30Engine engine;
            auto settings = cleanSettings (os);
            engine.setSettings (settings);
            engine.prepare (kFs, kBlock, os);

            std::vector<float> x ((size_t) kFs, 0.0f);
            run (engine, x);
            const auto r = rms (x, x.size() / 2, x.size());
            logMessage (juce::String (2 << os) + "x idle rms " + juce::String (r, 8));
            expect (r < 1.0e-3);
        }

        beginTest ("Top Boost: a 100 mV, 1 kHz tone comes out, louder with Volume");
        {
            double previous = 0.0;
            for (float vol : { 2.0f, 5.0f, 8.0f })
            {
                Ac30Engine engine;
                auto settings = cleanSettings();
                settings.preamp.topBoostVolume = vol;
                engine.setSettings (settings);
                engine.prepare (kFs, kBlock, 1);

                auto x = sine (1000.0, 0.1f, 1.0);
                run (engine, x);
                const auto a = toneAmplitude (x, 24000, 24000, 1000.0, kFs);
                logMessage ("TB volume " + juce::String (vol) + ": 1 kHz amplitude " + juce::String (a, 4));
                expect (std::isfinite (a) && a > previous);
                previous = a;
            }
        }
    }
};

static Ac30SignalTests ac30SignalTests;

//==============================================================================
#include "Ac30SpiceReference.h"

namespace
{
struct Harmonics { double fundamental, thdPercent; };

/** Fundamental and THD (harmonics 2..10, as ngspice's fourier) of the last 20 periods. */
Harmonics analyse (const std::vector<double>& y, double freq, double fs)
{
    const auto periodSamples = fs / freq;
    const auto count = (size_t) std::lround (20.0 * periodSamples);
    const auto from = y.size() - count;

    double h[11] {};
    for (int k = 1; k <= 10; ++k)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < count; ++i)
        {
            const auto ph = 2.0 * juce::MathConstants<double>::pi * freq * k * (double) i / fs;
            re += y[from + i] * std::cos (ph);
            im += y[from + i] * std::sin (ph);
        }
        h[k] = 2.0 * std::sqrt (re * re + im * im) / (double) count;
    }

    double sum = 0.0;
    for (int k = 2; k <= 10; ++k)
        sum += h[k] * h[k];
    return { h[1], 100.0 * std::sqrt (sum) / h[1] };
}

double db (double a, double b) { return 20.0 * std::log10 (a / b); }
} // namespace

class Ac30SpiceTests final : public juce::UnitTest
{
public:
    Ac30SpiceTests() : juce::UnitTest ("AC30C2 vs ngspice", "AC30") {}

    void runTest() override
    {
        constexpr double fs = 192000.0; // 4x oversampling at 48 kHz

        beginTest ("Preamp (V1, Normal / Top Boost, V2 + stack, U1B) matches ngspice");
        {
            double worstDb = 0.0;
            for (const auto& c : kAc30PreampCases)
            {
                const auto& cfg = kAc30PreampConfigs[c.config];
                PreampAC30 preamp;
                PreampControls controls;
                controls.input = cfg.normalInput ? InputJack::normalHi : InputJack::topBoostHi;
                controls.normalVolume = cfg.normal;
                controls.topBoostVolume = cfg.topBoost;
                controls.treble = cfg.treble;
                controls.bass = cfg.bass;
                preamp.setControls (controls);
                preamp.setSupplies (kAc30RefB4, kAc30RefB5);
                preamp.prepare ({ fs, 512, 1 });

                const auto total = (size_t) ((0.4 + 20.0 / c.frequency) * fs);
                std::vector<double> y (total);
                for (size_t i = 0; i < total; ++i)
                    y[i] = preamp.processSample (c.amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                                * c.frequency * (double) i / fs));

                const auto r = analyse (y, c.frequency, fs);
                const auto err = db (r.fundamental, c.fundamental);
                worstDb = std::max (worstDb, std::abs (err));
                logMessage (juce::String (cfg.name) + " " + juce::String (c.amplitude) + " V " + juce::String (c.frequency) + " Hz: H1 "
                            + juce::String (r.fundamental, 4) + " (spice " + juce::String (c.fundamental, 4) + ", "
                            + juce::String (err, 2) + " dB), THD " + juce::String (r.thdPercent, 2) + " % (spice "
                            + juce::String (c.thdPercent, 2) + " %)");

                expect (std::abs (err) < 1.0, "H1 off by " + juce::String (err, 2) + " dB");
                if (c.thdPercent > 1.0)
                    expect (std::abs (r.thdPercent - c.thdPercent) < 0.35 * c.thdPercent + 1.0,
                            "THD " + juce::String (r.thdPercent, 2) + " % vs " + juce::String (c.thdPercent, 2) + " %");
            }
            logMessage ("worst H1 error " + juce::String (worstDb, 2) + " dB");
        }

        beginTest ("Phase inverter, Tone Cut and Master match ngspice");
        {
            Ac30Engine engine;
            engine.setSettings (cleanSettings());
            engine.prepare (48000.0, 512, 1);
            expectWithinAbsoluteError (engine.getAmpChain (1).get<Ac30Engine::powerSection>().getSupplies().b3, kAc30RefB3, 0.2);

            for (const auto& c : kAc30PiCases)
            {
                const auto& cfg = kAc30PiConfigs[c.config];
                auto section = engine.getAmpChain (1).get<Ac30Engine::powerSection>();
                PowerControls controls;
                controls.toneCut = cfg.toneCut;
                controls.master = cfg.master;
                section.setControls (controls);
                section.reset();

                const auto total = (size_t) ((0.4 + 20.0 / c.frequency) * fs);
                std::vector<double> y (total);
                for (size_t i = 0; i < total; ++i)
                {
                    section.processSample (c.amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                          * c.frequency * (double) i / fs));
                    y[i] = section.getGridDrive();
                }

                const auto r = analyse (y, c.frequency, fs);
                const auto err = db (r.fundamental, c.fundamental);
                logMessage (juce::String (cfg.name) + " " + juce::String (c.frequency) + " Hz: grid drive "
                            + juce::String (r.fundamental, 4) + " (spice " + juce::String (c.fundamental, 4) + ", "
                            + juce::String (err, 2) + " dB)");
                expect (std::abs (err) < 0.5, "grid drive off by " + juce::String (err, 2) + " dB");
            }
        }
    }
};

static Ac30SpiceTests ac30SpiceTests;

//==============================================================================
namespace
{
/** RMS envelope in 5 ms frames. */
std::vector<double> envelope (const std::vector<float>& x, size_t from, double fs)
{
    const auto frame = (size_t) (0.005 * fs);
    std::vector<double> e;
    for (auto i = from; i + frame <= x.size(); i += frame)
        e.push_back (rms (x, i, i + frame));
    return e;
}

/** Modulation depth (dB) and rate (Hz: peak of the envelope spectrum, 0.5..12 Hz), envelope every 5 ms. */
std::pair<double, double> modulation (const std::vector<double>& e)
{
    double mean = 0.0, lo = 1.0e9, hi = 0.0;
    for (auto v : e) { mean += v; lo = std::min (lo, v); hi = std::max (hi, v); }
    mean /= (double) e.size();

    double bestRate = 0.0, bestMagnitude = 0.0;
    for (double f = 0.5; f <= 12.0; f += 0.02)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < e.size(); ++i)
        {
            const auto ph = 2.0 * juce::MathConstants<double>::pi * f * 0.005 * (double) i;
            re += (e[i] - mean) * std::cos (ph);
            im += (e[i] - mean) * std::sin (ph);
        }
        const auto magnitude = re * re + im * im;
        if (magnitude > bestMagnitude)
        {
            bestMagnitude = magnitude;
            bestRate = f;
        }
    }

    return { 20.0 * std::log10 (hi / std::max (lo, 1.0e-9)), bestRate };
}
} // namespace

class Ac30BehaviourTests final : public juce::UnitTest
{
public:
    Ac30BehaviourTests() : juce::UnitTest ("AC30C2 controls and dynamics", "AC30") {}

    void runTest() override
    {
        beginTest ("Treble and Bass act where they should (Top Boost, small signal)");
        {
            const auto level = [] (float treble, float bass, double freq)
            {
                PreampAC30 preamp;
                PreampControls c;
                c.treble = treble;
                c.bass = bass;
                c.topBoostVolume = 3.0f;
                preamp.setControls (c);
                preamp.prepare ({ 192000.0, 512, 1 });
                std::vector<float> y ((size_t) (0.3 * 192000.0));
                for (size_t i = 0; i < y.size(); ++i)
                    y[i] = preamp.processSample (0.003f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / 192000.0));
                return toneAmplitude (y, y.size() / 2, (size_t) std::lround (192000.0 / freq) * (size_t) std::max (1.0, freq / 50.0), freq, 192000.0);
            };

            const auto trebleSwing = 20.0 * std::log10 (level (9.0f, 5.0f, 4000.0) / level (1.0f, 5.0f, 4000.0));
            const auto bassSwing = 20.0 * std::log10 (level (6.0f, 9.0f, 100.0) / level (6.0f, 1.0f, 100.0));
            logMessage ("treble 1 -> 9 at 4 kHz: " + juce::String (trebleSwing, 1) + " dB, bass 1 -> 9 at 100 Hz: "
                        + juce::String (bassSwing, 1) + " dB");
            expect (trebleSwing > 6.0);
            expect (bassSwing > 6.0);
        }

        beginTest ("Tremolo: bias modulation at 2..8 Hz, faster with Speed, off with the footswitch");
        {
            const auto measure = [this] (float speed, bool on, float depth = 10.0f)
            {
                Ac30Engine engine;
                auto s = cleanSettings();
                s.power.tremDepth = depth;
                s.power.tremSpeed = speed;
                s.power.tremOn = on;
                engine.setSettings (s);
                engine.prepare (kFs, kBlock, 0);
                auto x = sine (1000.0, 0.02f, 6.0);
                run (engine, x);
                const auto m = modulation (envelope (x, (size_t) (3.0 * kFs), kFs));
                logMessage ("speed " + juce::String (speed) + ", depth knob " + juce::String (depth) + (on ? " on" : " off") + ": modulation "
                            + juce::String (m.first, 1) + " dB, rate " + juce::String (m.second, 2) + " Hz");
                return m;
            };

            const auto slow = measure (0.0f, true), fast = measure (10.0f, true), off = measure (10.0f, false);
            const auto gentle = measure (5.0f, true, 2.0f);
            expect (gentle.first > 1.0 && gentle.first < slow.first);
            // the oscillator itself runs at 2.07 / 8.39 Hz (see the profile test; ngspice: 2.0 / 7.6 Hz)
            expect (slow.first > 3.0 && fast.first > 3.0);
            expectWithinAbsoluteError (slow.second, 2.07, 0.3);
            expectWithinAbsoluteError (fast.second, 8.39, 0.6);
            expect (off.first < 1.0);
        }

        beginTest ("Reverb: Level adds a tail after the note stops");
        {
            const auto tail = [] (float levelKnob)
            {
                Ac30Engine engine;
                auto s = cleanSettings();
                s.reverb.level = levelKnob;
                engine.setSettings (s);
                engine.prepare (kFs, kBlock, 0);
                auto x = sine (440.0, 0.05f, 2.0);
                std::fill (x.begin() + (long) kFs, x.end(), 0.0f);
                run (engine, x);
                return rms (x, (size_t) (1.2 * kFs), (size_t) (1.6 * kFs));
            };
            const auto dry = tail (0.0f), wet = tail (8.0f);
            logMessage ("tail 0.2-0.6 s after the note: dry " + juce::String (dry, 6) + ", level 8 " + juce::String (wet, 6));
            expect (wet > 30.0 * dry && wet > 1.0e-3);
        }

        beginTest ("EL84 bias shifts up under heavy drive (shared cathode, C74)");
        {
            Ac30Engine engine;
            auto s = cleanSettings();
            s.preamp.topBoostVolume = 9.0f;
            s.power.master = 10.0f;
            engine.setSettings (s);
            engine.prepare (kFs, kBlock, 0);
            auto x = sine (220.0, 0.3f, 1.0);
            run (engine, x);
            const auto& power = engine.getAmpChain (0).get<Ac30Engine::powerSection>().getPowerAmp();
            const auto shift = power.getCathodeVoltage() - power.getCathodeVoltageDC();
            logMessage ("cathode " + juce::String (power.getCathodeVoltageDC(), 2) + " V idle -> "
                        + juce::String (power.getCathodeVoltage(), 2) + " V driven; output peak "
                        + juce::String (juce::FloatVectorOperations::findMaximum (x.data(), (int) x.size()), 3));
            expect (shift > 0.5 && shift < 5.0);
        }
    }
};

static Ac30BehaviourTests ac30BehaviourTests;

//==============================================================================
class Ac30RealtimeTests final : public juce::UnitTest
{
public:
    Ac30RealtimeTests() : juce::UnitTest ("AC30C2 real-time safety", "AC30") {}

    void runTest() override
    {
        beginTest ("No heap allocation while processing, moving every knob and switching oversampling");
        {
            Ac30Engine engine;
            auto s = cleanSettings();
            s.reverb.level = 5.0f;
            s.power.tremDepth = 6.0f;
            engine.setSettings (s);
            engine.prepare (kFs, kBlock, 1);
            auto x = sine (330.0, 0.2f, 0.1);

            dumble::test::AllocationGuard::resetCounts();
            {
                dumble::test::AllocationGuard guard;
                for (int block = 0; block < 300; ++block)
                {
                    const auto t = (float) (block % 100) / 10.0f;
                    s.preamp.topBoostVolume = t;
                    s.preamp.treble = 10.0f - t;
                    s.preamp.bass = t;
                    s.preamp.normalVolume = t * 0.5f;
                    s.preamp.input = (InputJack) (block / 60 % 5);
                    s.reverb.level = t;
                    s.reverb.tone = 10.0f - t;
                    s.reverb.on = block % 50 < 40;
                    s.power.toneCut = t;
                    s.power.master = 10.0f - t;
                    s.power.tremSpeed = t;
                    s.power.tremDepth = 10.0f - t;
                    s.power.tremOn = block % 70 < 50;
                    s.oversamplingIndex = block / 100;
                    engine.setSettings (s);
                    auto pos = (size_t) (block * kBlock) % (x.size() - kBlock);
                    std::vector<float>::iterator unused;
                    juce::ignoreUnused (unused);
                    engine.process (x.data() + pos, kBlock);
                }
            }

            expectEquals ((int) dumble::test::AllocationGuard::getAllocationCount(), 0);
            expectEquals ((int) dumble::test::AllocationGuard::getDeallocationCount(), 0);
        }

        beginTest ("CPU load (informational)");
        for (int os = 0; os < 3; ++os)
        {
            Ac30Engine engine;
            auto s = cleanSettings (os);
            s.reverb.level = 4.0f;
            s.power.tremDepth = 5.0f;
            s.cabOn = true;
            engine.setSettings (s);
            engine.prepare (kFs, kBlock, os);
            auto x = sine (196.0, 0.2f, 5.0);

            const auto start = juce::Time::getMillisecondCounterHiRes();
            run (engine, x);
            const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
            logMessage (juce::String (2 << os) + "x: " + juce::String (100.0 * elapsed / 5.0, 1) + " % of one core");
            expect (elapsed < 5.0 * (os == 2 ? 2.0 : 1.0)); // must at least run in real time on a VM core (8x: informational)
        }
    }
};

static Ac30RealtimeTests ac30RealtimeTests;

//==============================================================================
class Ac30ProfileTests final : public juce::UnitTest
{
public:
    Ac30ProfileTests() : juce::UnitTest ("AC30C2 profile", "AC30") {}

    void runTest() override
    {
        beginTest ("Per-stage cost at 192 kHz (informational) and tremolo oscillator");
        constexpr double fs = 192000.0;
        constexpr int n = 192000;
        std::vector<float> x (n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 196.0 * i / fs);

        const auto time = [] (auto&& fn)
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            fn();
            return (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        };

        PreampAC30 preamp;
        preamp.prepare ({ fs, 512, 1 });
        std::vector<float> y (n);
        const auto tPre = time ([&] { for (int i = 0; i < n; ++i) y[(size_t) i] = preamp.processSample (x[(size_t) i]); });

        ReverbFx reverb;
        ReverbControls rc;
        rc.level = 5.0f;
        reverb.setControls (rc);
        reverb.prepare ({ fs, 512, 1 });
        const auto tRev = time ([&] { for (int i = 0; i < n; ++i) y[(size_t) i] = reverb.processSample (y[(size_t) i]); });

        PowerSectionAC30 power;
        power.prepare ({ fs, 512, 1 });
        const auto tPow = time ([&] { for (int i = 0; i < n; ++i) y[(size_t) i] = power.processSample (y[(size_t) i]); });

        logMessage ("1 s of audio at 192 kHz: preamp " + juce::String (tPre * 100.0, 1) + " %, reverb "
                    + juce::String (tRev * 100.0, 1) + " %, power section " + juce::String (tPow * 100.0, 1) + " % of one core");
        logMessage ("mean Newton iterations: preamp A " + juce::String (preamp.getMeanIterations().first, 2) + ", B "
                    + juce::String (preamp.getMeanIterations().second, 2) + ", phase inverter " + juce::String (power.getMeanIterations(), 2));

        // tremolo oscillator alone: frequency and amplitude at the depth pot vs Speed
        for (float speed : { 0.0f, 5.0f, 10.0f })
        {
            TremoloOsc osc;
            osc.setSpeed (speed);
            osc.setSupply (power.getSupplies().b2);
            osc.prepare (48000.0);
            std::vector<double> o ((size_t) (8 * 48000));
            for (auto& v : o)
                v = osc.processSample();

            double lo = 1.0e9, hi = -1.0e9;
            int rises = 0;
            size_t first = 0, last = 0;
            for (size_t i = (size_t) (4 * 48000); i < o.size(); ++i)
            {
                lo = std::min (lo, o[i]);
                hi = std::max (hi, o[i]);
                if (o[i - 1] < 0.0 && o[i] >= 0.0)
                {
                    if (rises == 0) first = i;
                    last = i;
                    ++rises;
                }
            }
            const auto freq = rises > 1 ? (double) (rises - 1) * 48000.0 / (double) (last - first) : 0.0;
            logMessage ("tremolo speed " + juce::String (speed) + ": " + juce::String (freq, 2) + " Hz, "
                        + juce::String (lo, 1) + " .. " + juce::String (hi, 1) + " V at the depth pot");
        }
    }
};

static Ac30ProfileTests ac30ProfileTests;
