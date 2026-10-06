# VOX AC30C2 — architektura pluginu (projekt)

Audio Unit v2 + Standalone, **bez VST3**, macOS 26.4, **JUCE 9.0.3**. Emulacja wzmacniacza **VOX AC30C2**
metodą **circuit modeling** — tą samą, co plugin Dumble SSS w tym repozytorium (`docs/ARCHITECTURE.md`).
Dokument jest projektem: opisuje docelową strukturę kodu, model obwodu, parametry i plan weryfikacji.
Kod jeszcze nie istnieje.

Konwencja znaczników przy wartościach elementów:

* **[AB]** — ampbooks.com, *Circuit Analysis of the Vox AC30*,
* **[VS]** — voxshowroom.com (AC30C2 / Top Boost „Under the Hood”),
* **[VAC]** — voxac30.org.uk (transformatory, EL84, Top Boost),
* **[AIK]** — Aiken Amplification, *Is the Vox AC-30 Really Class A?*,
* **[FOR]** — Music Electronics Forum / The Amp Garage / tone-lizard (Cut, bias, transformator),
* **[TAD]** — Tube Amp Doctor (zamiennik transformatora wyjściowego AC30),
* **[?]** — założenie projektowe / wartość do weryfikacji pomiarem egzemplarza C2 lub schematem serwisowym.

---

## 0. Zakres, źródła, niepewności

### 0.1 Co modelujemy

AC30C2 (Vox/Korg, od 2010) to współczesny, płytkowy AC30 z dwoma kanałami i efektami
**[VS]**:

| Sekcja | Gałki / funkcje |
|---|---|
| Normal | Volume; wejścia Hi / Lo |
| Top Boost | Volume, Treble, Bass; wejścia Hi / Lo |
| Reverb | Tone, Level (pogłos sprężynowy, sterowany półprzewodnikowo) |
| Tremolo | Speed, Depth (półprzewodnikowe) |
| Master | Tone Cut, Master Volume |
| Lampy | 3 × 12AX7 (ECC83), 4 × EL84 |
| Zasilanie | prostownik krzemowy (mostek 4 × 1N4007 zamiast GZ34) |
| Głośniki | 2 × 12″ Celestion G12M Greenback (C2) / Alnico Blue (C2X) |
| Moc | ok. 30 W znamionowo, ok. 33–40 W RMS przy przesterze |

Pomijamy: pętlę efektów (w hoście zastępuje ją wpięcie efektów przed/za pluginem), footswitch
(obejmowany automatyzacją parametrów), wyjścia na dodatkowe kolumny.

### 0.2 Stan wiedzy o obwodzie

Vox **nie publikuje** schematu AC30C2. Topologia toru lampowego jest jednak dziedzictwem AC30/6 Top Boost
i AC30CC, które są dobrze opisane:

* stopień wejściowy z dwoma triodami (Normal / Brilliant-Top Boost) na **wspólnym** rezystorze katodowym **[AB]**,
* Top Boost: stopień wzmacniający → **wtórnik katodowy sprzężony stałoprądowo** → stos tonów Treble/Bass,
  będący „Bassmanem bez środka” **[AB]**, **[VS]**,
* odwracacz fazy **long-tailed pair** z dużym ogonem 47 kΩ, wejście nieodwracające uziemione dla AC **[FOR]**,
* **Cut** jako potencjometr + kondensator między wyjściami PI — półkowy filtr przez znoszenie sygnałów w przeciwfazie **[FOR]**,
* 4 × EL84 push-pull (dwie pary równolegle), **polaryzacja katodowa** wspólnym rezystorem, **bez globalnego NFB** **[AB]**, **[AIK]**,
* w C2 pogłos i tremolo są **półprzewodnikowe** (op-ampy w torze send i return) **[VS]**.

Wartości elementów z vintage AC30 i AC30CC traktujemy jako punkt wyjścia; każda wartość, której nie
potwierdzono dla C2, jest w `Ac30Constants.h` opisana źródłem i może zostać skorygowana bez zmian
architektury (wszystkie wartości są danymi, nie stałymi w kodzie solverów).

---

## 1. Struktura repozytorium (docelowa)

Repozytorium już zawiera rdzeń circuit modeling dla Dumble SSS. AC30C2 powstaje jako **drugi target
`juce_add_plugin`**, a uniwersalne klasy DSP przechodzą do wspólnej biblioteki `ampcore`
(bez zmian zachowania Dumble — weryfikowane istniejącymi testami).

```
CMakeLists.txt                  + target VoxAC30C2 (FORMATS Standalone AU), opcja AMP_BUILD_AC30
ampcore/                        wspólna biblioteka (INTERFACE, nagłówki) — przeniesione z source/dsp/
  TriodeModel.h                 Koren: 12AX7/5751/7025, pentody 6L6GC + EL84, prąd siatki
  KorenTable.h                  stablicowany Koren (Hermite 2D)
  PentodeTable.h                + PentodeTable::forTubeEL84()
  LinearNetwork.h               MNA + trapezy → postać stanowa
  TriodeStage.h                 stopień wspólnej katody (Newton 2×2, obciążenie siecią)
  CathodeFollower.h             wtórnik katodowy (skalarny Newton)
  SpringTank.h                  zbiornik sprężynowy
  CabinetIR.h, DCBlocker.h, OnePole.h, Pots.h, RealtimeSafety.h
source/                         Dumble SSS (bez zmian poza ścieżkami include)
ac30/
  PluginProcessor.*             Ac30AudioProcessor: APVTS, busy, processBlock, latencja
  PluginEditor.*                panel C2
  Parameters.*                  ParamIDs, layout, ParamRefs (atomiki)
  StandaloneApp.cpp             auto-wybór AXE I/O One (wzorzec source/StandaloneApp.cpp)
  ui/Ac30LookAndFeel.h          paleta Vox (miedziany panel, „diamond” grill), mierniki
  dsp/
    Ac30Constants.h             wartości elementów + napięcia zasilania, ze znacznikami źródeł
    InputStageAC30.h            V1a/V1b 12AX7 na wspólnej katodzie (Newton 3×3)
    TopBoost.h                  V2a gain → V2b CF (DC) → stos Treble/Bass
    PreampAC30.h                wejścia Hi/Lo, kanały, Volume + bright, sumator, jumper
    ReverbOpAmp.h               send op-amp → SpringTank → return op-amp → Tone → Level
    Tremolo.h                   LFO + model optoizolatora (VCA)
    MasterCut.h                 Master → siatka PI
    PhaseInverterAC30.h         LTP 12AX7 (ogon 47k) → LUT, sieć Tone Cut + sprzęgające + upływy siatek EL84
    PowerAmpEL84.h              4×EL84 PP, wspólna katoda Rk∥Ck (bias shift), OT
    PowerSupplySS.h             prostownik krzemowy + RC sag
    Ac30Engine.*                ProcessorChain + 3× Oversampling + przełączanie
tests/                          + Ac30Tests.cpp, Ac30SpiceReference.h
spice/                          + gen_ac30_refs.py, fit_el84.py, koren_ac30.inc
```

Target w CMake (szkic; identyczne opcje jak `DumbleSSS`):

```cmake
juce_add_plugin(VoxAC30C2
    PRODUCT_NAME                  "AC30C2"
    COMPANY_NAME                  "kgoniszewski"
    BUNDLE_ID                     "com.kgoniszewski.ac30c2"
    PLUGIN_MANUFACTURER_CODE      Kgon
    PLUGIN_CODE                   Ac30
    FORMATS                       Standalone AU          # bez VST3
    AU_MAIN_TYPE                  kAudioUnitType_Effect
    IS_SYNTH FALSE  NEEDS_MIDI_INPUT FALSE  NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD       ${APPLE}
    MICROPHONE_PERMISSION_ENABLED TRUE
    HARDENED_RUNTIME_ENABLED      TRUE
    HARDENED_RUNTIME_OPTIONS      com.apple.security.device.audio-input)
target_link_libraries(VoxAC30C2 PRIVATE ampcore juce::juce_audio_utils juce::juce_dsp ...)
target_compile_definitions(VoxAC30C2 PUBLIC JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
```

> Nazwa produktu bez znaku towarowego „VOX” w nazwie binariów i bundle ID — to emulacja, nie produkt Vox/Korg.

---

## 2. Przepływ sygnału

```
[AXE I/O One in 1, Hi-Z]
  │  PreChain (fs)       dsp::Gain (Input) → dsp::IIR HPF 20 Hz
  ▼
  Oversampling::processSamplesUp (2x / 4x / 8x)
  │  AmpChain (fs·N)
  │   PreampAC30
  │     wejście Hi/Lo (1M / dzielnik 68k+68k) ─┬─► V1a 12AX7 (Normal) ─► 47n ─► Normal Vol 1M ──────────────┐
  │                                            │      ║ wspólna katoda 1.5k ∥ 25µ                              │
  │                                            └─► V1b 12AX7 (TB) ─► TB Vol 1M (+ bright 100–120p)          │ sumator
  │                                                 ─► V2a 12AX7 gain ═► V2b CF (DC) ─► stos Treble/Bass ───┤ (rezystory)
  │                                                                                                         ▼
  │   ReverbOpAmp    dry ─────────────────────────────────────────────────────────────────────────────┐
  │                  send op-amp ─► SpringTank ─► return op-amp ─► Tone ─► Level ──────────────────────┴─► suma
  │   Tremolo        LFO (Speed) ─► opto-VCA (Depth) na sygnale sumy
  │   MasterCut      Master Volume ─► siatka PI
  │   PhaseInverterAC30   LTP 12AX7, ogon 47k ─► sprzęgające ─► stoppery ─► siatki EL84
  │                       Tone Cut: pot + C między wyjściami PI
  │   PowerAmpEL84   4×EL84 PP (2 pary ∥), wspólna katoda Rk ∥ Ck, OT ≈ 4 kΩ : 8/16 Ω, bez NFB
  │                  ⇅ PowerSupplySS (krzemowy mostek, RC sag, prąd zależny od wysterowania)
  ▼
  Oversampling::processSamplesDown
  │  PostChain (fs)      DCBlocker → CabinetIR (2×12 Greenback / IR użytkownika) → dsp::Gain (Output)
  ▼
  sanity clamp (NaN/Inf → 0, |x| ≤ 4) → [out mono; szyna stereo: L = R]
```

Łańcuchy jak w `source/dsp/AmpEngine.h`:

```cpp
using PreChain  = juce::dsp::ProcessorChain<juce::dsp::Gain<float>, juce::dsp::IIR::Filter<float>>;
using AmpChain  = juce::dsp::ProcessorChain<PreampAC30, ReverbOpAmp, Tremolo, MasterCut, PowerSectionAC30>;
using PostChain = juce::dsp::ProcessorChain<DCBlocker, CabinetIR, juce::dsp::Gain<float>>;
```

`PowerSectionAC30` łączy `PhaseInverterAC30` + `PowerAmpEL84` + `PowerSupplySS` w jeden element łańcucha
(wspólny stan: prąd katody i napięcie zasilania wpływają na PI i na końcówkę w tej samej próbce).
Każdy procesor spełnia kontrakt `prepare(const ProcessSpec&)`, `process(const Context&) noexcept`,
`reset()` i respektuje `context.isBypassed`.

---

## 3. Stopnie i wartości elementów

### 3.1 Wejście (V1, 12AX7)

| Element | Wartość | Źródło |
|---|---|---|
| Wejście Hi / Lo | 1 MΩ do masy; Lo: dzielnik 68k/68k (−6 dB) | typowe Vox/Fender **[?]** |
| Rezystory anodowe V1a, V1b | 220 kΩ | **[AB]** (AC30CC: 100 kΩ **[VS]**; C2 **[?]**) |
| Wspólny rezystor katodowy | 1.5 kΩ ∥ 25 µF (obie triody) | **[AB]** |
| B+ stopnia | ≈ 275 V | **[AB]** |
| Punkt pracy | Vgk ≈ −1.6 V; wzm. bez obciążenia 37.8 dB; Zout ≈ 49 kΩ | **[AB]** |
| Sprzęgający Normal | 47 nF (−0.1 dB przy 82 Hz — pasmo płaskie) | **[AB]** |

**Wspólna katoda** sprzęga kanały: sygnał jednego kanału moduluje katodę drugiej triody (słyszalne przy
„jumperowaniu” i jako przesłuch). Dlatego V1a/V1b rozwiązujemy **jednym** układem nieliniowym (§4.1).

### 3.2 Kanał Top Boost

| Element | Wartość | Źródło |
|---|---|---|
| TB Volume | 1 MΩ audio, bright 100 pF (vintage) / 120 pF (CC) wejście → wiper | **[AB]**, **[VS]** |
| V2a (gain) | 220k / 1.5k ∥ 25µ (vintage), w C2 **[?]** | **[AB]** **[?]** |
| V2b | wtórnik katodowy, siatka **bezpośrednio** na anodzie V2a, Rk ≈ 100k **[?]** | **[AB]** |
| Stos TB | C treble 50 pF (CC: 56 pF), Treble 1 MΩ lin, R slope 100 kΩ, 2 × 22 nF, Bass 1 MΩ audio | **[AB]**, **[VS]**, **[?]** |

Stos TB to sieć Bassmana bez potencjometru Middle — tłumienie w środku pasma jest zatem stałe i głębokie,
a interakcja Treble/Bass silna. Wtórnik DC-coupled daje niską impedancję źródła dla stosu i ma własną
nieliniowość (przewodzenie siatki V2b przy dużym wysterowaniu V2a, asymetryczne obcięcie) — istotny składnik
„chime → grind” Top Boosta.

### 3.3 Sumator, efekty, Master

| Blok | Model | Źródło |
|---|---|---|
| Sumator kanałów | rezystory sumujące (wartości **[?]**, startowo 470k/470k) — sieć liniowa | **[?]** |
| Reverb send / return | op-ampy (szyny ±15 V **[?]**), zbiornik sprężynowy, Tone (półka), Level | **[VS]** |
| Tremolo | półprzewodnikowe: LFO + element modulujący (zakładamy optoizolator LED/LDR) | **[VS]**, **[?]** |
| Master Volume | 1 MΩ audio przed siatką PI **[?]** | **[?]** |

### 3.4 Odwracacz fazy i Tone Cut (V3, 12AX7)

| Element | Wartość | Źródło |
|---|---|---|
| Topologia | LTP, wejście nieodwracające uziemione dla AC | **[FOR]** |
| Ogon | 47 kΩ (duży ogon → dobrze zrównoważone wzmocnienia) | **[FOR]** |
| Rezystory anodowe | 100k / 100k **[?]** (Fender: 82k/100k) | **[?]** |
| Tone Cut | potencjometr (100k–250k) szeregowo z 4.7 nF **między wyjściami PI** | **[FOR]** |
| Sprzęgające → EL84 | 47 nF **[?]**, upływy siatek 220k **[?]**, stoppery 10k (vintage) / 5.6k **[?]** | **[?]** |

**Mechanizm Cut:** kondensator między dwoma węzłami w przeciwfazie widzi podwójne napięcie, więc dla każdej
strony działa jak bocznik `2C` do wirtualnej masy przez `R_cut/2`. Przy impedancji wyjściowej strony PI `Rs`:

```
f_c  ≈ 1 / (2π · (Rs + R_cut/2) · 2C)
półka HF ≈ (R_cut/2) / (Rs + R_cut/2)
```

R_cut → 0: pełny dolnoprzepustowy (maks. cięcie, f_c ≈ 400 Hz przy Rs ≈ 40k, C = 4.7n);
R_cut duże: płytka półka. W modelu Cut jest częścią sieci `LinearNetwork` obciążającej PI — nie osobnym EQ.

### 3.5 Końcówka mocy (4 × EL84)

| Element | Wartość | Źródło |
|---|---|---|
| Polaryzacja | katodowa, **wspólny** Rk dla 4 lamp: 82 Ω (do ~1963) → 50 Ω (potem); spotykane 47 Ω | **[FOR]**, **[AB]** |
| Spoczynek | ≈ 10 V na Rk → 0.2 A łącznie → ≈ 50 mA / lampę; ≈ 12.5 V na Rk przy 30 W | **[AIK]**, **[FOR]** |
| Moc anodowa | ≈ 16 W / lampę przy dopuszczalnych 12 W (EL84 celowo „przegrzane”) | **[FOR]** |
| Klasa | nominalnie „A”, w praktyce AB przy pełnej mocy (prąd katody rośnie 10 → 12.5 V) | **[AIK]** |
| OT | Raa ≈ 4 kΩ (w praktyce 3.3 k – >4 k), DCR pierwotnego ≈ 220 Ω, odczepy 4/8/16 Ω | **[VAC]**, **[TAD]** |
| NFB | **brak** globalnego sprzężenia zwrotnego | **[AB]** |
| B+ anod/ekranów | ≈ 330–340 V **[?]** (C2: prostownik krzemowy) | **[?]** |
| Ck (bocznik katody) | duży elektrolit (100–470 µF) **[?]** | **[?]** |

Brak NFB + polaryzacja katodowa = charakter AC30: wysoka impedancja wyjściowa (głośnik „rozmawia” ze
wzmacniaczem), dużo parzystych/nieparzystych harmonicznych z końcówki, **dynamiczne przesunięcie biasu**
przy przesterze (ładowanie Ck → kompresja i „bloom”).

### 3.6 Zasilanie

C2 ma mostek 4 × 1N4007 **[VS]** — brak sagu prostownika lampowego GZ34. Pozostaje sag wynikający z
rezystancji uzwojeń transformatora sieciowego i pojemności filtrujących: model RC sterowany średnim prądem
końcówki (§4.7). Opcjonalny przełącznik „GZ34” (tryb vintage) jest rozszerzeniem na później, nie częścią C2.

---

## 4. Modele (circuit modeling)

| Blok | Metoda | Reuse |
|---|---|---|
| **Triody 12AX7** | Koren `Ip(Vgk + VCT, Vpk)` (`k12AX7`, z VCT), prąd siatki miękkim kolanem 0.36 V przez RGI; w czasie rzeczywistym `KorenTable` (Hermite 2D, błąd ~0.001 %) | `TriodeModel.h`, `KorenTable.h` |
| **V1a/V1b, wspólna katoda** | węzły `Vp_a`, `Vp_b`, `Vk` w `double`; Newton **3×3** z analitycznym Jakobianem, predyktor liniowy, limit 4 iteracji; każda anoda ładowana afinicznie przez swoją sieć (`i = αVp + β`) | rozszerzenie `TriodeStage.h` → `InputStageAC30` |
| **V2a → V2b (DC)** | V2a jako `TriodeStage` ładowany siatką CF; V2b jako `CathodeFollower` z siatką = `Vp(V2a)` w tej samej iteracji (sekwencyjnie z predyktorem; przy ≥ 176 kHz błąd pomijalny) — przewodzenie siatki V2b przez impedancję anody V2a | `TriodeStage.h`, `CathodeFollower.h` |
| **Sieci pasywne** | `LinearNetwork` (MNA + trapezy → postać stanowa O(K²)): (1) Volume TB + bright + Miller V2a, (2) stos TB obciążający katodę V2b, (3) Normal Volume + sumator + wejście efektów, (4) Master + siatka PI, (5) sieć wyjść PI: Cut + sprzęgające + upływy + stoppery. Przeliczenie co 32 próbki tylko podczas ruchu gałek | `LinearNetwork.h`, `Pots.h` |
| **Odwracacz fazy** | pełny nieliniowy obwód DC LTP (ogon 47k, anody, wspólne zasilanie) rozwiązany w `prepare()` → LUT dwuwyjściowa; w czasie rzeczywistym interpolacja + dynamiczne obciążenie siecią (5) | wzorzec `PhaseInverter.h` |
| **Siatki EL84** | przewodzenie siatek (RGI) przez stoppery ładuje sprzęgające → **blocking distortion** i przesunięcie biasu siatki; prąd siatki wstrzykiwany do sieci (5) | `gridCurrent()` z `TriodeModel.h` |
| **EL84** | Koren pentoda; parametry dopasowane do krzywych Philips/Mullard skryptem `spice/fit_el84.py` i porównane z bibliotekami Korena; tablice 1D jak dla 6L6GC (`gridTerm = Vg2^ex·F(Vg1/Vg2)`, `Ig2`, `atan(Vpk/kvb)`) | `PentodeTable.h` + `forTubeEL84()` |
| **Końcówka 4×EL84** | dwie „lampy zastępcze” (pary równoległe: prąd ×2), anody rozwiązywane łącznie przez uzwojenie z odczepem (jak `PowerAmp6L6`), **plus węzeł katody** `Vk`: `Ck·dVk/dt = Σ(Ia+Ig2) − Vk/Rk` — wspólny Newton (anody + katoda); ekrany z B+ przez rezystor ekranowy **[?]** | `PowerAmp6L6.h` → `PowerAmpEL84` |
| **OT** | idealny transformator Raa ≈ 4k : 16 Ω (2×16 Ω Greenback równolegle → 8 Ω) + HPF indukcyjności pierwotnej, LPF rozproszenia, łagodne nasycenie rdzenia przy niskich częstotliwościach; brak NFB → impedancja wyjścia zależna od stanu lamp | wzorzec `PowerAmp6L6.h` |
| **Pogłos** | op-amp send: wzmocnienie liniowe + miękkie obcięcie przy szynach; `SpringTank` (2 sprężyny, dyspersyjne allpassy, zdecymowany); op-amp return; Tone = półka HF; Level = mieszanie z dry. Tor powrotu liczony w zdecymowanej częstotliwości (pasmo ~4.5 kHz), jak w SSS | `SpringTank.h`, `OnePole.h` |
| **Tremolo** | LFO (Speed ≈ 1–10 Hz **[?]**, kształt ~ sinus z lekkim spłaszczeniem) → model LDR: nieliniowa rezystancja, asymetryczne stałe czasowe (attack ≈ 5 ms, release ≈ 50 ms **[?]**) → dzielnik napięcia na sygnale sumy; Depth skaluje amplitudę LFO. Liczony z częstotliwością fs (sterowanie), aplikowany na fs·N | nowy `Tremolo.h` |
| **Kolumna** | `juce::dsp::Convolution`: wbudowany proceduralny IR 2×12 Greenback (rezonans ~75 Hz, „honk” 1–2 kHz, spadek > 5 kHz) lub plik WAV/AIFF; kompensacja normalizacji JUCE (×8·√(48 kHz/fs)) | `CabinetIR.h` |

Wszystkie wartości: `ac30/dsp/Ac30Constants.h` (struktury `TriodeStageValues` jak w `CircuitConstants.h`,
z komentarzem źródła przy każdej liczbie).

---

## 5. Oversampling 2x / 4x / 8x

Bez zmian względem SSS (`source/dsp/AmpEngine.cpp`):

* trzy `juce::dsp::Oversampling<float>` (1 kanał, `filterHalfBandPolyphaseIIR`, max quality, integer latency)
  i trzy przygotowane `AmpChain` tworzone w `prepare()`,
* przełączanie (parametr nieautomatyzowalny): blok N — rampa w dół starej ścieżki, blok N+1 — `reset()` nowej
  i rampa w górę; zero alokacji,
* latencja liczona w `prepare()`, `setLatencySamples()` + `updateHostDisplay()` tylko z `juce::Timer`,
* domyślnie 4x. Brak NFB w AC30 usuwa ograniczenie stabilności pętli, które w SSS wymuszało pod-kroki
  ≥ 176.4 kHz; jedyna pętla to ładowanie Ck katody EL84 (wolna, rozwiązywana jawnie w Newtonie).

---

## 6. Rygor real-time w `processBlock()`

Identyczne zasady jak w `docs/ARCHITECTURE.md` §5:

* `juce::ScopedNoDenormals`; brak `new/delete`, locków, `String`, logów, I/O, `MessageManager`,
* `ParamRefs` — wskaźniki `std::atomic<float>*` pobrane raz w konstruktorze, w bloku tylko `load(relaxed)`,
* bufory/sieci/tablice (Koren 12AX7, pentoda EL84, LUT PI) przygotowane w `prepareToPlay`; za długie bloki
  dzielone na pod-bloki,
* `SmoothedValue` + przeliczanie sieci co 16/32 próbki tylko podczas ruchu gałek,
* solvery z twardym limitem iteracji (ograniczony WCET), bez `-ffast-math` (`std::isfinite` musi działać),
* GUI ↔ audio wyłącznie przez atomiki (mierniki, faza LFO do diody tremolo, obciążenie CPU),
* IR kolumny ładowany z wątku wiadomości,
* weryfikacja: test `AllocationGuard` (0 alokacji przy ciągłej zmianie parametrów i przełączaniu OS),
  opcjonalnie RealtimeSanitizer (`-DDUMBLE_RTSAN=ON`, makro `DUMBLE_NONBLOCKING`).

---

## 7. AU / Standalone / macOS 26.4

* `Ac30AudioProcessor` — czysty `juce::AudioProcessor`, ten sam kod dla AU i Standalone.
* **AUv2** `aufx` / `Ac30` / `Kgon`, stabilne `juce::ParameterID{ id, 1 }`, brak MIDI. VST3 nie jest budowany
  (`FORMATS Standalone AU`); AUv3 poza zakresem (wymagałby app extension i sandboxu).
* Szyny: wejście mono; wyjście mono lub stereo (mono zduplikowane — Logic/GarageBand oczekują stereo).
* Standalone (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`): przy pierwszym uruchomieniu wybiera urządzenie
  „AXE I/O” (wejście 1 Hi-Z, wyjścia 1+2, 48 kHz, 64 próbki), „mute input” wyłączone; potem zapisane ustawienia.
* macOS 26.4 (Tahoe): Xcode 26, deployment target 13.0, universal binary arm64 + x86_64, hardened runtime
  z `com.apple.security.device.audio-input`, `NSMicrophoneUsageDescription`; podpis Developer ID i notaryzacja
  przez istniejący `scripts/sign_and_notarize.sh` (parametryzowany nazwą produktu).
* Walidacja: `auval -v aufx Ac30 Kgon`, `pluginval --strictness-level 10`; Logic Pro / GarageBand jako hosty testowe.
* Licencja: JUCE 9 — AGPLv3 lub licencja komercyjna (jak dla SSS).

---

## 8. Parametry (APVTS)

| ID | Typ | Zakres / wartości | Uwagi |
|---|---|---|---|
| `inputGain` | float | −24…+24 dB | kalibracja poziomu interfejsu |
| `channel` | choice | Normal / Top Boost / Jumper | Jumper = oba kanały równolegle (oba V1 odwracają, sygnały się sumują) |
| `inputLo` | bool | Hi / Lo | dzielnik −6 dB |
| `normalVolume` | float | 0–10 | pot 1M audio (`potAudio`) |
| `tbVolume` | float | 0–10 | pot 1M audio + bright |
| `treble` | float | 0–10 | pot 1M lin |
| `bass` | float | 0–10 | pot 1M audio |
| `reverbTone` | float | 0–10 | |
| `reverbLevel` | float | 0–10 | domyślnie 0 |
| `tremSpeed` | float | 0–10 | ≈ 1–10 Hz, krzywa wykładnicza |
| `tremDepth` | float | 0–10 | domyślnie 0 |
| `toneCut` | float | 0–10 | 0 = brak cięcia (jak gałka na panelu) |
| `master` | float | 0–10 | |
| `output` | float | −24…+12 dB | |
| `cabOn` | bool | | |
| `oversampling` | choice | 2x / 4x / 8x | nieautomatyzowalny |

Wszystkie `AudioParameterFloat`/`Choice`/`Bool` z `SliderAttachment` / `ComboBoxAttachment` / `ButtonAttachment`;
krzywe potencjometrów z `Pots.h` (`potLinear`, `potAudio`).

---

## 9. GUI

Panel w kolejności oryginału (od lewej): **Normal** Volume │ **Top Boost** Volume, Treble, Bass │
**Reverb** Tone, Level │ **Tremolo** Speed, Depth (+ dioda fazy LFO) │ **Master** Tone Cut, Master Volume.
Pasek dolny: wybór kanału / Jumper, Hi/Lo, Cabinet + *Load IR…*, Oversampling, Input/Output, mierniki
(`LevelMeter`), obciążenie CPU. `Ac30LookAndFeel` (pochodna `AmpLookAndFeel`): miedziany panel, czarne gałki
„chicken head”, tło w stylu diamond grill; skalowanie wektorowe (Retina).

---

## 10. Weryfikacja

**Referencje SPICE** — `spice/gen_ac30_refs.py` (wzorem `gen_sss002_refs.py`) buduje netlistę ngspice:
V1a/V1b ze wspólną katodą, TB (V2a + CF + stos), sumator, Master, LTP z siecią Cut i siatkami EL84,
modele Korena w `spice/koren_ac30.inc`; `.op` / `.tran` + `.fourier` → `tests/Ac30SpiceReference.h`.

| Test | Kryterium |
|---|---|
| Punkt pracy V1 vs `.op` i **[AB]** | Vgk ≈ −1.6 V, wzm. ≈ 37.8 dB |
| H1 / THD przedwzmacniacza vs ngspice (siatka gałek × 100 Hz…6 kHz, 0.01–0.5 V) | H1 ≤ 0.5 dB, THD ≤ 25 % względnie (przy THD > 0.5 %) |
| Stos TB vs rozwiązanie analityczne (funkcja przenoszenia) | ≤ 0.1 dB |
| Tone Cut | f_c i głębokość półki zgodne z ngspice dla 0/5/10 |
| Bias EL84 | ≈ 10 V na Rk, ≈ 50 mA/lampę; przy pełnej mocy ≈ 12.5 V **[AIK]** |
| Bias shift / blocking | wzrost Vk i czas powrotu po impulsie zgodny z ngspice |
| Moc wyjściowa | 30–40 W na 8 Ω przy przesterze |
| Pogłos, tremolo | poziom ogona, głębokość modulacji, brak trzasków przy zmianie Speed |
| Aliasing (ton 4 kHz, przester) | 2x ≤ −45 dB, 8x ≤ −65 dB |
| Alokacje na ścieżce audio | 0 |
| CPU (bufor 64 @ 48 kHz, Apple Silicon) | 4x ≤ 10 % rdzenia |

CI: `.github/workflows/ci.yml` — Linux (testy DSP obu wzmacniaczy, compile check Standalone),
macOS (build Xcode, testy, `auval -v aufx Ac30 Kgon`, pluginval).

---

## 11. Etapy implementacji

1. **Refaktor `ampcore`** — przeniesienie klas z `source/dsp/` do biblioteki; testy SSS bez zmian wyników.
2. **Przedwzmacniacz** — `InputStageAC30` (Newton 3×3), Normal, Top Boost (V2a + CF + stos), sumator; referencje ngspice.
3. **Końcówka** — dopasowanie Korena EL84, `PentodeTable::forTubeEL84()`, LTP + Cut, `PowerAmpEL84` z węzłem
   katody, `PowerSupplySS`, OT; testy biasu i bias shift.
4. **Efekty** — `ReverbOpAmp` (+ `SpringTank`), `Tremolo`.
5. **Plugin** — `Parameters`, `PluginProcessor`, `PluginEditor`, `StandaloneApp`, IR 2×12 Greenback.
6. **Walidacja i wydanie** — auval, pluginval, CPU, podpis + notaryzacja, DMG.
7. **Kalibracja** — wartości oznaczone **[?]** porównane z pomiarem egzemplarza C2 (napięcia, odpowiedź stosu,
   sweep Cut) lub schematem serwisowym; korekta `Ac30Constants.h`.

---

## 12. Źródła

* ampbooks.com — *Circuit Analysis of the Vox AC30*: https://www.ampbooks.com/mobile/classic-circuits/vox-ac30/
* ampbooks.com — *Bright Boost Capacitor Calculator*: https://www.ampbooks.com/mobile/amplifier-calculators/bright-boost/
* ampbooks.com — *Digital Modeling of a Guitar Amplifier Tone Stack*: https://www.ampbooks.com/mobile/dsp/tonestack/
* Vox Showroom — AC30C2 „Under the Hood”: https://www.voxshowroom.com/uk/amp/ac30c2_hood.html
* Vox Showroom — Top Boost „Under the Hood”: https://voxshowroom.com/uk/amp/ac30_tb_hood.html
* Vox Showroom — AC30CC service: https://www.voxshowroom.com/uk/amp/ccservice.html
* voxac30.org.uk — Top Boost circuit: https://www.voxac30.org.uk/vox_ac30_top_boost_circuit.html
* voxac30.org.uk — transformatory: https://www.voxac30.org.uk/vox_ac30_transformers.html
* voxac30.org.uk — Mullard EL84: https://www.voxac30.org.uk/vox_ac30_mullard_el84.html
* Aiken Amplification — *Is the Vox AC-30 Really Class A?*: https://www.aikenamps.com/is-the-vox-ac-30-really-class-a
* The Amp Garage — *What's happening with a Vox cut control*: https://ampgarage.com/forum/viewtopic.php?t=35508
* tone-lizard — *Vox AC30 myths*: http://tone-lizard.com/vox-myths/
* Music Electronics Forum — *Biasing an AC30*: https://music-electronics-forum.com/forum/amplification/guitar-amps/maintenance-troubleshooting-repair/4519-biasing-an-ac30
* Tube Amp Doctor — OT dla AC30: https://www.tubeampdoctor.com/en/output-transformer-for-vox-ac-30
* Steve's Amps — *Voxiness: what makes the Vox AC30 sound that way?*: https://www.stevesamps.co.uk/?p=287
* N. Koren, *Improved vacuum tube models for SPICE simulations* (modele triod/pentod)
