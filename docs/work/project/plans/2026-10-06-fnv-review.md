# Recenzja planu FNV + decyzje użytkownika

Data: 2026-10-06. Dotyczy `2026-10-05-fnv-charge-correction.md`. Recenzja własna + codex (grill,
high). Następnik: `2026-10-06-defect-energies-and-origin-export.md`.

## Decyzje użytkownika

- Przypadek testowy: diament z pracy inżynierskiej. Defekty: GeV, SiV, GeNV, SiNV, GeN2V, SiN2V,
  GeN, SiN.
- Dane referencyjne: oryginalny sxdefectalign, przybliżone współrzędne środków, wyniki (być może
  w `.opju`).
- LOCPOT z `LVHAR=.TRUE.`.
- Znak w sxdefectalign: `--charge` odwrotnie niż intuicyjnie (q = +1 → `--charge -1`). Plateau
  (`-C`) wybierane ręcznie w Origin.
- ε podane przez promotora (zapisać pochodzenie).
- Zakres: na razie tylko E_corr; potem energie tworzenia i wiązania oraz eksport do Origin (osobny
  plan).
- Najpierw wariant izotropowy; anizotropię (hBN) użytkownik konsultuje z promotorem.

## Plateau: półautomatycznie (decyzja 2026-10-06)

W inżynierce plateau było wybierane ręcznie w Origin. W aplikacji aplikacja proponuje okno, a
użytkownik może je zmienić albo przeliczyć krok 1. Podział:

- **Krok 1 (Python, kosztowny):** odczyt LOCPOT defektu i bulk, profile planar average ΔV(x) dla
  3 osi, potencjał modelu V_model(x) i E_lat dla (q, ε, środek, ecut, model ładunku). Wynikiem są
  małe tablice (3 × N_grid), zapisane w analizie.
  - pymatgen zwraca te dane w `CorrectionResult.metadata` (`plot_data`/`pot_plot_data`: x, Vr,
    dft_diff, check). Niezweryfikowane, sprawdzić na przypiętej wersji.
- **Krok 2 (C++, natychmiastowy):** wyrównanie = średnia (ΔV − V_model) w oknie plateau.
  `E_corr = E_lat − q·ΔV_align`; znak według konwencji silnika, sprawdzony testem z sxdefectalign.
  Zmiana okna nie uruchamia Pythona.
- **Propozycja okna:** na każdej osi obszar najdalej od defektu (okresowo). Szerokość, przy
  której rozrzut reszty w oknie jest najmniejszy, z minimalną szerokością jako parametrem.
  Aplikacja pokazuje rozrzut reszty i zgodność ΔV_align między osiami; brak plateau = ostrzeżenie,
  nie blokada.
- **Korekta przez użytkownika:**
  - przeciąganie krawędzi okna na wykresie ImPlot (`DragLineX` / `DragRect`) albo wpisanie
    liczb;
  - wybór osi (jedna, średnia z kilku);
  - „Przelicz krok 1” ze zmienionym ε, środkiem, ecut lub modelem;
  - każda zmiana zapisana w pochodzeniu wyniku (propozycja automatyczna czy ręczna).

## Poprawki do planu

1. **Kolejność.** Start od LOCPOT i jednej poprawki zgodnej z sxdefectalign na diamencie.
   eFNV, interpolacja siatek i 3-poziomowy cache przesunięte za MVP. Niezgodne siatki są w MVP
   odrzucane.
   - **Zmiana 2026-10-06 (decyzja użytkownika): CHGCAR jest w MVP.** Wizualizacja gęstości spinowej
     (blok magnetyzacji CHGCAR stanu q=±1) i Δρ = ρ(q) − ρ(0) służy do ręcznego wskazania, gdzie
     jest dodany/zabrany elektron, czyli `--center`, oraz do oceny lokalizacji ładunku. Analiza
     diamentu pokazała, że bez tego FNV liczy się też dla stanów zdelokalizowanych (Ge_C/Si_C ±1,
     XN2V −1: nachylenie ΔV_DFT/V_model ≈ 0–0.3), gdzie wynik nie ma sensu.
   - Reużycie: siatka i izopowierzchnie już istnieją dla orbitali WAVECAR
     (`VaspOrbitalGridBridge`/`VaspOrbitalGridConversion`, `IsosurfaceMesher` +
     `isosurface_march.comp`, `ElectronicStructurePanel`). Nowy jest tylko odczyt
     `puntukas.vasp.Chgcar` (blok total + magnetyzacja) i semantyka „gęstość”, nie „orbital”.
   - W MVP: propozycja środka = centroid |m(r)| (lub |Δρ|), przeciągana przez użytkownika;
     metryka lokalizacji = ułamek |m| w kuli R wokół środka (R = 5 Å ≈ 18% komórki 512 at.)
     obok nachylenia z LOCPOT; ostrzeżenie, gdy ładunek jest zdelokalizowany.
2. **Silnik.** `pymatgen-analysis-defects` nie jest zainstalowany (ani `.venv`, ani
   `install/app/python`).
   - Bierze skalarne ε.
   - Wyrównanie liczy w stałym oknie 1 Å, podczas gdy w sxdefectalign `-C` było wybierane ręcznie.
     Dlatego porównywać osobno E_lat (prawie dokładnie) i wyrównanie (z tolerancją).
   - Okno plateau półautomatyczne (sekcja wyżej), co jest też potrzebne, żeby odtworzyć
     inżynierkę.
3. **Profile.** puntukas `planar_average` ma oś bez końca przedziału, a słowniki pymatgen oś z
   końcem (endpoint-inclusive). W MVP podać pymatgenowi LOCPOT bezpośrednio. Adapter profili z
   puntukas dopiero po teście równoważności.
4. **Dielektryk.** puntukas czyta tylko `epsilon` (ε∞), bez `epsilon_ion`. Dla diamentu bez
   znaczenia (brak modów IR, ε_ion ≈ 0, a ε i tak daje promotor); dla hBN do uzupełnienia.
5. **Kanał LOCPOT.** Przy LVHAR potencjał jest skalarny (Hartree + jony). Rozbieżność
   „total/mag” w docstringu puntukas vs „up/down” w VASP sprawdzić na prawdziwym pliku `ISPIN=2`.
6. **q.** Liczyć `q = Σ N·ZVAL − NELECT` z wyjściowego NELECT, walencje z POTCAR przez puntukas
   `vasp/input.py:324,336`. Brak POTCAR oznacza q nieznane. Pamiętać o odwrotnym znaku w
   sxdefectalign.
7. **Środek defektu.** Wakans: pozycja usuniętego atomu (`DefectModel.cpp:91–98`). Split-vacancy
   (SiV, GeV): środek wiązania między dwoma pustymi węzłami (pozycja Si/Ge). Współrzędne z
   inżynierki to punkt startowy.
8. **Anulowanie i timeout.** Podłączyć w nowym bridge'u. `VaspOrbitalGridBridge.cpp:69–82` i
   `VaspOutputJob.cpp:25` tego nie robią; naprawić we wspólnej ścieżce.
9. **Zapis.** `StorageLayer` to szkielet. `TextFileIO` nadpisuje plik w miejscu, więc zapis
   atomowy trzeba dopisać. Jeden plik analizy wspólny z modułem energii.

## Co użytkownik dostarcza

- Dysk z danymi. Dla bulk supercell i każdego (defekt, q): `LOCPOT`, `OUTCAR`, `INCAR`,
  `POSCAR`/`CONTCAR`, `POTCAR` (lub TITEL/ZVAL), opcjonalnie `vasprun.xml`.
- Binarka sxdefectalign (Linux; uruchamiana w WSL Ubuntu) i dokładne komendy lub skrypty:
  `--ecut`, `--charge`, `--eps`, `--center`, `-C`, parametry modelu.
- Współrzędne środków z jednostkami i układem (ułamkowe/kartezjańskie, Å/bohr, która komórka).
- Wyniki: E_corr dla każdego q (najlepiej E_lat i wyrównanie osobno). `.opju` czytelny przez
  `originpro` (Origin 2024 zainstalowany), alternatywnie eksport CSV.
- Wartość ε od promotora.
- Zgoda na instalację `pymatgen-analysis-defects` (+ scikit-image, mp-pyrho) do `.venv` i
  `install/app/python`, z przypiętą wersją.

## Dane testowe znalezione (2026-10-06)

Przeszukane dyski C:, D:, E:. Katalog główny (z „ż”):
`D:\STUDIA\Fizyka\01_Engeenering-Studies\Praca-Inżynierska\`. **Tylko do odczytu**: dane
źródłowe i repo użytkownika z niezacommitowanymi zmianami. Nic tam nie zapisywać.

- **GeN, q = −1: jedyny pełny zestaw.** Katalog `Diament\GeN\Stan_1-\Poprawki-Elektrostatyczne\Testy\`:
  - `LOCPOT-bulk`: 106 MB, 512 C, siatka 180³, 1 blok, skala 1.0 i wektory 14.183163 Å;
  - `LOCPOT_1-`: 212 MB, 510 C + Ge + N, **2 bloki** (ISPIN=2), skala 14.1831627666534 i wektory
    jednostkowe. Inny zapis komórki niż w bulk, więc to dobry test czytnika;
  - `sxdefectalign(2)`: binarka Linux, działa w WSL `-d Ubuntu`;
  - oryginalne wyjścia z 2025-03-05: `vline-eV-a{0,1,2}.dat`, `vAtoms.dat`, `vlinex.dat`;
  - `backup/`, `first/` i `Stan_1-.zip` to identyczne kopie; `second/` jest pusty.
- **GeV:** tylko `vline-eV-a*.dat` (stan +1, stan −2) i `UNTITLED.opju`, bez LOCPOT. Nadaje się
  najwyżej do porównania profili lub wykresów.
- **Notatki z procedurą:** `Engeenering-thesis\Notatki\Charakteryzacja-defektów.md`. Komenda dla
  GeV: `--ecut 44.11 --charge +2 --eps 5.7 --center 0.468766,0.531233,0.468766 --relative --vasp
  -C 0.02`. Dla stanu −2 podaje się `--charge +2`, co potwierdza odwrotny znak.
- **Brak LOCPOT** dla SiV, GeNV, SiNV, GeN2V, SiN2V i SiN (być może na osobnym dysku
  użytkownika). Brak projektu Origin dla GeN i brak energii tworzenia: `formation_energies.csv` w
  repo pracy to dane zabawkowe.

### Golden run GeN q = −1 (odtworzony, zweryfikowany)

Oryginalna komenda nie była zapisana. `--center` odtworzono trilateracją z `vAtoms.dat`
(residuum 1e-4 bohr). Ponowne uruchomienie odtwarza `vline-eV-a*.dat` do 1e-6, a `vAtoms.dat` do
1e-4.

```text
sxdefectalign --ecut 44.11 --charge 1 --eps 5.7 --center 0.461240,0.559967,0.461206 \
  --relative --vdef LOCPOT_1- --vref LOCPOT-bulk --vasp
Excess Electrons = 1 located at {12.3623,15.0084,12.3614} (bohr)
V average: -0.00444002 eV, vAlign=0 eV
Isolated energy 0.398942 Ha; Periodic energy 0.346012 Ha; Difference -1.4403 eV (unscreened)
Defect correction (eV): 0.252685 (incl. screening & alignment)
```

- ε = 5.7. `--ecut 44.11` Ry odpowiada 600 eV. Komórka ma 26.8023 bohr.
- Wartość `-C` z drugiego przebiegu (plateau) nie przetrwała. Test porównuje więc E_lat
  (0.252685 eV przy C = 0) i profile osobno, a wyrównanie dla zadanego C liczy wzorem.
- Środek nie wpływa na E_lat, tylko na profile.
- Pozycje (frac) z `LOCPOT_1-`: Ge (0.490760, 0.501601, 0.490721), N (0.441561, 0.598878,
  0.441530). Odtworzony środek leży na linii Ge–N, bliżej N.
- Kolumna V_def − V_bulk w `vAtoms.dat` zgadza się bit w bit niezależnie od środka. Oryginał
  czytał więc **pierwszy blok** `LOCPOT_1-`. Rozstrzyga to sprawę kanału spinowego (pkt 5
  poprawek): test czytnika puntukas ma wziąć blok 0 i porównać.
- Środowisko do ponownego uruchomienia (sesja-siostra, scratchpad): `sxrun/run.sh`, zmienne
  `Q`, `CENTER` i `EXTRA`; uruchamiane przez
  `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash <ścieżka>/run.sh`. Wynik ląduje w scratchpadzie,
  nie w danych użytkownika.

### Test akceptacyjny silnika FNV (GeN q = −1)

LOCPOT-y (~320 MB) **nie trafiają do repo**. Test czyta je ze ścieżki z env var (np.
`DS_FNV_GOLDEN_DIR`) i robi `GTEST_SKIP`, gdy jej nie ma. Do repo idą co najwyżej małe pliki
referencyjne jako fixture: `vline-eV-a*.dat`, `vAtoms.dat` i oczekiwane liczby.

1. Czytnik LOCPOT: oba formaty nagłówka (skala 1 + wektory oraz skala + wektory jednostkowe),
   2 bloki, siatka 180³, kolejność osi x-fastest.
2. Profile planar average dla 3 osi vs `vline-eV-a{0,1,2}.dat` (ΔV DFT, V_model).
3. E_lat vs 0.252685 eV; składowe: isolated 0.398942 Ha, periodic 0.346012 Ha.
4. Wyrównanie dla zadanego C liczone wzorem, zgodne z sxdefectalign uruchomionym z tym `-C`.

Tolerancje ustalimy przy pierwszym przebiegu. Kolejne defekty dopiero, gdy będą ich LOCPOT-y.

## Kryterium odbioru MVP

Dla każdego (defekt, q) z inżynierki:
- E_lat zgodne z sxdefectalign w granicach ~1 meV;
- wyrównanie przy tym samym oknie plateau zgodne w granicach ~0.01 eV;
- q = 0 daje 0.

Tolerancja według użytkownika „do przetestowania”, ustalimy przy pierwszym porównaniu.

## Test zgodności pymatgen-analysis-defects 2026.3.20 vs sxdefectalign (GeN q = −1, 2026-10-06)

Skrypt: `test-directory/fnv-diamond/analysis/pymatgen_vs_sx.py` (dane lokalne, gitignored).

- ΔV_DFT (planar average, pierwszy blok LOCPOT): zgodne do 5e-7 eV na wszystkich osiach. Czytnik,
  kanał spinowy i siatka (180, bez końca przedziału) są zgodne.
- E_lat: 0.252535 vs 0.252685 eV, różnica −0.15 meV (kryterium ~1 meV spełnione).
- V_model: stałe przesunięcie +3.66 meV (plus kształt w rdzeniu σ ≤ 3 meV, w oknie plateau ~0.2 meV).
  Przyczyna: wyraz G = 0 modelu Gaussa. sxdefectalign: średnia V_model = −0.00444 eV, pymatgen:
  −0.00078 eV. Stosunek = 5.70 = ε, czyli pymatgen dzieli wyraz G = 0 przez ε, a sxdefectalign nie.
- Skutek: C różni się o ~3.5 meV, E_corr o +3.4 meV dla |q| = 1 (0.2333 vs 0.2299 eV). Wyraz ∝ q,
  więc różnica E_corr ∝ q²: przewidywane ~14.6 meV dla |q| = 2 (do sprawdzenia na GeV −2).
- Okno pymatgen 1 Å daje E_corr 0.2346 eV; z naszym plateau 0.2333 eV.
- Do decyzji: którą konwencję G = 0 przyjąć (zgodność z pracą = sxdefectalign) i czy korygować ją
  stałą, czy liczyć V_model samodzielnie.
