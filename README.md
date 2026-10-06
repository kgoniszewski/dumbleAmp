# dumbleAmp — Dumble Steel String Singer

Circuit-modelled emulation of the Dumble **Steel String Singer** guitar amplifier for macOS 26,
built with **JUCE 9.0.3** as a **Standalone app** and an **Audio Unit (AUv2)** (no VST3).
Designed for the **IK Multimedia AXE I/O One** (mono instrument input → mono output).

* full topology of the **SSS #002** community reconstruction (LTspice, colganr) — see docs
* 5751 / 7025 stages solved as nonlinear circuits (Koren model, Newton-Raphson) coupled to exact
  R/L/C networks: tone stack, Volume/Bright/Deep, 300 mH choke + 7-position High/Low filters,
  local feedback loops, cathode-follower mixer, Master/Accent, tube-driven spring reverb
* 7025 long-tail-pair phase inverter, DC-coupled cathode-follower driver, 4×6L6GC push-pull,
  2 kΩ OT, global NFB into the PI tail
* whole preamp cross-checked against ngspice (`spice/gen_sss002_refs.py`, `tests/Sss002Tests.cpp`)
* `juce::dsp::ProcessorChain`, `juce::dsp::Oversampling` (2x / 4x / 8x), `AudioProcessorValueTreeState`
* real-time safe `processBlock()` (verified: zero heap allocations on the audio path)

Architecture and design notes (Polish): [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

Design of a planned VOX AC30C2 emulation on the same core (Polish): [docs/AC30C2_ARCHITECTURE.md](docs/AC30C2_ARCHITECTURE.md)

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
