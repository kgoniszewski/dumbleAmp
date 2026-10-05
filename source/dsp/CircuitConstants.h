#pragma once

// Component values of the Dumble Steel String Singer, serial #002, taken from the community
// reconstruction "SSS No 002.asc" (LTspice; Ryan Colgan / The Amp Garage, TheToneGeek):
//   github.com/colganr/LTSpiceCircuits/tree/master/Dumble Steel String Singer sn002
// Designators (R1, C2, U37 ...) refer to that schematic. Supply voltages are its .op results.
//
// Known slips in the simulation file, corrected here from the builder's BOM / intent:
//   R25, R26, R66 are entered as "1m" (1 milliohm) -> 1 MOhm;  R39 "2.2m" -> 2.2 MOhm;
//   the Master pot (U41) is drawn without a grounded end -> wired as a normal volume pot.
// Output transformer: BOM lists ClassicTone 40-18102 / Hammond 1760W, both 2 kOhm CT : 8 Ohm.

namespace dumble::circuit
{
    //==========================================================================
    // Signal level convention: 1.0f digital full scale == kVoltsPerFullScale volts at V1 grid.
    inline constexpr float kVoltsPerFullScale = 1.0f;

    //==========================================================================
    // Supply rails (.op of the reconstruction)
    inline constexpr float kHT1 = 446.80f;  // OT centre tap (plates)
    inline constexpr float kHT2 = 444.84f;  // screens, driver CF plates (after choke L1)
    inline constexpr float kHT3 = 415.22f;  // phase inverter
    inline constexpr float kHT4 = 348.79f;  // U20, U28, U37..U40
    inline constexpr float kHTA = 343.94f;  // V1, V4
    inline constexpr float kBias = -320.0f; // bias supply (V4 source)

    struct TriodeStageValues
    {
        float rPlate = 100.0e3f;           // plate load [ohm]
        float rCathode = 1.5e3f;           // bypassed cathode resistor [ohm]
        float cCathode = 5.0e-6f;          // its bypass cap [F]
        float rCathodeUnbypassed = 0.0f;   // unbypassed resistor below it (local NFB injection point) [ohm]
        float rGridStopper = 0.0f;         // series grid resistor [ohm]
        float cMiller = 0.0f;              // effective grid input capacitance, seen through rGridStopper [F]
                                           // (0 = modelled in the driving LinearNetwork instead)
        float cCoupling = 0.0f;            // internal output coupling cap (0 = plate drives an external network)
        float rLoad = 0.0f;                // load behind the coupling cap [ohm]
        float bPlus = kHTA;                // supply feeding rPlate [V]
        float cPlate = 10.0e-12f;          // stray plate-node capacitance [F]
    };

    // V1 (5751): R2 100k, R1 1.5k || C2 5u, R62 100R (LNFB), R22 || R24 = 34k stopper. Plate drives the tone stack.
    // Miller capacitance: Cgk + Cgp * (1 + |A|), Koren CCG 2.3p / CGP 2.4p.
    inline constexpr float kMiller5751 = 115.0e-12f; // |A| ~ 46
    inline constexpr float kMiller7025 = 153.0e-12f; // |A| ~ 62

    inline constexpr TriodeStageValues kV1  { 100.0e3f, 1.5e3f, 5.0e-6f, 100.0f, 34.0e3f, kMiller5751, 0.0f, 0.0f, kHTA };
    // V4 (5751): R5 100k, R4 1.5k || C5 5u, R68 100R (LNFB2), R69 68k. Plate drives the High/Low filter network.
    // R69 and the Miller capacitance are part of the front network; the stopper here only shapes grid conduction.
    inline constexpr TriodeStageValues kV4  { 100.0e3f, 1.5e3f, 5.0e-6f, 100.0f, 68.0e3f, 0.0f, 0.0f, 0.0f, kHTA };
    // U37 (7025): R80 100k, R81 1k || C46 1u. Grid fed by the filter network (~70k Thevenin, Miller cap in that
    // network). DC-coupled into U38.
    inline constexpr TriodeStageValues kU37 { 100.0e3f, 1.0e3f, 1.0e-6f, 0.0f, 70.0e3f, 0.0f, 0.0f, 0.0f, kHT4 };
    // U39 (7025, reverb return gain): R86 100k, R87 1k || C48 1u. Grid on the Reverb Return pot wiper (<= 25k).
    inline constexpr TriodeStageValues kU39 { 100.0e3f, 1.0e3f, 1.0e-6f, 0.0f, 25.0e3f, kMiller7025, 0.0f, 0.0f, kHT4 };
    // U20 (7025, reverb send amp): R38 100k, R36 1.5k || C19 5u, R37 100R (fed back from the tank via R41 2.2k),
    // R35 68k stopper; plate -> C20 1n -> Reverb Send pot 250k (audio).
    inline constexpr TriodeStageValues kU20 { 100.0e3f, 1.5e3f, 5.0e-6f, 100.0f, 68.0e3f, kMiller7025, 1.0e-9f, 250.0e3f, kHT4 };
    // U28 (7025, reverb recovery): R45 100k, R42 1.5k || C22 5u, R48 100R (fed from U40 via R52 270k / C23 .1u);
    // plate -> C27 3000p -> Reverb Return pot 100k (linear).
    inline constexpr TriodeStageValues kU28 { 100.0e3f, 1.5e3f, 5.0e-6f, 100.0f, 3.0e3f, kMiller7025, 3.0e-9f, 100.0e3f, kHT4 };

    //==========================================================================
    // Local feedback paths
    inline constexpr float kLnfbR  = 100.0e3f;  // R54: V4 plate -> C15 0.1u -> V1 cathode (R62)
    inline constexpr float kLnfbC  = 0.1e-6f;   // C15
    inline constexpr float kLnfb2R = 470.0e3f;  // R82: U38 cathode -> C47 .22u -> V4 cathode (R68)
    inline constexpr float kLnfb2C = 0.22e-6f;  // C47
    inline constexpr float kRevFbR = 270.0e3f;  // R52: U40 cathode -> C23 .1u -> U28 cathode (R48)
    inline constexpr float kRevFbC = 0.1e-6f;   // C23
    inline constexpr float kTankFbR = 2.2e3f;   // R41: tank transformer secondary -> U20 cathode (R37)

    //==========================================================================
    // Tone stack (plate-driven, "Guitar" position of the Guitar/Mic switch U34)
    inline constexpr float kTsTrebleCap = 360.0e-12f; // C1
    inline constexpr float kTsSlope     = 100.0e3f;   // R3
    inline constexpr float kTsBassCap   = 0.1e-6f;    // C3
    inline constexpr float kTsMidCap    = 0.047e-6f;  // C34
    inline constexpr float kTsTreblePot = 250.0e3f;   // U2, pot_pow (10 % at half rotation)
    inline constexpr float kTsBassPot   = 250.0e3f;   // U3, pot_pow
    inline constexpr float kTsMidPot    = 100.0e3f;   // U33, linear, wiper to ground
    inline constexpr float kTsBassFoot  = 1.8e3f;     // R7
    inline constexpr float kTsTrebleFoot = 4.7e-9f;   // C35 (treble pot bottom to ground in "Guitar")
    inline constexpr float kTsBassMix   = 100.0e3f;   // R63 (bass wiper -> treble wiper in "Guitar")
    inline constexpr float kVolumePot   = 1.0e6f;     // U5, pot_pow
    inline constexpr float kBrightCap   = 250.0e-12f; // C4 via U36
    inline constexpr float kV4Stopper   = 68.0e3f;    // R69
    // Deep (U35): R64 270k from the bass node and R65 270k from the volume wiper into C36 .01u || R66 1M, R67 10k
    inline constexpr float kDeepR1 = 270.0e3f, kDeepR2 = 270.0e3f, kDeepC = 0.01e-6f, kDeepRpar = 1.0e6f, kDeepRfoot = 10.0e3f;

    //==========================================================================
    // V4 -> U37 filter network (High / Low 7-position switches)
    inline constexpr float kMidCoupling = 0.01e-6f;   // C6
    inline constexpr float kMidTrebleCap = 0.001e-6f; // C37
    inline constexpr float kMidR53 = 100.0e3f;        // R53
    inline constexpr float kMidChoke = 0.3f;          // L2 300 mH
    inline constexpr float kMidChokeR = 59.0f;        // L2 series resistance
    inline constexpr float kMidR34 = 470.0e3f;        // R34
    inline constexpr float kMidR70 = 820.0e3f;        // R70
    inline constexpr float kMidC44 = 0.003e-6f;       // C44
    inline constexpr float kMidR71 = 100.0e3f;        // R71 (U37 grid leak)
    inline constexpr float kMidR79 = 270.0e3f;        // R79 (Low switch -> U37 grid)
    inline constexpr float kMidC45 = 0.01e-6f;        // C45
    // High switch: position 1 = short, 2..7 = C39 150p, C40 330p, C41 1n, C42 2.4n, C43 5.1n, C38 10n
    inline constexpr float kHighCaps[7] = { 0.0f, 150.0e-12f, 330.0e-12f, 1.0e-9f, 2.4e-9f, 5.1e-9f, 10.0e-9f };
    // Low ladder R72..R78 (39k, 68k, 100k, 180k, 270k, 390k, 12k); position n taps after the (n-1)-th resistor
    inline constexpr float kLowLadder[7] = { 39.0e3f, 68.0e3f, 100.0e3f, 180.0e3f, 270.0e3f, 390.0e3f, 12.0e3f };

    //==========================================================================
    // Cathode followers
    inline constexpr float kCfLoad = 100.0e3f;      // R83 (U38), R88 (U40)
    inline constexpr float kMixR = 220.0e3f;        // R84, R85 (dry / reverb mix into N045)
    inline constexpr float kMixCoupling = 0.01e-6f; // C49
    inline constexpr float kMasterPot = 1.0e6f;     // U41, pot_pow
    inline constexpr float kAccentCap = 0.001e-6f;  // C50 via U42
    inline constexpr float kPiInputCap = 0.02e-6f;  // C9
    inline constexpr float kPiGridLeak = 1.0e6f;    // R10 / R11

    //==========================================================================
    // Long-tail-pair phase inverter (U6/U7, 7025)
    inline constexpr float kPiBplus      = kHT3;
    inline constexpr float kPiBalance    = 0.65f;          // U11 25k balance pot wiper (as in the reconstruction)
    inline constexpr float kPiRPlateA    = 100.0e3f + 25.0e3f * (1.0f - kPiBalance); // R14 + pot section
    inline constexpr float kPiRPlateB    = 100.0e3f + 25.0e3f * kPiBalance;          // R13 + pot section
    inline constexpr float kPiRBias      = 820.0f;          // R12
    inline constexpr float kPiRTail      = 18.0e3f + 270.0f; // R9 + R8
    inline constexpr float kPiPlateCap   = 68.0e-12f;       // C14 between the plates
    inline constexpr float kPiSourceR    = 40.0e3f;         // PI plate output impedance (driver grid conduction)

    //==========================================================================
    // Driver: U12/U13 (7025) cathode followers, DC-coupled to the power grids
    inline constexpr float kDrvCoupling  = 0.1e-6f;   // C10 / C11
    inline constexpr float kDrvGridLeak  = 1.0e6f;    // R25 / R26 (see note above)
    inline constexpr float kDrvBiasTop   = 820.0e3f;  // R28 (to the bias supply)
    inline constexpr float kDrvBiasBottom = 130.0e3f; // R27 (to ground)
    inline constexpr float kDrvLoad      = 220.0e3f;  // R15 / R16 to the bias supply
    inline constexpr float kDrvShunt     = 235.0e3f;  // R31 || R32 (and R47 || R51) to ground
    inline constexpr float kPowerStopper = 1.5e3f;    // R30, R56, R46, R57

    //==========================================================================
    // Power amp: 4x 6L6GC push-pull (2 per side), fixed bias from the driver CFs
    inline constexpr int   kPowerTubesPerSide  = 2;
    inline constexpr float kScreenR            = 470.0f;   // R29, R18, R33, R19
    inline constexpr float kSupplyR            = 10.0f;    // V1 Rser: stiff solid-state supply
    inline constexpr float kSagTimeConstant    = 0.050f;   // reservoir time constant [s]
    inline constexpr float kPrimaryLoadPerSide = 500.0f;   // Raa/4, Raa = 2 kOhm (ClassicTone 40-18102 / Hammond 1760W)
    inline constexpr float kSpeakerLoad        = 8.0f;     // R17
    inline constexpr float kSpeakerFullScale   = 50.0f;    // speaker volts mapped to digital 1.0 (~150 W into 8 Ohm)
    inline constexpr float kOtLowCutHz         = 10.0f;
    inline constexpr float kOtHighCutHz        = 18000.0f;
    inline constexpr float kOtCoreSaturation   = 0.05f;

    //==========================================================================
    // Global negative feedback: speaker -> R20 2.7k -> R8 270R at the bottom of the PI tail
    inline constexpr float kNfbRatio = 270.0f / (2700.0f + 270.0f);
} // namespace dumble::circuit
