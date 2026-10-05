# dumbleAmp — Dumble Steel String Singer

Circuit-modelled emulation of the Dumble **Steel String Singer** guitar amplifier for macOS 26,
built with **JUCE 9.0.3** as a **Standalone app** and an **Audio Unit (AUv2)** (no VST3).
Designed for the **IK Multimedia AXE I/O One** (mono instrument input → mono output).

* 12AX7 preamp stages solved as nonlinear circuits (Koren tube model, Newton-Raphson)
* exact 3rd-order TMB tone stack, Volume/Bright, Presence/Deep NFB loop, tube-driven spring reverb
* triode stage cross-checked against ngspice (`spice/`, `tests/SpiceTests.cpp`)
* 12AT7 long-tail-pair phase inverter, 4×6550 push-pull power amp with sag and bias shift
* `juce::dsp::ProcessorChain`, `juce::dsp::Oversampling` (2x / 4x / 8x), `AudioProcessorValueTreeState`
* real-time safe `processBlock()` (verified: zero heap allocations on the audio path)

Architecture and design notes (Polish): [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

## Build (macOS 26, Xcode 26)

```sh
cmake -B build -G Xcode
cmake --build build --config Release
```

Artefacts: `build/DumbleSSS_artefacts/Release/Standalone/Dumble SSS.app` and
`.../AU/Dumble SSS.component` (copied to `~/Library/Audio/Plug-Ins/Components`).

Validate the AU:

```sh
auval -v aufx Dsss Kgon
pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/Components/Dumble\ SSS.component
```

## Tests

```sh
cmake -B build-tests -DDUMBLE_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
./build-tests/tests/DumbleTests_artefacts/Release/DumbleTests
```

The DSP tests run on macOS and Linux. `-DDUMBLE_RTSAN=ON` (LLVM clang ≥ 20) additionally builds
them with RealtimeSanitizer.

## Signed release (Developer ID + notarization)

Locally (after a Release build):

```sh
xcrun notarytool store-credentials dumble-notary --apple-id <id> --team-id <TEAMID> --password <app-specific-password>
DEVELOPER_ID_APP="Developer ID Application: <Name> (<TEAMID>)" NOTARY_PROFILE=dumble-notary ./scripts/sign_and_notarize.sh
```

On GitHub: push a `v*` tag; `.github/workflows/release.yml` signs, notarizes and attaches the DMG.
It needs the secrets listed at the top of that workflow.

## Using it with the AXE I/O One

1. Plug the guitar into input 1 (Hi-Z). Set the input to the clean/"Pure" setting, Z-Tone neutral.
2. Start **Dumble SSS.app** — on first launch it selects the AXE I/O One automatically
   (input 1, outputs 1+2, 48 kHz, 64-sample buffer). Use *Options → Audio/MIDI Settings* to change it.
3. Allow microphone (audio input) access when macOS asks.
4. Keep **Cabinet** on when monitoring through speakers/headphones; load your own IR with *Load IR…*.
5. Oversampling: 4x is the default; 8x for the lowest aliasing at higher CPU cost.

## Licence note

JUCE 9 is dual-licensed under AGPLv3 and the commercial JUCE licence. This repository is GPLv3;
distributing binaries requires either relicensing to AGPLv3 or a commercial JUCE licence.
