#pragma once

// Component values for the Dumble Steel String Singer model.
//
// The SSS is a Fender (blackface Showman/Twin) derived, high-headroom design:
// 12AX7 preamp, passive TMB tone stack, 12AT7 long-tail-pair phase inverter and
// a 4x6550 fixed-bias push-pull power amp with a Presence/Deep negative feedback loop.
//
// Values below follow the publicly circulated SSS/blackface schematics. Anything
// marked "VERIFY" is an educated value that should be checked against the reference
// schematic (and the SPICE reference netlist) before final voicing.

namespace dumble::circuit
{
    //==========================================================================
    // Signal level convention: 1.0f digital full scale == kVoltsPerFullScale volts at V1 grid.
    inline constexpr float kVoltsPerFullScale = 1.0f;

    //==========================================================================
    // Preamp supply
    inline constexpr float kPreampBplus = 300.0f;          // V, preamp B+ node (VERIFY)

    struct TriodeStageValues
    {
        float rPlate;        // plate load resistor [ohm]
        float rCathode;      // cathode resistor [ohm]
        float cCathode;      // cathode bypass cap [F]
        float rGridStopper;  // series grid resistor [ohm]
        float cMiller;       // effective grid input capacitance (Miller) [F]
        float cCoupling;     // output coupling cap [F]
        float rLoad;         // load seen through the coupling cap (next grid leak / pot) [ohm]
        float bPlus;         // supply node feeding rPlate [V]
        float cPlate;        // plate node shunt capacitance (Cpk + wiring + next stage Miller) [F]
        float inputDivider;  // fixed resistive divider in front of the grid (mixing network), 1 = none
    };

    // V1a — input stage
    inline constexpr TriodeStageValues kV1a { 100.0e3f, 1.5e3f, 25.0e-6f, 68.0e3f, 120.0e-12f, 22.0e-9f, 1.0e6f, kPreampBplus, 150.0e-12f, 1.0f };
    // V1b — make-up stage after Volume (partially bypassed cathode, VERIFY)
    inline constexpr TriodeStageValues kV1b { 100.0e3f, 1.5e3f, 22.0e-6f, 34.0e3f, 120.0e-12f, 22.0e-9f, 1.0e6f, kPreampBplus, 100.0e-12f, 1.0f };
    // V2a — recovery stage after the tone stack, fed through the dry/reverb mixing divider
    // (470k into 100k-ish -> ~0.15, VERIFY). Without it the amp would be a high-gain design,
    // which the SSS is not: it is voiced for headroom.
    inline constexpr TriodeStageValues kV2a { 100.0e3f, 1.5e3f, 25.0e-6f, 34.0e3f, 120.0e-12f, 22.0e-9f, 1.0e6f, kPreampBplus, 100.0e-12f, 0.15f };

    //==========================================================================
    // Volume pot + bright cap
    inline constexpr float kVolumePot  = 1.0e6f;    // 1M audio taper
    inline constexpr float kBrightCap  = 120.0e-12f; // across the top of the pot when Bright = on

    //==========================================================================
    // Fender-style TMB tone stack (Yeh & Smith naming)
    //   R1 = treble pot, R2 = bass pot, R3 = mid pot, R4 = slope resistor
    //   C1 = treble cap,  C2 = bass cap, C3 = mid cap
    inline constexpr float kTsR1 = 250.0e3f;
    inline constexpr float kTsR2 = 250.0e3f;
    inline constexpr float kTsR3 = 10.0e3f;
    inline constexpr float kTsR4 = 100.0e3f;
    inline constexpr float kTsC1 = 250.0e-12f;
    inline constexpr float kTsC2 = 0.1e-6f;
    inline constexpr float kTsC3 = 0.047e-6f;

    //==========================================================================
    // Long-tail-pair phase inverter (12AT7)
    inline constexpr float kPiBplus      = 420.0f;   // V (VERIFY)
    inline constexpr float kPiRPlateA    = 82.0e3f;  // classic Fender imbalance 82k / 100k
    inline constexpr float kPiRPlateB    = 100.0e3f;
    inline constexpr float kPiRBias      = 470.0f;   // shared cathode bias resistor
    inline constexpr float kPiRTail      = 10.0e3f;  // tail resistor to ground
    inline constexpr float kPiCCoupling  = 0.1e-6f;  // to power tube grids
    inline constexpr float kPiRGridLeak  = 220.0e3f; // power tube grid leak (bias feed)
    inline constexpr float kPiSourceR    = 40.0e3f;  // PI plate output impedance seen by the power grids
    inline constexpr float kMixerGain    = 0.25f;    // V2a -> PI mixing network attenuation (VERIFY)

    //==========================================================================
    // Power amp: 4x6550 push-pull (2 per side), fixed bias
    inline constexpr int   kPowerTubesPerSide  = 2;
    inline constexpr float kPowerBplus         = 460.0f;  // V, plate supply (VERIFY)
    inline constexpr float kScreenDrop         = 10.0f;   // V, screen supply below plate supply
    inline constexpr float kIdleCurrentPerTube = 0.040f;  // A, bias target per tube
    inline constexpr float kSagResistance      = 60.0f;   // ohm, effective PSU source impedance
    inline constexpr float kSagTimeConstant    = 0.060f;  // s, reservoir RC time constant
    inline constexpr float kPrimaryLoadPerSide = 425.0f;  // ohm, Raa/4 (Raa ~1.7k for 4x6550, VERIFY)
    inline constexpr float kOutputRefCurrent   = 1.0f;    // A, primary current difference mapped to 1.0
    inline constexpr float kOtLowCutHz         = 25.0f;   // primary inductance corner
    inline constexpr float kOtHighCutHz        = 18000.0f; // leakage inductance / winding capacitance
    inline constexpr float kOtCoreSaturation   = 0.15f;   // mild LF core saturation amount

    //==========================================================================
    // Global negative feedback (speaker winding -> PI)
    inline constexpr float kSpeakerPeakVolts = 40.0f;  // speaker volts at normalised output 1.0
    inline constexpr float kNfbRatio         = 0.08f;  // divider into PI feedback grid (VERIFY)
    inline constexpr float kPresenceHz       = 3500.0f;
    inline constexpr float kDeepHz           = 120.0f;
} // namespace dumble::circuit
