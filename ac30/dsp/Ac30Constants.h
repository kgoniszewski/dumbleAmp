#pragma once

#include "dsp/TriodeModel.h"

// Component values of the VOX AC30C2, from the factory schematics (VOX R&D UK, ISS3a, 15/12/2009):
// "AC30C2 PreAmp", "AC30C2 Power Amp", "AC30C2 Rev/FX". Designators (R14, VR9, U1B ...) refer to them.
// Values marked [est] are not on the schematics and were derived from the circuit (see
// docs/AC30C2_ARCHITECTURE.md); [AUT] marks values fixed by the project author.
namespace ac30::circuit
{
    //==========================================================================
    // Signal level convention: 1.0f digital full scale == 1 V at the input jack.
    inline constexpr float kVoltsPerFullScale = 1.0f;

    //==========================================================================
    // 12AX7 inter-electrode capacitances (RCA data): the Miller effect comes out of the networks.
    inline constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12, kCpk = 0.46e-12;

    //==========================================================================
    // Input jacks (input board): 10k (R1/R2, R5/R6) + ferrite, then 2 x 56k per grid on the main board
    // (TB: R7/R8, Normal: R18/R20), grid leak 1M (R9, R17). Hi: both 56k in parallel; Lo: the second
    // 56k + 10k goes to ground through the Hi jack's switch -> divider.
    inline constexpr double kJackR = 10.0e3, kGridR = 56.0e3;
    inline constexpr double kHiSeries = kJackR + kGridR * 0.5;                    // 38k
    inline constexpr double kLoSeries = (kJackR + kGridR) * 0.5;                  // 33k (Thevenin)
    inline constexpr double kLoGain = 0.5;
    inline constexpr double kC13 = 120.0e-12;                                     // C13, TB grid to cathode

    //==========================================================================
    // V1 (12AX7): TB triode R14 220k, Normal triode R12 100k, shared cathode R16 1k5 || C11 22u, from B+5
    inline constexpr double kV1PlateTB = 220.0e3, kV1PlateN = 100.0e3;
    inline constexpr double kV1Cathode = 1.5e3, kV1CathodeCap = 22.0e-6;

    // Normal channel: C7 47n -> R13 330k || C8 120p -> VR1 A500K -> R37 470k -> U1B (-)
    inline constexpr double kC7 = 47.0e-9, kR13 = 330.0e3, kC8 = 120.0e-12, kVR1 = 500.0e3;

    // Top Boost: C9 470p -> VR2 A500K (bright C15 120p top -> wiper) -> V2a grid
    inline constexpr double kC9 = 470.0e-12, kVR2 = 500.0e3, kC15 = 120.0e-12;

    // V2 (12AX7) from B+4: V2a R32 100k, R26 1k5 || C18 22u; V2b cathode follower (grid on the V2a plate),
    // R21 56k 1 W || C84 1n
    inline constexpr double kV2Plate = 100.0e3, kV2Cathode = 1.5e3, kV2CathodeCap = 22.0e-6;
    inline constexpr double kV2bLoad = 56.0e3, kC84 = 1.0e-9;

    // Top Boost tone stack: C23 56p (treble), R19 100k, C28 22n, C38 22n, VR3 A1M treble, VR4 A1M bass
    // (rheostat), R47 10k; out of the treble wiper: C25 220n -> R34 330k / R35 120k -> U1B (+)
    inline constexpr double kC23 = 56.0e-12, kR19 = 100.0e3, kC28 = 22.0e-9, kC38 = 22.0e-9;
    inline constexpr double kVR3 = 1.0e6, kVR4 = 1.0e6, kR47 = 10.0e3;
    inline constexpr double kC25 = 220.0e-9, kR34 = 330.0e3, kR35 = 120.0e3;

    // U1B (NJM2147, +/-27 V) mixer: R37 470k, R40 820k
    inline constexpr double kR37 = 470.0e3, kR40 = 820.0e3;
    inline constexpr float kOpAmpSwing = 25.0f; // output swing from +/-27 V rails [est]

    //==========================================================================
    // Rev/FX board (all NJM2147)
    // FX loop bypassed: send divider R41 470R / R38 4k7 / R36 1k8, return amp U1A 1 + R30 5k1 / R31 1k8
    inline constexpr float kFxGain = (float) ((1.8e3 / (470.0 + 4.7e3 + 1.8e3)) * (1.0 + 5.1e3 / 1.8e3));
    inline constexpr float kFxHighpassHz = 13.0f;      // C24 220n into ~55k [est]
    // driver U2B: R42 47k, R43/R44 1M, C37 560p, gain 1 + R48 330k / R45 5k6
    inline constexpr float kDriverHighpassHz = 272.0f; // C37 into ~1.045M [est]
    inline constexpr float kDriverGain = (float) ((1.0e6 / 1.047e6) * (1.0 + 330.0e3 / 5.647e3));
    // tank 9EB2C1B / BL3EB3C1B: 600 R in, 2250 R out [AUT]; spring model reused from dumble::SpringTank
    inline constexpr float kTankDrive = 0.05f;          // driver volts -> spring model input [est]
    inline constexpr float kTankOutput = 1.5f;          // spring model output -> recovery input volts [est]
    // recovery U5A: R76 8k2 + C56 56n to ground, R77 100k || C55 330p feedback
    inline constexpr float kRecoveryHighpassHz = 347.0f, kRecoveryLowpassHz = 4820.0f;
    inline constexpr float kRecoveryGain = (float) (100.0e3 / 8.2e3);
    // Level / Tone / sum: R78 33k -> VR6 B100K; VR5 A500K || C47 47p in series with C49 10n (wiper shunt)
    // [est: VR5 wiring]; dry R85 56k + wet R84 56k into R83 22k -> U5B 1 + R80 3k9 / R82 1k
    inline constexpr double kR78 = 33.0e3, kVR6 = 100.0e3, kVR5 = 500.0e3, kC47 = 47.0e-12, kC49 = 10.0e-9;
    inline constexpr double kR84 = 56.0e3, kR85 = 56.0e3, kR83 = 22.0e3;
    inline constexpr float kSumGain = (float) (1.0 + 3.9e3 / 1.0e3);

    //==========================================================================
    // Phase inverter V3 (12AX7) from B+3: C45 100n, R57/R63 1M, R55 1k2, R60 47k, R67/R70 100k, C42 47p,
    // C48 100n (grid B to ground), C50/C51 100n
    inline constexpr double kC45 = 100.0e-9, kPiGridLeak = 1.0e6, kR55 = 1.2e3, kR60 = 47.0e3;
    inline constexpr double kPiPlate = 100.0e3, kC42 = 47.0e-12, kC48 = 100.0e-9, kPiCoupling = 100.0e-9;
    // Tone Cut: VR9 B220K + C80 4.7n between the phases
    inline constexpr double kVR9 = 220.0e3, kC80 = 4.7e-9;
    // Master (differential): R105 / R112 10k, VR10 A500K between the phases, R115 / R118 220k,
    // R113 / R114 / R116 1M; tremolo depth VR8 B500K drives the R115/R118 junction
    inline constexpr double kMasterSeries = 10.0e3, kVR10 = 500.0e3, kR118 = 220.0e3, kPowerGridLeak = 1.0e6;
    inline constexpr double kVR8 = 500.0e3;
    // EL84 grids: 3k3 stoppers (R61, R81, R101, R108), two grids per phase; input capacitance [est]
    inline constexpr double kPowerStopper = 3.3e3, kPowerGridCap = 25.0e-12;

    //==========================================================================
    // Tremolo oscillator: Q4 LND150N3 phase-shift oscillator from B+2 via R93 33k / C60 10u
    inline constexpr double kR93 = 33.0e3, kC60 = 10.0e-6, kR91 = 100.0e3;
    inline constexpr double kR90 = 470.0, kC66 = 100.0e-6, kR92 = 10.0e3;
    inline constexpr double kC63 = 22.0e-9, kR86 = 27.0e3, kVR7 = 2.2e6, kC64 = 10.0e-9, kR87 = 1.0e6;
    inline constexpr double kC62 = 10.0e-9, kR89 = 3.3e6, kC61 = 100.0e-9, kR94 = 510.0e3, kC67 = 22.0e-9;
    // LND150N3 as a square-law depletion MOSFET: IDSS ~1.5 mA, VGS(off) ~ -1.8 V [est, data sheet range]
    inline constexpr double kLndVto = -1.8, kLndBeta = 0.926e-3, kLndLambda = 0.002;

    //==========================================================================
    // Power amp: 4 x EL84, shared cathode R119 50R || C74 220u, screens 470R per tube, no global NFB
    inline constexpr double kR119 = 50.0, kC74 = 220.0e-6, kScreenR = 470.0;
    inline constexpr double kRaa = 4.0e3;                // [AUT]
    inline constexpr double kPrimaryLoadPerSide = kRaa / 4.0;
    inline constexpr double kSpeakerLoad = 16.0;          // 2 x 8 R in series on the 16 R tap
    inline constexpr float kSpeakerFullScale = 40.0f;    // speaker volts mapped to digital 1.0
    inline constexpr float kOtLowCutHz = 25.0f, kOtHighCutHz = 15000.0f, kOtCoreSaturation = 0.06f;

    //==========================================================================
    // Supply: 2 x ~262 VAC, 1N4007 full wave, R107/R109 22R; B+1 C72 47u -> R120 1k -> B+2 C68 100u;
    // B+3 (PI) = B+2 - R74 22k (C58 10u), B+4 (V2) = B+2 - R22 10k (C20 10u), B+5 (V1) = B+4 - R15 22k (C10 10u)
    inline constexpr double kNoLoadHT = 365.0;            // [est]
    inline constexpr double kSourceR = 90.0;              // windings + R107/R109 + reservoir averaging [est]
    inline constexpr double kC72 = 47.0e-6, kR120 = 1.0e3, kC68 = 100.0e-6;
    inline constexpr double kR74 = 22.0e3, kC58 = 10.0e-6, kR22 = 10.0e3, kR15 = 22.0e3;
} // namespace ac30::circuit

namespace ac30
{
/** EL84 / 6BQ5, Koren pentode fitted to the Philips data (Va = Vg2 = 250 V, Vg1 = -7.3 V: Ia 48 mA,
    Ig2 5.5 mA, gm 11.3 mA/V, ra 38k, mu(g2-g1) 19). See spice/koren_ac30.inc. */
inline constexpr dumble::KorenPentodeParams kEL84 { 19.0f, 1.35f, 630.0f, 2000.0f, 200.0f, 49.0f, 1000.0f };
} // namespace ac30
