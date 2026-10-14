# Task 108: uniwersalna „Animacja drgań” (mod defektu, mod bulku w q, kierunek ΔQ)

Status: zatwierdzony kierunek (2026-10-14, decyzje użytkownika niżej), konsultacja codex read-only. Kolejność ustalona przez użytkownika 2026-10-13:
106d → 107 → 105c → **108** (pamięć: phonon-datasets-decision).

## Co jest

- Animator w `PhononPanelSession` (`StartAnimation`/`AdvanceAnimation`/`SetPhase`/`StopAnimation`, pozycje
  spoczynkowe, strzałki zgodne z fazą) - tylko mod defektu, okno superkomórki defektu
  (`PhononPanelSessionWindow.cpp:53`), korekta masowa w `Domain/Vibrations/PhononModes.cpp:13`.
- Dyspersja (`scripts/python/examples/phonon_dispersion.py`) zapisuje tylko segment, odległość i częstości -
  **bez wektorów własnych i bez współrzędnych q**; wykres nie ma wyboru modu.
- CCD zwraca tylko skalar ΔQ = sqrt(Σ m_i |ΔR_i|²) po minimum image i usunięciu ruchu środka masy
  (`ccd_analysis.py:53`); wektor ΔR nie jest zapisywany.

## Projekt (codex + Claude)

Jeden animator, różne źródła: mała wartość domenowa „przygotowane pole przemieszczeń” (per atom zespolony
wektor w Å, pozycje równowagi, etykieta, normalizacja) + czysta matematyka klatki i strzałek; adaptery w
Presentation: mod defektu (dzisiejsze zachowanie bez zmian), mod bulku w q, ΔR z CCD. Bez hierarchii klas.

UI: sekcja „Animacja drgań” w oknie `Fonony: mody`: źródło (mod defektu / mod bulku w q / kierunek ΔQ),
zestaw lub przypadek, mod albo q+gałąź, okno docelowe / „Otwórz”, ▶/⏸/⏹, amplituda [Å], okres [s], faza,
strzałki. `Fonony: wykresy` → Dyspersja: klik na krzywej wybiera q i gałąź, pionowa linia przeciągana po
segmencie ścieżki (bez przeskoku przez przerwy), liczy po puszczeniu, nowsze żądanie wygrywa.

## Wycinki

| Wycinek | Zakres | Python |
|---|---|---|
| 108a | pole przemieszczeń + uniwersalne sterowanie, na razie tylko mod defektu; testy kwadratury zespolonej, Stop/zapis/zmiana przypadku/zamknięcie okna przywracają pozycje | nie |
| 108b | ΔQ: `ccd_analysis.py` zapisuje uporządkowane ΔR i geometrię GS; stare zbiory bez wektora → jasny powód „niedostępne” | rozszerzenie istniejącego |
| 108c | nowy job `phonon_mode.py` (FC bulku jak w dyspersji, `run_qpoints(with_eigenvectors)` + modulacja), `vibration.json` + POSCAR; dyspersja zapisuje q numerycznie | nowy |
| 108d | klik/przeciąganie w dyspersji, okno bulku, przełączanie źródeł zachowuje kamerę | nie |

## Pułapki fizyczne

- Konwencja fazy phonopy: e_κ · exp(i q·(R + τ_κ)), q zredukowane × 2π; nie mieszać z konwencją tylko-R.
- Dzielenie przez √m dokładnie raz (modulacja phonopy już to robi).
- Mody zdegenerowane: dowolna baza - suwak kąta mieszania (decyzja 3), etykieta „zdegenerowany”.
- Domknięcie okresowe wymaga q·T ∈ ℤ dla translacji superkomórki.
- ΔQ [amu^½ Å] to nie przemieszczenie: animujemy ΔR w Å; etykieta „kierunek ΔQ (nieharmoniczny)”.

## Decyzje użytkownika (2026-10-14)

1. Niewspółmierne q: **fragment fali z ostrzeżeniem + podpowiedź** najmniejszej superkomórki N×M×K, w której
   ruch domyka się okresowo (przycisk „Użyj”).
2. ΔQ: **oba tryby z przełącznikiem** - przejście GS ↔ ES (Q od 0 do ΔQ, suwak Q) oraz drganie ± wokół GS
   wzdłuż ΔR.
3. Mody zdegenerowane: **od razu suwak kąta mieszania** cos θ · e1 + sin θ · e2 (dla par zdegenerowanych,
   także modów defektu, np. E w C3v) - wchodzi do 108a.
