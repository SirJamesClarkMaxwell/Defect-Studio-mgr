# Ryciny (matplotlib), personalizacja (ImPlot + matplotlib) i jeden eksport TSV pod Origin

> 2026-10-20. **Status: propozycja do decyzji, nic nie zaimplementowane.** Rozwija
> `2026-10-11-figure-module.md` po zadaniach 110 (PlotData + TSV) i 111 (FigureSpec v1 + `render_figure.py`).
> Prośba: moduł możliwie zbliżony do Origina funkcjami, ale bez przesady, czyli tylko wykresy, których
> używamy w projektach. Personalizacja ważna w ImPlot i matplotlib. Jeden spójny system TSV pod Origin,
> chyba że matplotlib dojdzie do poziomu, przy którym Origin nie będzie potrzebny. Dwie rundy z codex
> (read-only) + własne pomiary.

## 1. Stan dziś

**Wspólny łańcuch** istnieje: `PlotData` → `PlotSeries` (ImPlot), `PlotDataColumns` → `DataTableIO`
(TSV/CSV/schowek, opcjonalne trzy wiersze Long Name/Units/Comments, kropka dziesiętna, NaN = pusta komórka)
oraz `FigureSpec` v1 → `render_figure.py`:
- serie Line/Stems/Scatter/VerticalLine/Segments, druga oś Y, log;
- MathText, rozmiar w cm, PDF/SVG/PNG, `figures/thesis.mplstyle`, `--all`;
- zapis atomowy, a błąd nie niszczy poprzedniego pliku.

Personalizacja w ImPlot: popup legendy (`SeriesLegendPopup`) zmienia kolor, grubość, styl linii,
wypełnienie i widoczność. Style zapisują się trwale **tylko dla fononów**: `PhononPanelSettingsIO.hpp:89`,
globalnie dla panelu, nie per przypadek. Etykiety osi to „TeX-lite”.

**Pokrycie** (17 wywołań `BeginPlot`, około 19 widoków):

| Wykres | PlotData + wspólny TSV | Rycina |
|---|---|---|
| β / S_k + S(ħω) / PL, CCD, CCD-PL 1D, dyspersja | tak | tak (CCD bez strzałek i klamer, dyspersja bez ticków punktów symetrii) |
| ΔQ²(r) lokalizacja | tak | tak |
| E_f(E_F), poziomy przejść (CTL) | nie; własny eksport `states/transitions/mu_sets/formation_*.tsv` (`DefectEnergeticsModel.cpp:294–448`) | nie |
| Koncentracje i E_F w funkcji T oraz λ, mapa koncentracji | nie; własny `concentrations.tsv` | nie |
| Profil FNV | nie | nie |
| Diagram obsadzeń (KS) | nie; stary synchroniczny eksport PNG (`electronic_structure_plot.py`, `ElectronicStructureSession.cpp:679`) | stary PNG |
| Ewolucja poziomów KS i zbieżność (Calculation Summary) | nie | nie |
| Dystrybuanta gęstości | nie | nie |
| Orbitale (tabela) | własny TSV zgodny z puntukas (`#` nagłówek), **zostaje** | – |

`PhononPlotData.cpp` ma 499 linii, więc kod ogólny trzeba z niego wydzielić przed dalszą rozbudową.

**Origin u Ciebie:** OriginPro 2024 (`D:\Aplications\Origin\2024`) i własny szablon `Base-plot.otpu`.
Filtr „Last used.oif”:
- separator tab, 3 linie subheader z autodetekcją;
- skrypt po imporcie kopiuje `Temperature` do Long Name i Comments (pozostałość po pomiarach),
  więc nie można na nim polegać.

## 2. Pomiary

- Render matplotlib (Agg, 8,5 × 6,5 cm, MathText, legenda, PNG 150 dpi):

  | Przebieg | Pomiar | Codex |
  |---|---|---|
  | świeży proces, łącznie | 0,6–1,1 s | 0,51 s (mediana z 6) |
  | sam import | 0,35–0,74 s | 0,27–0,28 s |
  | pierwszy render | 0,08–0,16 s | ok. 0,09 s |
  | kolejne rendery w tym samym procesie | ok. 35 ms | 45–49 ms |

- Wniosek: podgląd przez subprocess z debounce 400–600 ms daje około 1 s od końca edycji i na start
  wystarczy. Ciepły worker (np. host sesji Pythona z planu `2026-10-20-python-session-and-scripts.md`)
  dopiero, jeśli to przeszkadza.

## 3. Ustalenia z codex (dwie rundy, zgoda co do kierunku)

**Lista wykresów (priorytet):**
1. E_f(E_F) + CTL.
2. Diagram obsadzeń.
3. S/PL.
4. CCD.
5. ΔQ²(r).
6. Profil FNV (okna plateau i residua tłumaczą poprawkę).
7. Koncentracje/E_F w funkcji T **i** λ (z krzywymi elektronów i dziur).
8. Druga transza: mapa koncentracji i dyspersja.

β i CCD-PL 1D zostają, bo już są w łańcuchu. Zbieżność, ewolucja KS i dystrybuanta gęstości dostają na
start tylko TSV.

**Funkcje „jak w Originie” według wartości dla tych wykresów:**

| Waga | Funkcje |
|---|---|
| Niezbędne | etykiety i jednostki, odniesienia energii, jawne ticki, zakresy, log, rozróżnialne linie i markery, edytowalna legenda, wymiar fizyczny, typografia, PDF |
| Niezbędne dla konkretnych wykresów | etykiety CTL/spin/kategorii, strzałki i klamry CCD, okna FNV, ticki punktów symetrii, orientacja/maska/colorbar mapy |
| Następne | nakładka eksperymentalnego PL, wspólne style, ponowne generowanie wszystkich rycin, proste panele a/b/c |
| Warunkowo | jedna wstawka (inset), słupki błędów (tylko gdy będą prawdziwe niepewności) |
| Odłożone (pułapki) | dowolne warstwy, przerwy osi, fitowanie, wygładzanie, swobodny edytor rysunku; renderer nie liczy fizyki |

**Werdykt (Origin czy matplotlib):** matplotlib zastąpi Origina przy rutynowych rycinach do pracy z tych
paneli, gdy dojdą trzy rzeczy:
- przesuwanie i ukrywanie adnotacji;
- nakładka eksperymentu;
- proste panele a/b/c.

TSV zostaje na wyjątkowe kompozycje (nietypowe drabinki obsadzeń, rozbudowane wstawki, nieregularne układy)
oraz na analizę eksperymentu (fit, tło, wygładzanie).

**Personalizacja: jeden model stylu, dwa renderery.**
- Jeden wersjonowany plik projektu `plot_styles.yaml`:
  - IO: struktury wartości i (de)serializacja;
  - Presentation: domyślne wartości, nadpisania i budowa PlotData;
  - App: składa właściciela magazynu.
- Klucz: rodzaj wykresu / stabilne ID przypadku lub źródła / rola semantyczna / podgrupa.
  Nigdy etykieta ani kolejność porównania. Dziś S/PL używa `DirectoryLabel`, a ΔQ² stabilnych ID.
  Przykład:

  ```yaml
  formatVersion: 1
  series:
    "phonon.S/case-1:dataset-A":
      spectrum: {color: [0.1, 0.3, 0.8, 1]}
    "ccd/case-1:dataset-C":
      "levels/all": {lineWeight: 1.0}
    "formation/case-1:energetics":
      "envelope/defect-NV": {lineWeight: 2.5}
      "charge/defect-NV/q-minus-1": {lineStyle: 1}
    "concentration.T/case-1:solution-A":
      electrons: {lineStyle: 1}
      holes: {lineStyle: 2}
    "dispersion/bulk:dataset-D":
      "branches/all": {lineWeight: 0.8}
  ```

- Grupy wspólnego stylu zostają (punkty/poziomy/funkcje CCD, gałęzie dyspersji).
- Migracja: stare `seriesStyles` fononów czytane jako zapas, nowe wpisy wygrywają, nic nie ginie, stary
  plik zostaje do udanego zapisu.
- **Wspólne dla ImPlot i ryciny:** kolory, widoczność, etykiety, markery, style linii, jawne nadpisania osi
  (rozmiary w punktach).
- **Tylko rycina:** fonty, cm, marginesy, kierunek i lustrzane ticki, układ legendy.
- ImPlot nie udaje ryciny. Zoom nie zmienia po cichu zakresu ryciny; jest jawne „użyj bieżącego widoku”.
- Popupy ImPlot zmieniają magazyn w pamięci od razu, a zapisują go po zakończeniu edycji.
- W popupie ryciny każde ustawienie ma znaczek: **Projekt**, **Ta rycina** albo **Tylko rycina**.
  - Edycja dziedziczonego ustawienia tworzy lokalne nadpisanie.
  - „Przywróć z projektu” usuwa nadpisanie, „Zastosuj w projekcie” je promuje.
  - Zapisana rycina trzyma wartości rozwiązane; „Odśwież z projektu” jest jawne.
- Bez osobnego systemu presetów JSON. Domyślne wartości per rodzaj wykresu są w kodzie, nad nimi nadpisania
  projektu i zapisane FigureSpec.
- Powtarzalność: dziś render czyta zmienny `thesis.mplstyle` przy każdym uruchomieniu. FigureSpec v2 ma
  zapisywać rozwiązane rc i wersję matplotlib, a „przestylizuj istniejące ryciny” ma być jawną akcją.

**FigureSpec v2** (dodatki; v1 czytany dalej z wartościami domyślnymi; stary renderer odrzuci v2, czyli
zgodność tylko wstecz):
- `layout`:
  - `legend{position, frame, columns}`;
  - `ticks{direction, mirrored}`;
  - `fontPt{base, axisLabel, tickLabel, legend, annotation}`.
- Oś: `ticks{values, labels}`, czyli jawne ticki zastępujące automatyczne (np. Γ, M, K).
- `annotations[]`, osobno od serii, **nie trafiają do kolumn TSV**:
  - `Text` i `Arrow` we współrzędnych `data` albo `axes` (0–1), z `offsetPt`, wyrównaniem i przycinaniem;
  - `Band` (zakres na jednej osi, np. okno plateau FNV); gradient później;
  - klamry jako odcinki + tekst.
- `heatmap{xEdges, yEdges, values (wiersze = rosnące Y, null = maska), colorScale{map, log, min, max},
  colorbar{show, label}}`.
- `resolvedRc`, `renderedWith`.

**TSV pod Origin: jeden system.**
- Każdy wykres eksportuje przez `PlotDataColumns` → `DataTableIO`.
- Tabele semantyczne (stany, CTL, μ, orbitale) jako `DataTableColumn` przez ten sam zapis. Wyjątek:
  format orbitali zgodny z puntukas zostaje.
- Precyzja: 9 cyfr znaczących dla TSV do wykresów. Energie eksportujemy jako różnice lub wartości
  odniesione (E_f, E − E_VBM), nie surowe TOTEN rzędu −1000 eV. Pełna precyzja zostaje w JSON datasetów.
- Nagłówek: trzy wiersze (nazwa / jednostka / komentarz = źródło). Nazwy w zwykłym Unicode (Δ, ω),
  **bez** składni MathText. UTF-8 bez BOM.
- Własny filtr `DefectStudio.oif` (jawnie: 0 linii nagłówka, 3 subheadery → Long Name/Units/Comments, tab,
  kropka, puste pola zachowane, opcjonalnie wykres z `Base-plot.otpu`) dopiero po teście klikanym w
  Originie 2024. Przerwy (puste komórki) w liniach Origina też do sprawdzenia.

## 4. Etapy

| Etap | Zakres | Uwagi |
|---|---|---|
| **F0** | Wydzielenie ogólnego kodu eksportu/konwersji z plików fononowych. Magazyn `plot_styles.yaml` + klucze + migracja. Profile TSV. | `PhononPlotData.cpp` ma 499 linii |
| **F1** | Domknięcie fononów: adnotacje CCD, ticki dyspersji, β i CCD-PL w rycinach. Nakładka eksperymentalnego PL (2 kolumny, jawne jednostki, skala/normalizacja, pochodzenie; bez auto-dopasowania). | FigureSpec v2: ticki + adnotacje |
| **F2** | E_f(E_F) + CTL oraz diagram obsadzeń jako PlotData. Stary eksport PNG obsadzeń przechodzi na wspólny renderer. Eksport energetyki przez DataTableIO. | |
| **F3** | FNV, koncentracje/E_F (T i λ), potem mapa (heatmap XYZ + colorbar). | |
| **F4** | Podgląd PNG w panelu (subprocess, debounce, najnowsze żądanie wygrywa), potem proste a/b/c. | |
| później | Zbieżność, ewolucja KS i dystrybuanta jako ryciny. `.oif`. Inset. Słupki błędów. | |

Każdy etap kończy się fixture'ami PDF i TSV pokrywającymi ryzyka naukowe: odniesienia energii, spin,
dokładne przecięcia, przerwy, zawijanie FNV, orientację mapy.

**Poza zakresem:**
- arkusz i edycja danych;
- fitowanie i wygładzanie;
- import dowolnych TSV (poza nakładką PL);
- swobodne rysowanie;
- przerwy osi;
- dowolne warstwy;
- pełny LaTeX (MathText domyślnie);
- `.opju` przez `originpro`.

## 5. Pytania do użytkownika

1. Czy lista z §3 to faktycznie wykresy do pracy? Coś brakuje, np. DOS lub struktura pasmowa (dziś ich nie
   ma w aplikacji)?
2. Nakładka eksperymentalnego PL: tak/nie? W jakim formacie masz dane (kolumny, jednostki: eV czy nm)?
3. Test w Originie (2 minuty): przeciągnij dowolny TSV z „Eksport… → Origin headers” z panelu fononów do
   Origina. Czy nazwy, jednostki i komentarze trafiają we właściwe wiersze, a przerwy w liniach zostają?
   To rozstrzyga, czy warto robić `.oif`.
4. Czy `Base-plot.otpu` to Twój docelowy wygląd rycin? Jeśli tak, `thesis.mplstyle` warto do niego zbliżyć
   (fonty, grubości, ticki). Wystarczy zrzut ekranu wykresu z tym szablonem.
5. Kolejność F0 → F1 → F2 → F3 → F4, czy energetyka (F2) przed domknięciem fononów (F1)?
