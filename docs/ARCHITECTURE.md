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
    TriodeStage.h           stopień wspólnej katody — nodalny solver Newton-Raphson
    BrightVolume.h          potencjometr Volume 1M + kondensator Bright
    ToneStackTMB.h          TMB — dokładna transmitancja 3. rzędu, bilinear, double precision
    PhaseInverter.h         LTP 12AT7 — rozwiązanie pełnego obwodu DC → LUT
    PowerAmp6550.h          push-pull 4×6550, linia obciążenia, sag, blocking, transformator
    NegativeFeedback.h      pętla NFB z Presence/Deep
    PowerSection.h          element łańcucha: PI → Master → końcówka → NFB (pętla 1 próbki)
    DCBlocker.h, CabinetIR.h, OnePole.h, RealtimeSafety.h
    AmpEngine.*             łańcuchy ProcessorChain + 3× Oversampling + przełączanie
tests/                      testy jednostkowe (juce::UnitTest), działają też na Linuksie
```

## 2. Przepływ sygnału

```
[AXE I/O One in 1, Hi-Z]
  │  PreChain (fs)          dsp::Gain (Input) → dsp::IIR HPF 20 Hz
  ▼
  Oversampling::processSamplesUp (2x/4x/8x, polyphase IIR, integer latency)
  │  AmpChain (fs·N)        V1a 12AX7 → Volume/Bright → V1b 12AX7 → TMB → V2a 12AX7 (przez dzielnik miksera)
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
using AmpChain  = ProcessorChain<TriodeStage, BrightVolume, TriodeStage, ToneStackTMB, TriodeStage, PowerSection>;
using PostChain = ProcessorChain<DCBlocker, CabinetIR, dsp::Gain<float>>;
```

Każdy własny procesor spełnia kontrakt `prepare(const ProcessSpec&)`, `process(const Context&) noexcept`,
`reset()` i respektuje `context.isBypassed`.

## 3. Circuit modeling — modele

| Blok | Metoda |
|---|---|
| **Trioda 12AX7/12AT7** | Model Korena `Ip(Vgk, Vpk)` z analitycznymi pochodnymi; prąd siatki jako miękka dioda `Ig ∝ Vgk^1.5`. |
| **Stopień wspólnej katody** | Węzły `Vp`, `Vk` rozwiązywane Newtonem-Raphsonem (2×2, analityczny Jakobian). `Ck` i pojemność węzła anody `Cp` dyskretyzowane trapezowym modelem towarzyszącym. Ciepły start z poprzedniej próbki, **twardy limit 4 iteracji** (wcześniejsze wyjście po zbieżności) → ograniczony koszt na próbkę. Przewodzenie siatki przez opornik szeregowy — osobny skalarny Newton. Pojemność Millera (LPF wejścia) i kondensator sprzęgający (HPF na obciążeniu). Punkt pracy DC liczony w `prepare()`. |
| **Volume + Bright** | `H(s) = Rb(1 + sCRt) / (Rt + Rb + sCRtRb)`, potencjometr log (audio taper), bilinear. |
| **Tone stack TMB** | Dokładna transmitancja 3. rzędu wyprowadzona symbolicznie (analiza węzłowa) dla okablowania blackface/SSS (Bass i Mid jako reostaty) — metoda Yeh & Smith (DAFx-06). Bilinear, TDF-II w `double` (bieguny blisko z=1 przy 8x). Test porównuje z niezależnym numerycznym MNA: błąd 0.0000 dB. |
| **Odwracacz fazy (LTP)** | Pełny nieliniowy obwód DC (3 niewiadome, Newton z numerycznym Jakobianem) rozwiązany w `prepare()` dla 2049 napięć wejściowych → tablica `std::array` (bez alokacji). W czasie rzeczywistym tylko interpolacja. Siatki AC uziemione kondensatorami (poprawna degeneracja ogona → zbalansowane wyjścia 82k/100k). |
| **Końcówka 4×6550** | Koren beam-tetrode; bias stały wyznaczany bisekcją dla 40 mA/lampę. Wspólne rozwiązanie napięć anod przez uzwojenie z odczepem (`Va + Vb = 2Vs`, skalarny Newton). Sag zasilacza (RC + rezystancja źródła), przesunięcie biasu przy przewodzeniu siatek (blocking distortion), transformator: HPF (indukcyjność), LPF (rozproszenie), łagodne nasycenie rdzenia przy niskich f. |
| **NFB** | Z uzwojenia głośnikowego do drugiej siatki LTP; Presence = odjęcie HF z pętli, Deep = odjęcie LF. Pętla zamknięta z opóźnieniem 1 próbki (≤ 10 µs przy 2x–8x). |
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
| V1a punkt pracy | Vp = 203 V, Vk = 1.46 V, Ip = 0.97 mA |
| V1a wzmocnienie 1 kHz | −60.8 (odwracające) |
| 6550 bias | −55.1 V dla 40 mA/lampę |
| Aliasing (ton 4 kHz, granica przesteru) | 2x −30 dB, 4x −40 dB, 8x −52 dB |
| THD, wejście 50 mV, Volume 2 / 4 / 6 / 8 | 0.3 % / 0.8 % / 2.9 % / 18 % |
| Alokacje na ścieżce audio | 0 |
| CPU (bufor 64 @ 48 kHz, 1 rdzeń VM) | 2x ≈ 10 %, 4x ≈ 19 %, 8x ≈ 37 % |

## 8. Dalsze kroki

1. Weryfikacja wartości „VERIFY” ze schematem SSS; netlista ngspice i porównanie punktów pracy / THD.
2. Optymalizacja CPU przy 8x (tablicowanie Korena 2D lub szybkie aproksymacje exp/log).
3. Pogłos (tube reverb SSS) w miejscu dzielnika miksera przed V2a.
4. Podpisywanie i notaryzacja (Developer ID), walidacja `auval` + `pluginval` na macOS 26.
