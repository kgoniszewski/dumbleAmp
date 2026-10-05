# Dumble Steel String Singer — architektura

Standalone (macOS 26) + Audio Unit v2, JUCE 9.0.3, bez VST3. Wzmacniacz modelowany metodą
**circuit modeling** (rozwiązywanie równań obwodu lampowego w czasie rzeczywistym), pracujący
z interfejsem **IK Multimedia AXE I/O One** w konfiguracji mono in → mono out.

---

## 1. Struktura repozytorium

```
CMakeLists.txt              FetchContent JUCE 9.0.3, juce_add_plugin(FORMATS Standalone AU)
cmake/Warnings.cmake        flagi DSP (bez -ffast-math), opcja RealtimeSanitizer
source/
  PluginProcessor.*         DumbleAudioProcessor: APVTS, busy mono, processBlock, latencja
  PluginEditor.*            GUI: SliderAttachment / ButtonAttachment / ComboBoxAttachment
  Parameters.*              ParamIDs, createParameterLayout(), ParamRefs (atomiki)
  StandaloneApp.cpp         własna aplikacja Standalone (auto-wybór AXE I/O One)
  ui/                       AmpLookAndFeel, LevelMeter
  dsp/
    CircuitConstants.h      wartości R/C/napięć SSS (constexpr; "VERIFY" = do weryfikacji ze schematem)
    TriodeModel.h           model Korena (trioda + pentoda/beam), pochodne analityczne
    KorenTable.h            stablicowany model Korena 12AX7 (Ip + pochodne, interpolacja dwuliniowa)
    TriodeStage.h           stopień wspólnej katody — nodalny solver Newton-Raphson
    SpringReverb.h          pogłos sprężynowy (driver 12AT7, zbiornik 2 sprężyn, recovery)
    BrightVolume.h          potencjometr Volume 1M + kondensator Bright
    ToneStackTMB.h          TMB — dokładna transmitancja 3. rzędu, bilinear, double precision
    PhaseInverter.h         LTP 12AT7 — rozwiązanie pełnego obwodu DC → LUT
    PowerAmp6550.h          push-pull 4×6550, linia obciążenia, sag, blocking, transformator
    NegativeFeedback.h      pętla NFB z Presence/Deep
    PowerSection.h          element łańcucha: PI → Master → końcówka → NFB (pętla 1 próbki)
    DCBlocker.h, CabinetIR.h, OnePole.h, RealtimeSafety.h
    AmpEngine.*             łańcuchy ProcessorChain + 3× Oversampling + przełączanie
tests/                      testy jednostkowe (juce::UnitTest), działają też na Linuksie
spice/                      netlisty referencyjne ngspice (cross-check solvera)
scripts/sign_and_notarize.sh  podpis Developer ID, DMG, notaryzacja, staple
resources/Standalone.entitlements  hardened runtime: audio-input
```

## 2. Przepływ sygnału

```
[AXE I/O One in 1, Hi-Z]
  │  PreChain (fs)          dsp::Gain (Input) → dsp::IIR HPF 20 Hz
  ▼
  Oversampling::processSamplesUp (2x/4x/8x, polyphase IIR, integer latency)
  │  AmpChain (fs·N)        V1a 12AX7 → Volume/Bright → V1b 12AX7 → TMB → [mikser: dry + Reverb] → V2a 12AX7 (przez dzielnik miksera)
  │                         → PowerSection [ LTP 12AT7 → Master → 4×6550 + OT ⇄ NFB(Presence, Deep) ]
  ▼
  Oversampling::processSamplesDown
  │  PostChain (fs)         DCBlocker → CabinetIR (dsp::Convolution, bypass) → dsp::Gain (Output)
  ▼
  sanity clamp (NaN/Inf → 0, |x| ≤ 4)  →  [out mono; przy szynie stereo kopiowane L=R]
```

`AmpEngine` definiuje łańcuchy jako `juce::dsp::ProcessorChain`:

```cpp
using PreChain  = ProcessorChain<dsp::Gain<float>, dsp::IIR::Filter<float>>;
using AmpChain  = ProcessorChain<TriodeStage, BrightVolume, TriodeStage, ToneStackTMB, SpringReverb, TriodeStage, PowerSection>;
using PostChain = ProcessorChain<DCBlocker, CabinetIR, dsp::Gain<float>>;
```

Każdy własny procesor spełnia kontrakt `prepare(const ProcessSpec&)`, `process(const Context&) noexcept`,
`reset()` i respektuje `context.isBypassed`.

## 3. Circuit modeling — modele

| Blok | Metoda |
|---|---|
| **Trioda 12AX7/12AT7** | Model Korena `Ip(Vgk, Vpk)` z analitycznymi pochodnymi; prąd siatki jako miękka dioda `Ig ∝ Vgk^1.5`. |
| **Stopień wspólnej katody** | Węzły `Vp`, `Vk` rozwiązywane Newtonem-Raphsonem (2×2, analityczny Jakobian). `Ck`, pojemność węzła anody `Cp` oraz kondensator sprzęgający `Cc` z obciążeniem następnego stopnia dyskretyzowane trapezowymi modelami towarzyszącymi (węzeł wyjściowy eliminowany analitycznie → anoda widzi rzeczywiste obciążenie AC). Ciepły start z poprzedniej próbki, **twardy limit 4 iteracji** (wyjście po zbieżności 1 mV) → ograniczony koszt na próbkę. Model lampy z tablicy `KorenTable` (budowana raz, poza wątkiem audio; poza siatką — model analityczny). Przewodzenie siatki — osobny skalarny Newton. Pojemność Millera jako LPF wejścia. Punkt pracy DC liczony w `prepare()`. **Zweryfikowane z ngspice** (`spice/v1a_stage.cir`). |
| **Volume + Bright** | `H(s) = Rb(1 + sCRt) / (Rt + Rb + sCRtRb)`, potencjometr log (audio taper), bilinear. |
| **Tone stack TMB** | Dokładna transmitancja 3. rzędu wyprowadzona symbolicznie (analiza węzłowa) dla okablowania blackface/SSS (Bass i Mid jako reostaty) — metoda Yeh & Smith (DAFx-06). Bilinear, TDF-II w `double` (bieguny blisko z=1 przy 8x). Test porównuje z niezależnym numerycznym MNA: błąd 0.0000 dB. |
| **Odwracacz fazy (LTP)** | Pełny nieliniowy obwód DC (3 niewiadome, Newton z numerycznym Jakobianem) rozwiązany w `prepare()` dla 2049 napięć wejściowych → tablica `std::array` (bez alokacji). W czasie rzeczywistym tylko interpolacja. Siatki AC uziemione kondensatorami (poprawna degeneracja ogona → zbalansowane wyjścia 82k/100k). |
| **Końcówka 4×6550** | Koren beam-tetrode; bias stały wyznaczany bisekcją dla 40 mA/lampę. Wspólne rozwiązanie napięć anod przez uzwojenie z odczepem (`Va + Vb = 2Vs`, skalarny Newton). Sag zasilacza (RC + rezystancja źródła), przesunięcie biasu przy przewodzeniu siatek (blocking distortion), transformator: HPF (indukcyjność), LPF (rozproszenie), łagodne nasycenie rdzenia przy niskich f. |
| **NFB** | Z uzwojenia głośnikowego do drugiej siatki LTP; Presence = odjęcie HF z pętli, Deep = odjęcie LF. Pętla zamknięta z opóźnieniem 1 próbki (≤ 10 µs przy 2x–8x). |
| **Pogłos sprężynowy** | Driver 12AT7 + transformator (miękkie nasycenie) → 2 sprężyny: kaskada 40 rozciągniętych allpassów 1. rzędu (`z^-K`, dyspersyjny „chirp”, Välimäki/Parker/Abel 2010) w tłumionej pętli opóźnienia 56/69 ms → recovery → mikser przed V2a. Zbiornik ma pasmo ~4.5 kHz, więc liczony jest w zdecymowanej częstotliwości wewnętrznej (40–80 kHz) także przy 8x: LPF 4. rzędu → co D-ta próbka → sample-and-hold → LPF 4. rzędu. Reverb = 0 jest bit-transparentny. RT60 ≈ 1.6 s. |
| **Kolumna** | `juce::dsp::Convolution`; wbudowany proceduralny IR 2x12 lub plik WAV/AIFF użytkownika. |

Wszystkie wartości elementów: `source/dsp/CircuitConstants.h`. Pozycje oznaczone **VERIFY**
wymagają porównania z referencyjnym schematem SSS / netlistą SPICE.

## 4. Oversampling 2x / 4x / 8x

* W `prepare()` tworzone są **trzy** `juce::dsp::Oversampling<float>` (1 kanał,
  `filterHalfBandPolyphaseIIR`, max quality, integer latency) i **trzy** przygotowane `AmpChain`.
* Przełączenie (parametr `oversampling`, nieautomatyzowalny) w `processBlock`:
  blok N — wyciszenie starej ścieżki rampą; blok N+1 — `reset()` nowej ścieżki i rampa w górę.
  Zero alokacji, zero przeliczeń.
* Latencja: wartości dla każdego współczynnika liczone w `prepare()` i trzymane w atomikach;
  `setLatencySamples()` + `updateHostDisplay()` wywoływane **wyłącznie** z `juce::Timer` na wątku
  wiadomości (oraz w `prepareToPlay`). Zmierzone: 2x = 4, 4x = 6, 8x = 6 próbek.

## 5. Rygor real-time w `processBlock()`

* `juce::ScopedNoDenormals`; brak `new/delete`, locków, `String`, logów, I/O, `MessageManager`.
* Parametry: `ParamRefs` trzyma wskaźniki `std::atomic<float>*` z `getRawParameterValue()` pobrane
  raz w konstruktorze → w bloku tylko `load(memory_order_relaxed)`.
* Wszystkie bufory/filtry/tablice przygotowane w `prepareToPlay`; bloki dłuższe niż zapowiedziane
  są dzielone na pod-bloki zamiast realokacji.
* Wygładzanie: `SmoothedValue` + przeliczanie współczynników co 16/32 próbki tylko podczas ruchu.
* Solvery nieliniowe z twardym limitem iteracji (ograniczony WCET).
* GUI ↔ audio wyłącznie przez atomiki (mierniki szczytowe, obciążenie CPU przez `AudioProcessLoadMeasurer`).
* IR kolumny ładowany z wątku wiadomości (`Convolution::loadImpulseResponse` jest wait-free).
* Nie używamy `-ffast-math` — ostatnia linia obrony `std::isfinite()` musi działać.
* **Weryfikacja:** test podmienia globalne `operator new/delete` i liczy alokacje na wątku audio podczas
  przetwarzania z ciągłą zmianą parametrów i przełączaniem oversamplingu → **0 alokacji, 0 zwolnień**.
  Dodatkowo `-DDUMBLE_RTSAN=ON` (LLVM clang ≥ 20) włącza RealtimeSanitizer dla funkcji oznaczonych
  `DUMBLE_NONBLOCKING` (`[[clang::nonblocking]]`).

## 6. AU / Standalone / AXE I/O One

* `DumbleAudioProcessor` jest czystym `juce::AudioProcessor` — ten sam kod dla AU i Standalone.
* Szyny: wejście mono; wyjście mono lub stereo (mono zduplikowane — część hostów AU wymaga stereo).
* AU: `aufx` / `Dsss` / `Kgon`, `ParameterID{id, 1}` (stabilne ID), brak MIDI.
* Standalone (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`): przy pierwszym uruchomieniu wyszukuje
  urządzenie zawierające „AXE I/O” i ustawia: wejście 1 (Hi-Z), wyjścia 1+2, 48 kHz, 64 próbki;
  „mute input” domyślnie wyłączone (wejście to gitara, nie mikrofon). Później obowiązują zapisane ustawienia.
* macOS: `MICROPHONE_PERMISSION_ENABLED`, hardened runtime z `com.apple.security.device.audio-input`,
  universal binary arm64 + x86_64, deployment target 13.0.

## 7. Wyniki testów (Linux x86-64, VM, Release)

| Test | Wynik |
|---|---|
| TMB: wzór zamknięty vs numeryczne MNA | 0.0000 dB |
| TMB: filtr cyfrowy (192 kHz) vs analogowy < 10 kHz | 0.0045 dB |
| V1a punkt pracy vs ngspice `.op` | Vp 202.95 V / 202.95 V, Vk 1.456 V / 1.456 V |
| V1a H1 i THD @1 kHz vs ngspice `.fourier` (0.1 / 1 / 3 V) | H1 −0.3…0 %, THD −0.4…−0.6 % względnie |
| V1a wzmocnienie 1 kHz (z obciążeniem 1 MΩ) | −58.9 (odwracające) |
| 6550 bias | −55.1 V dla 40 mA/lampę |
| Aliasing (ton 4 kHz, granica przesteru) | 2x −31 dB, 4x −41 dB, 8x −52 dB |
| Pogłos: ogon 0.2–0.6 s vs dry | ~600× (+56 dB); po 2.5 s < −90 dB |
| Alokacje na ścieżce audio (z pogłosem, IR, przełączaniem OS) | 0 |
| CPU (bufor 64 @ 48 kHz, 1 rdzeń VM, z IR i pogłosem) | 2x ≈ 6 %, 4x ≈ 11 %, 8x ≈ 16–19 % (wcześniej 10 / 19 / 37 %) |

## 8. Status kroków

1. **Solver vs SPICE — zrobione.** Stopień V1a zgadza się z ngspice (ten sam model Korena); test regresyjny
   `tests/SpiceTests.cpp`. **Wartości elementów oznaczone „VERIFY” nadal wymagają porównania
   z rzeczywistym schematem SSS** — SPICE potwierdza poprawność solvera, nie zgodność wartości z oryginałem.
2. **CPU przy 8x — zrobione:** tablica Korena (×~2), `logf` zamiast `log1pf`, tolerancja Newtona 1 mV.
   Własne aproksymacje exp/log okazały się wolniejsze niż libm (glibc FMA) — odrzucone.
3. **Pogłos — zrobione** (`SpringReverb.h`, parametr `reverb`, domyślnie 0).
4. **Podpis i notaryzacja — przygotowane:** `scripts/sign_and_notarize.sh` + `.github/workflows/release.yml`
   (tag `v*`). Wymaga certyfikatu Developer ID i klucza App Store Connect w sekretach repozytorium;
   nieprzetestowane bez tych poświadczeń.
