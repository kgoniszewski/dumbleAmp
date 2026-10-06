# VOX AC30C2 — architektura pluginu (projekt)

Audio Unit v2 + Standalone, **bez VST3**, macOS 26.4, **JUCE 9.0.3**. Emulacja wzmacniacza **VOX AC30C2**
metodą **circuit modeling** — tą samą, co plugin Dumble SSS w tym repozytorium (`docs/ARCHITECTURE.md`).
Dokument jest projektem: opisuje docelową strukturę kodu, model obwodu, parametry i plan weryfikacji.
Kod jeszcze nie istnieje.

Konwencja znaczników przy wartościach elementów:

* **[C2]** — schematy fabryczne AC30C2 (VOX R&D UK, D. Clarke / E. Tomiyama, 15.12.2009, rewizja ISS3a):
  *AC30C2 PreAmp*, *AC30C2 Power Amp*, *AC30C2 Rev/FX* — **źródło nadrzędne**; oznaczenia (R14, VR9, U1B…)
  odnoszą się do tych arkuszy,
* **[CC2]** — *Service Manual AC30CC2 / AC30CC2X* (KORG, 2005): schemat blokowy i preamp poprzednika — do porównań,
* **[AB]** — ampbooks.com, *Circuit Analysis of the Vox AC30*,
* **[VS]** — voxshowroom.com (AC30C2 / Top Boost „Under the Hood”),
* **[VAC]** — voxac30.org.uk (transformatory, EL84),
* **[AIK]** — Aiken Amplification, *Is the Vox AC-30 Really Class A?*,
* **[FOR]** — Music Electronics Forum / The Amp Garage (Cut, bias),
* **[AUT]** — ustalenia autora projektu (rozstrzygnięcia niejasności schematu, dane katalogowe zbiornika),
* **[est]** — wartość wyliczona z elementów [C2] (szacunek, do potwierdzenia symulacją),
* **[?]** — nieczytelne lub nieobecne na schemacie; do weryfikacji pomiarem egzemplarza.

---

## 0. Zakres, źródła, niepewności

### 0.1 Co modelujemy

AC30C2 (Vox/Korg, od 2010) — dwa kanały, efekty półprzewodnikowe, końcówka lampowa **[C2]**, **[VS]**:

| Sekcja | Gałki / funkcje | Elementy [C2] |
|---|---|---|
| Normal | Volume; wejścia Hi / Lo | VR1 A500K |
| Top Boost | Volume, Treble, Bass; wejścia Hi / Lo | VR2 A500K, VR3 A1M, VR4 A1M |
| Reverb | Tone, Level | VR5 A500K, VR6 B100K |
| Tremolo | Speed, Depth | VR7 C2.2M, VR8 B500K |
| Master | Tone Cut, Master Volume | VR9 B220K, VR10 A500K |
| Footswitch | Reverb on/off, Tremolo on/off | Q1 J174 (JFET), Q2 2SC2910 |
| Lampy | V1, V2, V3 = 12AX7; V4–V7 = EL84 | |
| Op-ampy | NJM2147 (U1–U5), zasilanie ±27 V | |
| Zasilanie | prostownik krzemowy (1N4007), bez lampy prostowniczej | |
| Głośniki | 2 × 8 Ω szeregowo (16 Ω); C2: Celestion G12M Greenback | |

Pomijamy: pętlę efektów (w modelu zawsze „bypass”; host zastępuje ją wpięciem efektów), gniazda dodatkowych kolumn
i przełącznik impedancji (model zakłada dopasowanie 16 Ω), przydźwięk żarzenia (żarzenie DC w C2).

### 0.2 Najważniejsze fakty topologiczne ze schematu C2

Schemat C2 zmienia kilka założeń typowych dla vintage AC30:

1. **Kanały miesza op-amp U1B**, nie rezystory: Top Boost wchodzi na „+”, Normal przez 470k na „−” → kanał Normal
   jest **w przeciwfazie** względem Top Boost (istotne przy graniu na obu kanałach naraz).
2. **Pogłos i pętla FX** są w całości na op-ampach (płytka Rev/FX) — między mikserem a odwracaczem fazy.
3. **Tone Cut i Master są za odwracaczem fazy** — Cut między fazami za kondensatorami sprzęgającymi, Master
   jako tłumik różnicowy przed siatkami EL84 (jak w CC2 **[CC2]**).
4. **Tremolo** to oscylator z przesuwnikiem fazy na tranzystorze MOSFET wysokiego napięcia (LND150N3), którego sygnał
   przez C48 **moduluje drugą siatkę odwracacza fazy** — nie optoizolator.
5. Końcówka: **wspólny rezystor katodowy 50 Ω ∥ 220 µF**, **brak globalnego NFB** (uzwojenie wtórne idzie wyłącznie do gniazd).

### 0.3 Rozstrzygnięte niejasności i pozostałe niepewności

Rozstrzygnięte **[AUT]**:

* **Transformator wyjściowy:** Raa = **4 kΩ** (anoda–anoda) — standardowe obciążenie 4 × EL84 w push-pull z polaryzacją
  katodową **[AUT]**, **[VAC]**. Lista części podaje tylko numer 550022-1000042212, bez parametrów **[CC2]**.
* **Zasilanie końcówki:** anody (odczep OT) z **B+1** — pierwszego węzła za prostownikiem; ekrany z **B+2**, za R120 1k 10 W
  i C68 100 µF, przez własne 470R na lampę **[AUT]**, **[C2]**. W C2 tę funkcję pełni rezystor R120; dławik 15 H z listy
  części dotyczy CC1/CC2 **[CC2]**.
* **Drabinka B+:** B+3 → odwracacz fazy (V3), B+4 → V2 (Top Boost), B+5 → V1 (wejście, najlepiej odfiltrowane) **[AUT]**, **[C2]**.
* **Zbiornik pogłosu:** 600 Ω na wejściu, 2250 Ω na wyjściu, montaż poziomy otwartą stroną w dół (§3.5) **[AUT]**.

Pozostaje:

* napięcia pracy — schematy C2 ich nie podają; wyliczamy je z elementów (**[est]**) i ze schematu CC2,
* czas wybrzmiewania zbiornika — oznaczenia „9EB2C1B” na arkuszu Rev/FX C2 i „BL3EB3C1B” w liście części CC2 różnią
  się cyfrą decay (2 = średni, 3 = długi), §3.5.

Skany schematów **nie są** dołączane do repozytorium (materiał chroniony); wartości są przepisane do
`ac30/dsp/Ac30Constants.h` z oznaczeniami elementów.

---

## 1. Struktura repozytorium (docelowa)

Repozytorium już zawiera rdzeń circuit modeling dla Dumble SSS. AC30C2 powstaje jako **drugi target
`juce_add_plugin`**, a uniwersalne klasy DSP przechodzą do wspólnej biblioteki `ampcore`
(bez zmian zachowania Dumble — weryfikowane istniejącymi testami).

```
CMakeLists.txt                  + target VoxAC30C2 (FORMATS Standalone AU), opcja AMP_BUILD_AC30
ampcore/                        wspólna biblioteka (nagłówki) — przeniesione z source/dsp/
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
  ui/Ac30LookAndFeel.h          paleta Vox, mierniki
  dsp/
    Ac30Constants.h             wartości elementów [C2] z oznaczeniami
    InputStageAC30.h            V1: dwie triody 12AX7 na wspólnej katodzie R16/C11 (Newton 3×3)
    TopBoost.h                  V2a gain → V2b CF (DC) → stos Treble/Bass (C23/R19/C28/C38/R47)
    OpAmp.h                     model NJM2147: wzmocnienie z sieci RC + miękkie nasycenie szyn ±27 V
    PreampAC30.h                wejścia Hi/Lo, Normal (C7, R13∥C8, VR1), TB (C9, VR2+C15), mikser U1B
    ReverbFx.h                  driver U2B + bufory 6×47R → SpringTank → recovery U5A → Level/Tone → suma U5B
    TremoloOsc.h                oscylator z przesuwnikiem fazy (Q4 LND150) → Depth VR8 → C48
    PhaseInverterAC30.h         V3 LTP (R55 1k2 / R60 47k, anody 100k/100k, C42 47p) + sieć wyjść:
                                C50/C51, Tone Cut (VR9 + C80), Master różnicowy (VR10 + R105/R112/R115/R118 + 1M)
    PowerAmpEL84.h              4×EL84 PP, stoppery 3k3, ekrany 470R, wspólna katoda R119 ∥ C74, OT
    PowerSupplyAC30.h           prostownik krzemowy + drabinka RC B+1…B+5 (sag całego wzmacniacza)
    Ac30Engine.*                ProcessorChain + 3× Oversampling + przełączanie
tests/                          + Ac30Tests.cpp, Ac30SpiceReference.h
spice/                          + gen_ac30_refs.py, fit_el84.py, koren_ac30.inc, ac30_trem_osc.cir
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
  │  PreChain (fs)        dsp::Gain (Input) → dsp::IIR HPF 20 Hz
  ▼
  Oversampling::processSamplesUp (2x / 4x / 8x)
  │  AmpChain (fs·N)
  │   PreampAC30                                                           (B+5)
  │     Normal Hi/Lo ─► 2×56k ─► V1 (Normal) anoda 100k ─► C7 47n ─► R13 330k∥C8 120p ─► VR1 Normal ─► R37 470k ─┐
  │                              ║ wspólna katoda R16 1k5 ∥ C11 22µ                                                │
  │     TB Hi/Lo ─────► 2×56k ─► V1 (TB) anoda 220k ─► C9 470p ─► VR2 TB Vol (+C15 120p) ─┐                      │
  │                                                                             (B+4)     ▼                      │
  │                   V2a (100k / 1k5∥22µ) ═► V2b CF (56k) ─► stos C23/R19/C28/C38/VR3/VR4/R47 ─► C25 ─► 330k/120k │
  │                                                                                                 ▼ (+)       ▼ (−)
  │                                                                       U1B NJM2147, R40 820k: TB×2.74, Normal×(−1.74)
  │   ReverbFx      (pętla FX: bypass)
  │                 dry ───────────────────────────────────────────────────────────── R85 56k ─┐
  │                 U2B driver ─► 6× bufor ∥ 47R ─► zbiornik ─► U5A recovery ─► VR6 Level / VR5 Tone ─► R84 56k ─┴─► U5B ×4.9
  │   PhaseInverterAC30                                                    (B+3)
  │                 C45 100n ─► V3 LTP (R55 1k2 + ogon R60 47k, anody 100k/100k, C42 47p) ◄─ C48 ◄─ VR8 Depth ◄─ TremoloOsc (VR7 Speed)
  │                 ─► C50/C51 100n ─► Tone Cut (VR9 220k + C80 4.7n między fazami)
  │                 ─► R105/R112 10k ─► Master VR10 (różnicowo) ─► upływy 1M ─► OP+ / OP−
  │   PowerAmpEL84  stoppery 3k3 ─► 4×EL84 (2 pary ∥), ekrany 470R, wspólna katoda R119 50Ω ∥ C74 220µ
  │                 ─► OT (Raa ≈ 4k : 16 Ω), bez NFB ─► 2 × 8 Ω szeregowo
  │                 ⇅ PowerSupplyAC30: B+1 (47µ) ─ 1k ─ B+2 (100µ) ─ 22k ─ B+3 ─ 10k ─ B+4 ─ 22k ─ B+5
  ▼
  Oversampling::processSamplesDown
  │  PostChain (fs)       DCBlocker → CabinetIR (2×12 Greenback / IR użytkownika) → dsp::Gain (Output)
  ▼
  sanity clamp (NaN/Inf → 0, |x| ≤ 4) → [out mono; szyna stereo: L = R]
```

Łańcuchy jak w `source/dsp/AmpEngine.h`:

```cpp
using PreChain  = juce::dsp::ProcessorChain<juce::dsp::Gain<float>, juce::dsp::IIR::Filter<float>>;
using AmpChain  = juce::dsp::ProcessorChain<PreampAC30, ReverbFx, PowerSectionAC30>;
using PostChain = juce::dsp::ProcessorChain<DCBlocker, CabinetIR, juce::dsp::Gain<float>>;
```

`PowerSectionAC30` łączy `TremoloOsc` + `PhaseInverterAC30` + `PowerAmpEL84` + `PowerSupplyAC30` w jeden element
łańcucha: tremolo moduluje siatkę PI, Cut i Master obciążają wyjścia PI, a prąd końcówki (przez drabinkę B+)
zmienia zasilanie PI i przedwzmacniacza — wszystko w tej samej próbce. Napięcia B+3…B+5 są przekazywane do
`PreampAC30` z opóźnieniem jednej próbki (stałe czasowe drabinki ≫ okres próbkowania).
Każdy procesor spełnia kontrakt `prepare(const ProcessSpec&)`, `process(const Context&) noexcept`, `reset()`
i respektuje `context.isBypassed`.

---

## 3. Stopnie i wartości elementów

### 3.1 Wejścia i V1 (12AX7, zasilanie B+5)

| Element | Wartość | Źródło |
|---|---|---|
| Płytka gniazd (każdy kanał) | Hi / Lo: 10k (R1/R2, R5/R6) + ferryt (R3, R4), 22 pF do masy (C1–C6) | [C2] |
| Sieć siatki | 2 × 56k na siatkę (TB: R7, R8; Normal: R18, R20), upływ 1M (R9, R17); Lo ≈ −6 dB | [C2] |
| C13 | 120 pF (filtr RF siatki) | [C2] |
| Anoda V1 — TB | **R14 220k** | [C2] |
| Anoda V1 — Normal | **R12 100k** | [C2] |
| Wspólna katoda | **R16 1k5 ∥ C11 22 µF** (obie triody) | [C2], zgodne z [AB] |
| Punkt pracy | Vgk ≈ −1.6 V, wzm. bez obciążenia ≈ 37.8 dB (dla 220k / 275 V) | [AB] |

**Wspólna katoda** sprzęga kanały: sygnał jednego kanału moduluje katodę drugiej triody (przesłuch, interakcja
przy graniu na obu kanałach). V1 rozwiązujemy **jednym** układem nieliniowym (§4).

### 3.2 Kanał Normal

| Element | Wartość | Źródło |
|---|---|---|
| Sprzęgający | C7 47 nF | [C2] |
| Szeregowo do potencjometru | R13 330k ∥ C8 120 pF (podbicie wysokich, spadek poziomu niskich) | [C2] |
| Volume | VR1 A500K → R37 470k → „−” U1B | [C2] |

### 3.3 Kanał Top Boost (V1 TB + V2, zasilanie B+4)

| Element | Wartość | Źródło |
|---|---|---|
| Sprzęgający V1 → Volume | **C9 470 pF** — mocne cięcie niskich, f ≈ 600 Hz przy Volume max | [C2], [est] |
| TB Volume | VR2 A500K, bright C15 120 pF, C82 10 pF | [C2] |
| V2a (gain) | anoda R32 100k, katoda R26 1k5 ∥ C18 22 µF | [C2] |
| V2b | **wtórnik katodowy sprzężony DC** (siatka na anodzie V2a), katoda R21 56k 1 W, C84 1 nF | [C2] |
| Stos TB | C23 56 pF, R19 100k, C28 22 nF, C38 22 nF, Treble VR3 A1M, Bass VR4 A1M, R47 10k | [C2] |
| Wyjście stosu | C25 220 nF → R34 330k / R35 120k (×0.27) → „+” U1B | [C2] |

Mały kondensator C9 (470 pF, jak 500 pF w vintage TB) to główne źródło „chime” Top Boosta: kanał TB dostaje
głównie środek i górę, a stos Treble/Bass (Bassman bez Middle) przywraca bas dopiero za V2. Wtórnik DC ma własną
nieliniowość (przewodzenie siatki V2b, asymetryczne obcięcie V2a/V2b).

### 3.4 Mikser U1B

| Element | Wartość | Źródło |
|---|---|---|
| Op-amp | U1B NJM2147, ±27 V | [C2] |
| Wejście „+” | TB z dzielnika R34/R35 → wzmocnienie 1 + 820k/470k = **×2.74** | [C2], [est] |
| Wejście „−” | Normal przez R37 470k → wzmocnienie −820k/470k = **×−1.74** | [C2], [est] |
| Wyjście | R_FX_SND → płytka Rev/FX | [C2] |

### 3.5 Pogłos i pętla FX (płytka Rev/FX)

| Blok | Elementy | Źródło |
|---|---|---|
| Pętla FX | send przez dzielnik −10 dB (R41 470R, R38 4k7, R36 1k8); return U1A ×3.8 (R30 5k1 / R31 1k8), Zenery 27 V; bypass SW1 | [C2] |
| Driver | U2B: wejście C37 560 pF, R42 47k, R44 1M; sprzężenie R48 330k / R45 5k6 (R46 47R) → wzm. ≈ ×60 | [C2], [est] |
| Stopień mocy | 5 buforów (U2A, U3A, U3B, U4A, U4B) + U2B równolegle, każdy przez **47R** → wejście zbiornika (J23) | [C2] |
| Zbiornik | kod Accutronics/Belton: wejście **E = 600 Ω**, wyjście **B = 2250 Ω**, montaż **B** = poziomo, otwartą stroną w dół. Arkusz Rev/FX C2: **9EB2C1B** (decay 2 = średni, ≈ 1.75–3 s); lista części CC2: **BL3EB3C1B** (3 sprężyny, decay 3 = długi, ≈ 2.75–4 s). Niska impedancja wejścia pasuje do sterowania z op-ampów (6 × 47R ∥ ≈ 7.8 Ω) | [C2], [CC2], [AUT] |
| Recovery | U5A: R75 220k, R76 8k2, R77 100k ∥ C55 330 pF, C56 56 nF → pasmo ≈ 350 Hz – 4.8 kHz, wzm. ≈ ×13 | [C2], [est] |
| Level / Tone | R78 33k → VR6 B100K (Level); VR5 A500K z C47 47 pF / C49 10 nF (Tone) | [C2] |
| Sumowanie | dry R85 56k + wet R84 56k → U5B (R80 3k9 / R82 1k → ×4.9, R83 22k, C85 10 pF) → R_FX_RET | [C2] |
| Wyciszenie | Q1 J174 (JFET) zwiera powrót — footswitch Reverb | [C2] |

### 3.6 Odwracacz fazy, Tone Cut, Master (V3, zasilanie B+3)

| Element | Wartość | Źródło |
|---|---|---|
| Wejście | R_FX_RET → C45 100 nF → siatka A; upływy R57 / R63 1M do węzła katoda/ogon | [C2] |
| Katoda / ogon | **R55 1k2** (polaryzacja) + **R60 47k** (ogon) | [C2] |
| Anody | R67 / R70 **100k / 100k**, **C42 47 pF między anodami** (ograniczenie pasma) | [C2] |
| Sprzęgające | C50 / C51 100 nF | [C2] |
| **Tone Cut** | VR9 B220K szeregowo z **C80 4.7 nF między fazami** (za C50/C51) | [C2] |
| **Master** | R105 / R112 10k szeregowo, **VR10 A500K między fazami**, R115 / R118 220k, R113 / R114 / R116 1M (upływy siatek EL84) → OP+ / OP− | [C2] |
| Tremolo | C48 100 nF z suwaka VR8 (Depth) na **siatkę B** odwracacza fazy | [C2] |

**Mechanizm Cut:** kondensator między dwoma węzłami w przeciwfazie widzi podwójne napięcie, więc dla każdej
strony działa jak bocznik `2C` do wirtualnej masy przez `R_cut/2`. Przy impedancji źródła strony `Rs`
(anoda PI ‖ dalsza sieć):

```
f_c  ≈ 1 / (2π · (Rs + R_cut/2) · 2C)
półka HF ≈ (R_cut/2) / (Rs + R_cut/2)
```

**Master różnicowy:** VR10 między fazami tłumi sygnał różnicowy (ten, który wysterowuje końcówkę push-pull),
a składowa wspólna (np. część modulacji tremolo i niezrównoważenie PI) przechodzi inaczej — dlatego Cut, Master
i upływy siatek są w modelu **jedną** siecią liniową obciążającą PI, a nie osobnymi filtrami.

### 3.7 Tremolo (oscylator na Q4)

| Element | Wartość | Źródło |
|---|---|---|
| Element aktywny | Q4 LND150N3 (MOSFET z kanałem zubożanym, wysokie napięcie), zasilanie z B+2 przez R93 33k | [C2] |
| Przesuwnik fazy | C61 100 nF, C63 22 nF, C64 10 nF, C62 10 nF, R89 3M3, R87 1M, R91 100k | [C2] |
| Polaryzacja | R90 470R ∥ C66 100 µF, R92 10k; C60 10 µF | [C2] |
| Speed | VR7 C2.2M + R86 27k | [C2] |
| Depth | R94 510k, C67 22 nF → VR8 B500K → C48 100 nF → siatka B PI | [C2] |
| Włączanie | Q2 2SC2910 (TREM_ON, sterowany z footswitcha przez R96 10k / R98 3k3) | [C2] |

Tremolo C2 działa przez **modulację punktu pracy odwracacza fazy** — zmienia wzmocnienie LTP i symetrię, więc
obok modulacji amplitudy daje lekką modulację barwy i zniekształceń (inaczej niż tremolo optyczne).

### 3.8 Końcówka mocy (V4–V7, EL84)

| Element | Wartość | Źródło |
|---|---|---|
| Stoppery siatek | **3k3** (R61, R81, R101, R108) | [C2] |
| Rezystory ekranowe | **470R ½ W** na lampę (R79, R95, R104, R117) | [C2] |
| Katoda | **wspólna R119 50 Ω 10 W ∥ C74 220 µF / 50 V** | [C2] |
| Spoczynek | ≈ 10 V na Rk → ≈ 0.2 A łącznie, ≈ 50 mA / lampę; ≈ 12.5 V przy pełnej mocy | [AIK], [FOR] |
| NFB | **brak** | [C2] |
| OT | Raa **4 kΩ** (P/N 550022-1000042212, bez parametrów w liście części), wtórne 8 / 16 Ω (przełącznik) | [C2], [CC2], [AUT] |
| Głośniki | 2 × 8 Ω **szeregowo** = 16 Ω | [C2] |

### 3.9 Zasilanie

| Węzeł | Elementy | Zasila | Źródło |
|---|---|---|---|
| Uzwojenie WN | 2 × ~262 VAC z odczepem, prostownik pełnookresowy (2 × 1N4007 na gałąź, 1 nF/1 kV), R107 / R109 22R 5 W, Standby | | [C2] |
| B+1 | C72 47 µF / 450 V (+ C71 10 nF) — ≈ 360 V bez obciążenia **[est]** | odczep OT (anody EL84) | [C2], [AUT] |
| B+2 | R120 1k 10 W → C68 100 µF (+ C65 10 nF) | ekrany EL84 (przez 470R na lampę), oscylator tremolo | [C2], [AUT] |
| B+3 | R74 22k 1 W → C58 10 µF (+ C59 10 nF) | V3 (odwracacz fazy) | [C2], [AUT] |
| B+4 | R22 10k → C20 10 µF (+ C16 10 nF) | V2 (Top Boost) | [C2], [AUT] |
| B+5 | R15 22k → C10 10 µF (+ C14 10 nF) | V1 (wejście) | [C2], [AUT] |
| ±27 V | ~25 VAC, D12/D13, R106/R103 220R, Zenery D11/D6 27 V, C70/C73/C77/C81 1000 µF | op-ampy | [C2] |

Prostownik krzemowy oznacza brak sagu lampy GZ34 (która była jeszcze w CC2 **[CC2]**); pozostaje sag z rezystancji
uzwojeń, R107/R109 22R i R120 1k. Drabinka RC przenosi zmiany prądu końcówki do przedwzmacniacza (dynamika, lekkie
„oddychanie” przy mocnym przesterze).

---

## 4. Modele (circuit modeling)

| Blok | Metoda | Reuse |
|---|---|---|
| **Triody 12AX7** | Koren `Ip(Vgk + VCT, Vpk)` (`k12AX7`), prąd siatki miękkim kolanem 0.36 V przez RGI; w czasie rzeczywistym `KorenTable` (Hermite 2D) | `TriodeModel.h`, `KorenTable.h` |
| **V1 — dwie triody, wspólna katoda** | węzły `Vp_TB`, `Vp_N`, `Vk` w `double`; Newton **3×3** z analitycznym Jakobianem, predyktor liniowy, limit 4 iteracji; anody ładowane afinicznie przez swoje sieci (`i = αVp + β`): TB — C9 + VR2 + Miller V2a, Normal — C7 + R13∥C8 + VR1 + R37 | rozszerzenie `TriodeStage.h` → `InputStageAC30` |
| **V2a → V2b (DC)** | V2a jako `TriodeStage` (100k, 1k5∥22µ); V2b jako `CathodeFollower` (Rk 56k, C84) z siatką = `Vp(V2a)` w tej samej iteracji — przewodzenie siatki V2b przez impedancję anody V2a | `TriodeStage.h`, `CathodeFollower.h` |
| **Stos TB** | `LinearNetwork` (C23, R19, C28, C38, VR3, VR4, R47, C25, R34, R35) zasilany z katody V2b, obciążony wejściem „+” U1B | `LinearNetwork.h`, `Pots.h` |
| **Op-ampy NJM2147** | `OpAmp.h`: idealny wzmacniacz w sieci RC (wzmocnienie i bieguny z elementów [C2]) + miękkie nasycenie przy ±(27 V − 1.5 V) + ograniczenie slew rate; mikser U1B, U1A, U2B, U5A, U5B | nowy |
| **Pogłos** | driver U2B ×60 z HPF C37 → bufor (6 × 47R ∥ = 7.8 Ω) → cewka wejściowa zbiornika 600 Ω (indukcyjna: prąd napędu opada z częstotliwością, model RL) → `SpringTank` (3 sprężyny, dyspersyjne allpassy, zdecymowany; czas wybrzmiewania jako parametr konstrukcyjny „średni/długi”) → przetwornik 2250 Ω obciążony R75 220k → U5A (pasmo 350 Hz – 4.8 kHz) → VR6/VR5 (sieć liniowa) → suma z dry w U5B. Tor powrotu liczony w zdecymowanej częstotliwości (pasmo < 5 kHz), jak w SSS. Wyciszenie Q1 = rampa | `SpringTank.h`, `OnePole.h` |
| **Tremolo** | oscylator Q4 zasymulowany offline w ngspice (`spice/ac30_trem_osc.cir`) dla siatki położeń VR7 → tablice: częstotliwość, amplituda i kształt fali (1 okres, 256 punktów) w funkcji Speed. W czasie rzeczywistym: akumulator fazy + interpolacja kształtu, amplituda przez VR8/R94/C67 → C48 jako źródło napięcia w sieci wejściowej PI (siatka B). Włącz/wyłącz (Q2) z rampą | nowy `TremoloOsc.h` |
| **Odwracacz fazy** | LTP (R55 1k2 + R60 47k, anody 100k/100k z B+3) — nieliniowy obwód rozwiązywany Newtonem 3×3 (dwie anody + węzeł katody) zamiast samej LUT, bo siatka B nie jest stała (tremolo); C42 47 pF i całe obciążenie wyjść (C50/C51, Cut, Master, upływy, stoppery 3k3, prąd siatek EL84) jako jedna `LinearNetwork` z dwoma wejściami | wzorzec `PhaseInverter.h`, `TriodeStage.h` |
| **Siatki EL84** | przewodzenie siatek (RGI) przez stoppery 3k3 ładuje C50/C51 → **blocking distortion** i przesunięcie biasu siatek; prąd siatki wstrzykiwany do sieci wyjść PI | `gridCurrent()` z `TriodeModel.h` |
| **EL84** | Koren pentoda; parametry dopasowane do krzywych Philips/Mullard skryptem `spice/fit_el84.py`; tablice 1D jak dla 6L6GC | `PentodeTable.h` + `forTubeEL84()` |
| **Końcówka 4×EL84** | dwie „lampy zastępcze” (pary równoległe: prąd ×2, stopper 3k3/2), anody rozwiązywane łącznie przez uzwojenie z odczepem (jak `PowerAmp6L6`), ekrany przez 470R/2 z B+2, **plus węzeł katody** `C74·dVk/dt = Σ(Ia + Ig2) − Vk/R119` — wspólny Newton (2 anody + katoda) → dynamiczne przesunięcie biasu (kompresja, „bloom”) | `PowerAmp6L6.h` → `PowerAmpEL84` |
| **OT + głośniki** | idealny transformator Raa ≈ 4k : 16 Ω + HPF indukcyjności pierwotnej, LPF rozproszenia, łagodne nasycenie rdzenia; obciążenie: model impedancji 2 × 8 Ω szeregowo (rezonans ~75 Hz, wzrost indukcyjny) — bez NFB impedancja głośnika kształtuje pasmo | wzorzec `PowerAmp6L6.h` |
| **Zasilanie** | `PowerSupplyAC30`: B+1 z prostownika (źródło napięcia szczytowego + R107/R109 + rezystancja uzwojeń, ładowanie C72 w szczytach sieci uśrednione), drabinka RC B+1…B+5 krokowana na fs (stałe czasowe ≥ 0.2 s); prąd obciążenia z końcówki (anody, ekrany) i z PI | nowy |
| **Kolumna** | `juce::dsp::Convolution`: wbudowany proceduralny IR 2×12 Greenback lub plik WAV/AIFF; kompensacja normalizacji JUCE (×8·√(48 kHz/fs)) | `CabinetIR.h` |

Wszystkie wartości: `ac30/dsp/Ac30Constants.h` (struktury `TriodeStageValues` jak w `CircuitConstants.h`,
oznaczenie elementu [C2] przy każdej liczbie).

---

## 5. Oversampling 2x / 4x / 8x

Bez zmian względem SSS (`source/dsp/AmpEngine.cpp`):

* trzy `juce::dsp::Oversampling<float>` (1 kanał, `filterHalfBandPolyphaseIIR`, max quality, integer latency)
  i trzy przygotowane `AmpChain` tworzone w `prepare()`,
* przełączanie (parametr nieautomatyzowalny): blok N — rampa w dół starej ścieżki, blok N+1 — `reset()` nowej
  ścieżki i rampa w górę; zero alokacji,
* latencja liczona w `prepare()`, `setLatencySamples()` + `updateHostDisplay()` tylko z `juce::Timer`,
* domyślnie 4x. Brak NFB usuwa ograniczenie stabilności pętli, które w SSS wymuszało pod-kroki ≥ 176.4 kHz;
  sprzężenia zwrotne w AC30C2 to tylko węzły katod (V1, V3, EL84) — rozwiązywane jawnie w Newtonie — oraz drabinka
  zasilania (wolna, opóźnienie 1 próbki).

---

## 6. Rygor real-time w `processBlock()`

Identyczne zasady jak w `docs/ARCHITECTURE.md` §5:

* `juce::ScopedNoDenormals`; brak `new/delete`, locków, `String`, logów, I/O, `MessageManager`,
* `ParamRefs` — wskaźniki `std::atomic<float>*` pobrane raz w konstruktorze, w bloku tylko `load(relaxed)`,
* bufory, sieci i tablice (Koren 12AX7, pentoda EL84, tablice oscylatora tremolo) przygotowane w `prepareToPlay`;
  za długie bloki dzielone na pod-bloki,
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

| ID | Typ | Zakres / wartości | Element [C2] / uwagi |
|---|---|---|---|
| `inputGain` | float | −24…+24 dB | kalibracja poziomu interfejsu |
| `input` | choice | Normal Hi / Normal Lo / TB Hi / TB Lo / Oba (Hi) | gniazdo, do którego „wpięta” jest gitara; „Oba” = kabel łączący kanały (Normal w przeciwfazie, §3.4) |
| `normalVolume` | float | 0–10 | VR1 A500K (`potAudio`) |
| `tbVolume` | float | 0–10 | VR2 A500K + C15 |
| `treble` | float | 0–10 | VR3 A1M |
| `bass` | float | 0–10 | VR4 A1M |
| `reverbTone` | float | 0–10 | VR5 A500K |
| `reverbLevel` | float | 0–10 | VR6 B100K (`potLinear`), domyślnie 0 |
| `reverbOn` | bool | | Q1 (footswitch) |
| `tremSpeed` | float | 0–10 | VR7 C2.2M (krzywa „C” — odwrotna logarytmiczna) |
| `tremDepth` | float | 0–10 | VR8 B500K, domyślnie 0 |
| `tremOn` | bool | | Q2 (footswitch) |
| `toneCut` | float | 0–10 | VR9 B220K; 0 = brak cięcia (jak gałka na panelu) |
| `master` | float | 0–10 | VR10 A500K |
| `output` | float | −24…+12 dB | |
| `cabOn` | bool | | |
| `oversampling` | choice | 2x / 4x / 8x | nieautomatyzowalny |

Krzywe potencjometrów z `Pots.h` (`potLinear`, `potAudio`) + nowa `potReverseAudio` dla VR7 (krzywa C).

---

## 9. GUI

Panel w kolejności oryginału (od lewej): **Normal** Volume │ **Top Boost** Volume, Treble, Bass │
**Reverb** Tone, Level │ **Tremolo** Speed, Depth (+ dioda fazy LFO) │ **Master** Tone Cut, Master Volume.
Pasek dolny: wybór wejścia, Reverb / Tremolo on (footswitch), Cabinet + *Load IR…*, Oversampling, Input/Output,
mierniki (`LevelMeter`), obciążenie CPU. `Ac30LookAndFeel` (pochodna `AmpLookAndFeel`): miedziany panel,
gałki „chicken head”, tło w stylu diamond grill; skalowanie wektorowe (Retina).

---

## 10. Weryfikacja

**Referencje SPICE** — `spice/gen_ac30_refs.py` (wzorem `gen_sss002_refs.py`) buduje z wartości [C2] netlistę ngspice:
V1 ze wspólną katodą, Normal + TB (V2a + CF + stos), U1B (makromodel op-ampu), pogłos jako tor dry, PI z C42,
Cut, Master, siatki EL84; modele Korena w `spice/koren_ac30.inc`; `.op` / `.tran` + `.fourier` → `tests/Ac30SpiceReference.h`.
Osobno `spice/ac30_trem_osc.cir` — oscylator tremolo (częstotliwość i kształt w funkcji VR7).

| Test | Kryterium |
|---|---|
| Punkty pracy V1/V2/V3 vs `.op` (przy B+3…B+5 z modelu zasilania) | ≤ 1 % |
| H1 / THD przedwzmacniacza vs ngspice (siatka gałek × 100 Hz…6 kHz, 0.01–0.5 V) | H1 ≤ 0.5 dB, THD ≤ 25 % względnie (przy THD > 0.5 %) |
| Kanał TB: wpływ C9 (cięcie niskich) i stos Treble/Bass vs ngspice | ≤ 0.2 dB |
| Mikser U1B: wzmocnienia ×2.74 / ×−1.74, przeciwfaza kanałów | ≤ 0.1 dB, faza 180° ± 1° |
| Recovery pogłosu: pasmo 350 Hz – 4.8 kHz | ≤ 0.5 dB |
| Tone Cut i Master (różnicowo) vs ngspice dla 0/5/10 | ≤ 0.3 dB |
| Tremolo: częstotliwość i głębokość vs `ac30_trem_osc.cir` | ≤ 5 % |
| Bias EL84 | ≈ 10 V na R119, ≈ 50 mA/lampę; przy pełnej mocy ≈ 12.5 V **[AIK]** |
| Bias shift / blocking (impuls 0.5 s) | wzrost Vk i czas powrotu zgodne z ngspice |
| Moc wyjściowa | 30–40 W na 16 Ω przy przesterze |
| Aliasing (ton 4 kHz, przester) | 2x ≤ −45 dB, 8x ≤ −65 dB |
| Alokacje na ścieżce audio | 0 |
| CPU (bufor 64 @ 48 kHz, Apple Silicon) | 4x ≤ 10 % rdzenia |

CI: `.github/workflows/ci.yml` — Linux (testy DSP obu wzmacniaczy, compile check Standalone),
macOS (build Xcode, testy, `auval -v aufx Ac30 Kgon`, pluginval).

---

## 11. Etapy implementacji

1. **Refaktor `ampcore`** — przeniesienie klas z `source/dsp/` do biblioteki; testy SSS bez zmian wyników.
2. **Netlisty [C2]** — `gen_ac30_refs.py`, `ac30_trem_osc.cir`, `fit_el84.py`; przepisanie wartości do `Ac30Constants.h`.
3. **Przedwzmacniacz** — `InputStageAC30` (Newton 3×3), Normal, Top Boost (V2a + CF + stos), `OpAmp` + mikser U1B.
4. **Końcówka** — `PentodeTable::forTubeEL84()`, PI (Newton 3×3) + sieć Cut/Master, `PowerAmpEL84` z węzłem katody,
   `PowerSupplyAC30`, OT; testy biasu i bias shift.
5. **Efekty** — `ReverbFx` (+ `SpringTank`), `TremoloOsc` (tablice z SPICE).
6. **Plugin** — `Parameters`, `PluginProcessor`, `PluginEditor`, `StandaloneApp`, IR 2×12 Greenback.
7. **Walidacja i wydanie** — auval, pluginval, CPU, podpis + notaryzacja, DMG.
8. **Kalibracja** — wartości **[est]** (napięcia B+, wzmocnienia op-ampów, czas wybrzmiewania zbiornika) porównane
   z pomiarem egzemplarza C2; korekta `Ac30Constants.h`.

---

## 12. Źródła

* **[C2]** VOX R&D UK, *AC30C2 PreAmp*, *AC30C2 Power Amp*, *AC30C2 Rev/FX* — schematy, 15.12.2009, rewizja ISS3a
  (skany dostarczone przez autora projektu, poza repozytorium)
* **[CC2]** KORG, *Service Manual AC30CC2 / AC30CC2X*, wyd. 1, 21.10.2005 (schemat blokowy, preamp, listy części)
* **[AUT]** ustalenia autora projektu (Raa OT, zasilanie anod/ekranów, drabinka B+, specyfikacja zbiornika)
* Accutronics/Belton — system oznaczeń zbiorników pogłosu (typ, impedancja wejścia/wyjścia, decay, montaż)
* ampbooks.com — *Circuit Analysis of the Vox AC30*: https://www.ampbooks.com/mobile/classic-circuits/vox-ac30/
* ampbooks.com — *Bright Boost Capacitor Calculator*: https://www.ampbooks.com/mobile/amplifier-calculators/bright-boost/
* ampbooks.com — *Digital Modeling of a Guitar Amplifier Tone Stack*: https://www.ampbooks.com/mobile/dsp/tonestack/
* Vox Showroom — AC30C2 „Under the Hood”: https://www.voxshowroom.com/uk/amp/ac30c2_hood.html
* Vox Showroom — Top Boost „Under the Hood”: https://voxshowroom.com/uk/amp/ac30_tb_hood.html
* voxac30.org.uk — transformatory: https://www.voxac30.org.uk/vox_ac30_transformers.html
* voxac30.org.uk — Mullard EL84: https://www.voxac30.org.uk/vox_ac30_mullard_el84.html
* Aiken Amplification — *Is the Vox AC-30 Really Class A?*: https://www.aikenamps.com/is-the-vox-ac-30-really-class-a
* The Amp Garage — *What's happening with a Vox cut control*: https://ampgarage.com/forum/viewtopic.php?t=35508
* Music Electronics Forum — *Biasing an AC30*: https://music-electronics-forum.com/forum/amplification/guitar-amps/maintenance-troubleshooting-repair/4519-biasing-an-ac30
* N. Koren, *Improved vacuum tube models for SPICE simulations* (modele triod/pentod)
