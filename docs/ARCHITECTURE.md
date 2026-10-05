# Dumble Steel String Singer — architektura

Standalone (macOS 26) + Audio Unit v2, JUCE 9.0.3, bez VST3. Wzmacniacz modelowany metodą
**circuit modeling** (rozwiązywanie równań obwodu lampowego w czasie rzeczywistym), pracujący
z interfejsem **IK Multimedia AXE I/O One** w konfiguracji mono in → mono out.

## 0. Źródło schematu

Fabryczny schemat Dumble nie jest publiczny. Model odwzorowuje **społecznościową rekonstrukcję
SSS #002** (Ryan Colgan / The Amp Garage, TheToneGeek), opartą na zdjęciach wnętrza:

* `github.com/colganr/LTSpiceCircuits` → `Dumble Steel String Singer sn002/SSS No 002.asc`
  (netlist LTspice; oznaczenia R1, C2, U37… w kodzie odnoszą się do niego),
* `github.com/colganr/steel-string-singer-sn-002` → layout, BOM.

Poszczególne egzemplarze SSS (#001/#002/#004/#005) różniły się — to jest model **#002**.
Lampy mocy: **4×6L6GC** (jak w symulacji).

Poprawione pomyłki pliku symulacji (z BOM / intencji konstruktora):
R25, R26, R66 wpisane jako „1m” (1 mΩ) → 1 MΩ; R39 „2.2m” → 2.2 MΩ; potencjometr Master (U41)
narysowany bez uziemionego końca → podłączony jak zwykły potencjometr głośności.
Transformator wyjściowy z BOM: ClassicTone 40-18102 / Hammond 1760W, Raa 2 kΩ : 8 Ω.

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
    CircuitConstants.h      wartości elementów i napięcia zasilania SSS #002 (z oznaczeniami schematu)
    TriodeModel.h           Koren: triody 5751 / 7025 / 12AX7 (z VCT), pentoda 6L6GC, prąd siatki
    KorenTable.h            stablicowany Koren (Ip + pochodne, interpolacja bikubiczna Hermite'a)
    LinearNetwork.h         sieć liniowa R/L/C: MNA + trapezy → postać stanowa O(K²)
    TriodeStage.h           stopień wspólnej katody — Newton 2×2 z dokładnym obciążeniem sieci
    CathodeFollower.h       wtórnik katodowy (U38/U40, driver U12/U13) — skalarny Newton
    Pots.h                  charakterystyki potencjometrów, przełączniki
    Preamp002.h             przedwzmacniacz V1 → TMB/Volume/Bright/Deep → V4 → High/Low → U37/U38 + tor pogłosu
    SpringTank.h            zbiornik sprężynowy (dyspersyjne allpassy, zdecymowany)
    MasterStage.h           mikser → Master (+Accent) → siatka PI
    PhaseInverter.h         LTP 7025 — rozwiązanie pełnego obwodu DC → LUT
    PentodeTable.h          stablicowana pentoda 6L6GC (rozkład na tablice 1D, Hermite)
    PowerAmp6L6.h           driver CF DC → 4×6L6GC push-pull, sag, OT
    PowerSection.h          element łańcucha: PI → końcówka ⇄ globalne NFB (sub-kroki ≥ 176.4 kHz)
    DCBlocker.h, CabinetIR.h, OnePole.h, RealtimeSafety.h
    AmpEngine.*             łańcuchy ProcessorChain + 3× Oversampling + przełączanie
tests/                      testy jednostkowe (juce::UnitTest), działają też na Linuksie
spice/                      modele Korena dla ngspice + generator referencji (gen_sss002_refs.py)
scripts/sign_and_notarize.sh  podpis Developer ID, DMG, notaryzacja, staple
resources/Standalone.entitlements  hardened runtime: audio-input
```

## 2. Przepływ sygnału

```
[AXE I/O One in 1, Hi-Z]
  │  PreChain (fs)          dsp::Gain (Input) → dsp::IIR HPF 20 Hz
  ▼
  Oversampling::processSamplesUp (2x/4x/8x, polyphase IIR, integer latency)
  │  AmpChain (fs·N)
  │   Preamp002   V1 5751 → tone stack (Treble/Middle/Bass) → Volume 1M + Bright 250p (+ Deep)
  │               → V4 5751 → sieć filtrów (dławik 300 mH, High 7 poz., Low 7 poz.) → U37 7025 → U38 CF ─┐
  │               lokalne NFB: V4 → katoda V1 (LNFB), U38 → katoda V4 (LNFB2)                          │ 220k
  │               V1 → U20 (Send) → zbiornik → U28 (recovery) → Return → U39 → U40 CF ──────────────────┴─ 220k → mikser
  │   MasterStage C49 → Master 1M (+ Accent 1n) → C9 → siatka PI
  │   PowerSection LTP 7025 → driver CF 7025 (DC, bias −320 V) → 4×6L6GC → OT 2k:8 ⇄ NFB 2.7k/270 → ogon PI
  ▼
  Oversampling::processSamplesDown
  │  PostChain (fs)         DCBlocker → CabinetIR (dsp::Convolution, bypass) → dsp::Gain (Output)
  ▼
  sanity clamp (NaN/Inf → 0, |x| ≤ 4)  →  [out mono; przy szynie stereo kopiowane L=R]
```

`AmpEngine` definiuje łańcuchy jako `juce::dsp::ProcessorChain`:

```cpp
using PreChain  = ProcessorChain<dsp::Gain<float>, dsp::IIR::Filter<float>>;
using AmpChain  = ProcessorChain<Preamp002, MasterStage, PowerSection>;
using PostChain = ProcessorChain<DCBlocker, CabinetIR, dsp::Gain<float>>;
```

Każdy własny procesor spełnia kontrakt `prepare(const ProcessSpec&)`, `process(const Context&) noexcept`,
`reset()` i respektuje `context.isBypassed`.

Parametry (APVTS): `inputGain`, `volume`, `treble`, `middle`, `bass`, `reverbSend`, `reverbReturn`,
`master`, `output` (AudioParameterFloat + SliderAttachment), `bright`, `deep`, `accent`, `cabOn`
(przełączniki), `highFilter`, `lowFilter` (1–7), `oversampling` (2x/4x/8x).

## 3. Circuit modeling — modele

| Blok | Metoda |
|---|---|
| **Triody 5751 / 7025** | Koren `Ip(Vgk + VCT, Vpk)` z parametrami z `Koren_Tubes.INC` (te same, co w rekonstrukcji); prąd siatki jako miękkie kolano 0.36 V przez RGI. W czasie rzeczywistym tablica (Vgk −10…4 V co 0.02, Vpk 0…500 V co 1) z interpolacją bikubiczną Hermite'a (błąd 0.001 %). |
| **Stopnie wspólnej katody** (V1, V4, U37, U20, U28, U39) | Węzły `Vp`, `Vk` w `double`, Newton 2×2 z analitycznym Jakobianem, predyktor liniowy, limit 4 iteracji. Niebocznikowany rezystor katodowy (100 Ω) jako punkt wstrzyknięcia lokalnego NFB. Anoda widzi **dokładne obciążenie** sieci pasywnej: prąd sieci jest afiniczny w `Vp` (`i = αVp + β`, re-centrowany w `double`) i wchodzi do równań Newtona. |
| **Sieci pasywne** | `LinearNetwork`: MNA + trapezowe modele towarzyszące; po każdej zmianie wartości (co 32 próbki tylko podczas ruchu gałek) odwrócenie macierzy i wyprowadzenie postaci stanowej `x = P·h + q·vs` → koszt na próbkę O(K²) w liczbie elementów reaktywnych. Sieć przednia (TMB w okablowaniu „Guitar”, Volume, Bright, Deep, R69 + pojemność Millera V4) i sieć środkowa (C6, C37, R53 + L2 300 mH/59 Ω, przełącznik High 0/150p…10n, drabinka Low 39k…390k/12k, gałąź LNFB C15/R54, Miller U37). |
| **Wtórniki katodowe** | U38/U40 (bufory do miksera 220k/220k) i U12/U13 (driver DC siatek 6L6GC z zasilania −320 V, dzielnik 820k/130k): skalarny Newton w `double`, predyktor, limit kroku 5 V, przewodzenie siatki przez impedancję źródła. |
| **Lokalne NFB** | V4 → C15 0.1µ → R54 100k → katoda V1; U38 → 470k/0.22µ → katoda V4; U40 → 270k/0.1µ → katoda U28. Zamknięte z opóźnieniem 1 próbki (przy ≥ 96 kHz pomijalne wobec stałych czasowych pętli). |
| **Pogłos** | U20 (send) → potencjometr Send → driver/zbiornik (2 sprężyny, kaskady dyspersyjnych allpassów, zdecymowane) → U28 (recovery, sprzężenie z U40) → Return → U39 → U40. Tor powrotu (U28 → Return → U39 → U40 i jego sprzężenie) dostaje sygnał o paśmie ~4.5 kHz, więc liczy się co R-tą próbkę (R = 1/2/4 przy 2x/4x/8x, zawsze ≥ 88.2 kHz) na uśrednionym wyjściu zbiornika, z interpolacją liniową; poziom pogłosu identyczny przy każdym współczynniku (±0.03 dB). Kalibracja: wyjście zbiornika 0.1 (poziom recovery zgodny z symulacją). |
| **Master / Accent** | Sieć liniowa: mikser (Thevenin 110k) → C49 → Master 1M audio z C50 1n (Accent) → C9 .02µ → R10 1M. |
| **Odwracacz fazy (LTP 7025)** | Pełny nieliniowy obwód DC (płyty 108.75k/116.25k z balansem 25k, ogon 820 Ω + 18.27k) rozwiązany w `prepare()` → tablica; w czasie rzeczywistym interpolacja. |
| **Końcówka 4×6L6GC** | Koren pentoda (6L6GC), siatki 1.5k, ekrany 470 Ω. Bias z dzielnika driverów: −39.3 V → **72.9 mA/lampę** (gorący bias, jak w symulacji). Model Korena rozkłada się dokładnie na tablice 1D: `gridTerm = Vg2^ex · F(Vg1/Vg2)`, `Ig2 = P(Vg2/µ + Vg1)`, `atan(Vpk/kvb)` (błąd < 0.001 %). Wspólne rozwiązanie anod (Newton z predyktorem), sag zasilania, OT Raa 2 kΩ (500 Ω/strona) jako idealny transformator + HPF 10 Hz / LPF 18 kHz + łagodne nasycenie. |
| **Globalne NFB** | Głośnik → R20 2.7k → R8 270 Ω na dole ogona PI, β = 270/2970, ≈ 12.8 dB. Pętla zamknięta z opóźnieniem 1 próbki; `PowerSection` dzieli próbki na pod-kroki tak, by pętla zawsze pracowała przy ≥ 176.4 kHz (przy 96 kHz oscylowała). |
| **Kolumna** | `juce::dsp::Convolution`; wbudowany proceduralny IR 2x12 lub plik WAV/AIFF użytkownika. |

Wszystkie wartości elementów: `source/dsp/CircuitConstants.h` (z oznaczeniami schematu #002).

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

## 7. Weryfikacja i wyniki testów (Linux x86-64, VM, Release)

Referencja: `spice/gen_sss002_refs.py` buduje z wartości #002 netlistę ngspice całego
przedwzmacniacza (od wejścia do siatki PI, łącznie z siecią pogłosu w gałęzi dry, pojemnościami
lamp i diodami siatek; modele w `spice/koren_sss002.inc`), uruchamia `.op` / `.tran` + `.fourier`
i zapisuje `tests/Sss002SpiceReference.h`. Trzy konfiguracje gałek × sześć przypadków
(0.01 V przy 100 / 500 / 2000 / 6000 Hz; 0.1 V i 0.5 V przy 1 kHz).

| Test | Wynik |
|---|---|
| Punkt pracy vs ngspice `.op` (V1, V4, U37, U38) | zgodny (V1 Vp 208.85 V, U37 207.05 V — jak adnotacje rekonstrukcji) |
| H1 vs ngspice (18 przypadków) | najgorzej 0.56 dB (0.5 V, Bright + High 1 + Deep), typowo ≤ 0.2 dB |
| THD vs ngspice (przy THD > 0.5 %) | ≤ 22 % względnie (np. 5.74 % vs 5.66 %) |
| Koren — tablica vs model analityczny | 0.001 % |
| Bias 6L6GC | −39.3 V, 72.9 mA/lampę |
| Globalne NFB (96/192/384 kHz) | 12.8 dB, stabilne |
| Aliasing (ton 4 kHz, granica przesteru) | 2x −48 dB, 4x −48 dB, 8x −70 dB |
| Pogłos: ogon 0.2–0.6 s vs dry | > +80 dB; po 2.5 s ≈ −68 dB względem ogona |
| Alokacje na ścieżce audio (z pogłosem, IR, przełączaniem OS) | 0 |
| CPU (bufor 64 @ 48 kHz, 1 rdzeń VM, z IR i pogłosem) | 2x ≈ 19 %, 4x ≈ 24 %, 8x ≈ 39 % (macOS, Apple Silicon, 4x: ≈ 9 % wg wskaźnika DSP) |

Uwaga: przy sygnałach rzędu pojedynczych mV na wyjściu (ciemne ustawienia filtrów) zmierzone THD
jest zawyżone przez tolerancję Newtona (1e-4 V) — to szum solvera na poziomie ok. −90 dB, test THD
dotyczy tylko przypadków z THD referencji > 0.5 %.

## 8. Status kroków

1. **Pełna topologia #002 — zrobione:** 5751/7025, lokalne pętle NFB, filtry High/Low z dławikiem,
   Deep, Accent, mikser przez wtórniki, driver DC, 4×6L6GC, NFB do ogona PI. Zweryfikowane z ngspice.
2. **CPU:** tablice Korena z interpolacją bikubiczną, sieci w postaci stanowej, predyktor Newtona
   (8x: 74 % → 49 % rdzenia VM), tablice pentody 6L6GC (końcówka −27 %, 8x → 47.5 %).
   Tor powrotu pogłosu (U28/U39/U40) liczony w zdecymowanej częstotliwości (8x → 39 %).
   Kolejny kandydat, jeśli zajdzie potrzeba: SIMD dla sieci liniowych.
3. **Pogłos — zrobione** (`SpringTank.h`, gałki Reverb Send / Return, domyślnie 0).
4. **Podpis i notaryzacja — przygotowane:** `scripts/sign_and_notarize.sh` + `.github/workflows/release.yml`
   (tag `v*`). Wymaga certyfikatu Developer ID i klucza App Store Connect w sekretach repozytorium;
   nieprzetestowane bez tych poświadczeń.
