# Fonony: wyniki z serwera, zbieżność po n, porównanie defektów, PL eksperymentalne (113a-113g)

Status 2026-11-17: 113a-g zaimplementowane (codex) i zweryfikowane buildem + testami; GUI niesprawdzone.
Commity: f1d41a4 (113a), b300e0e (113f1), 51e914d (113e+113b), ac33ab0 (113f2+113d), następny (113c+113g).
Branch: `task/109-phonon-workspace` (bez merge).

## Skąd to się wzięło (2026-11-17)

Na P: (= `sjcm_mgr` na kriaucius) w `PL-spectra/scripts` są skrypty z 12-13.11, liczone na klastrze:

| Skrypt | Co robi | Wynik |
|---|---|---|
| `embeding.py` + `run_embeding_series.py` | embedding FC dimeru 9×9×1 w bulk n×n×1, n = 9..50, MPI/scalapack, 36 rdzeni | `9x9x1/embedded/NxNx1/{modes.h5, freq.dat, irreps.dat, iprs.dat, defect.yaml, POSCAR, embedding_pattern.yaml, peak_rss_gb.txt}` |
| `hr_factors.py` | `calc_hr_factors`, metoda `displacements`, ES singlet → GS (`CONTCAR_aligned`), cutoff auto 6,06 Å (46 atomów) + 3/4/5 Å | `9x9x1/spectral-function/NxNx1/<metoda>[_cutoff<C>A]/{freq_mev, hr_factors, irreps, fc_displ}.dat + hr_factors.log` |
| `lineshape.py` + `lineshape_sweep.py` | PL T=0 (funkcja tworząca), ZPL 3452 meV, siatka σ / σ_max / γ | `9x9x1/lineshape/PL_<metoda>_<parametry>.{dat,png}` - jeden plik Origin (3 wiersze nagłówka, bloki kolumn różnej długości) |

Wyniki (σ 2, γ 0,5 meV): S = 1,421-1,422 dla każdego n = 9..50, DW 0,241, ℏω_eff 146,1 meV, E_FC 207,7 meV;
cutoff 4 Å: S = 1,413. Aplikacja (raport 104): embedding 1,4222 (9) / 1,4217 (12, 24, 36), bez embeddingu 1,407
(pełne przesunięcie, bez odcięcia - prawdopodobna przyczyna różnicy, niesprawdzone).

Wnioski fizyczne:
- Przy `displacements` + cutoff S nie może istotnie zależeć od n: ΔR jest obcięte do sfery wokół defektu, większy
  embedding zagęszcza tylko S(ℏω). Zbieżność dalekozasięgowa wymaga metody siłowej: `forces` (siły GS w geometrii
  ES - dla dimeru 9×9×1 brak takiego obliczenia, użytkownik 2026-11-17) albo `approximate_forces` (siły z
  przesunięć przez FC bulku, bez nowego DFT - do użycia od razu).
- Rozkład S na irrepy psuje się z n (S_A1 1,42 → ~0,65, reszta w `???`, tolerancja 0,01) - kolumny irrepów dla
  embeddingu pokazywać z ostrzeżeniem i z S_???.

## Decyzje użytkownika (2026-11-17)

1. Tak: cutoff i metoda HR w aplikacji. 2. Tak: widok zbieżności po n z eksportem. 3. Brak sił ES@GS dla dimeru.
- Aplikacja ma dać kontrolę jak skrypt i **czytać gotowe obliczenia z serwera** („bardzo ważne”).
- **Porównywanie różnych defektów** dokończyć tak, jak w module termodynamiki („bardzo ważne”).
- PL eksperymentalne z pliku do porównania; format teraz: tekst, energia w eV - ale tak, by dało się rozszerzać.
- Zestawy wyliczone z danych serwera: w folderze projektu (`<projekt>/defectstudio/phonons/...`); dane serwera
  czytane na miejscu, P: nietknięte.
- Duże n (nie mieszczą się lokalnie): aplikacja generuje gotowy folder zadania, użytkownik sam robi `sbatch`,
  aplikacja potem znajduje wyniki. Aplikacja nie wysyła zadań.
- Wektory własne dla wyników z serwera: na żądanie, jeden mod (częściowy odczyt `modes.h5`).
- Potem moduł rycin (plan `2026-10-20-figures-tsv-origin.md`).

## Zadania

| Task | Zakres |
|---|---|
| 113a | **Odczyt wyników z serwera.** `phonon_vibronic_analysis.py --hr-dir <spectral-function/NxNx1/<metoda>>`: bez FC i diagonalizacji, czyta `freq_mev/hr_factors/irreps.dat`, IPR z `embedded/NxNx1/iprs.dat` (jeśli jest), ścieżki GS/ES i cutoff z `hr_factors.log`, liczy S(ℏω)/PL z parametrami aplikacji (ta sama funkcja widm co tryb lokalny - wydzielić). Zapis `ds.phonon_vibronic.v1` z `source: "server"`, `has_eigenvectors: false`, `n`, `method`, `cutoff_A`, mtime `hr_factors.dat`. `PhononAnalysisIO` przyjmuje brak `.npy`, gdy summary tak mówi; animacja/strzałki wyłączone z powodem w tooltipie (do 113g). Nieaktualny względem serwera (mtime) → „Odśwież”. |
| 113e | **Defekty jak w termodynamice.** „Dodaj defekt z folderu…” (+ prawy klik w drzewie projektu): skan rozpoznaje zestawy aplikacji, układ serwera (`spectral-function/`, `embedded/`, `dispersion/`, `relax/`, `excited/*/`) i surowe wejścia VASP; proponuje przypadek (GS/ES/FC/bulk wypełnione, warianty n/metoda/cutoff jako zestawy), użytkownik zatwierdza wybór obliczeń. Menu: Ukryj/Pokaż, Usuń z analizy; foldery zapamiętane w `phonon_panel.yaml`. Porównanie (109d) obejmuje wszystkie widoczne przypadki; kolumny: źródło (lokalne/serwer), n, metoda, cutoff, E_FC, ℏω_eff. Wzorzec: `DefectEnergeticsPanelDefects.cpp`. |
| 113f | **PL eksperymentalne.** „Wczytaj widmo eksperymentalne…” per przypadek (kilka na przypadek), plik czytany na miejscu, ścieżka względem projektu. Czytnik tekstu: separator auto (tab/spacja/przecinek/średnik), linie nienumeryczne pomijane, wybór kolumn x/y. Jednostka x jako enum z jedną funkcją konwersji (teraz eV; nm, cm⁻¹ i jakobian dopisuje się w jednym miejscu). ZPL eksperymentalny: wpisany albo z maksimum w oknie. Ta sama normalizacja (max/pole) i oś (E albo E − E_ZPL) co widma liczone; w PL, porównaniu i eksporcie TSV. |
| 113b | **Parametry jak w skrypcie**, dla lokalnych i serwerowych: σ, σ_max (σ rośnie liniowo z ℏω), γ, ZPL (presety + ręcznie meV), metoda HR (`displacements`, `approximate_forces` - FC bulku z przypadku, `forces` - nowe pole „GS w geometrii ES”), cutoff (auto albo Å) także bez embeddingu. Sprawdzenie: dimer 9×9×1 lokalnie z cutoff auto = 1,422 jak na serwerze; `approximate_forces` vs `displacements` na 9..36. |
| 113c | **Zbieżność po n.** Seria = zestawy przypadku różniące się tylko n. Zakładka „Zbieżność” w `Fonony: wykresy`: S, DW, ℏω_eff, E_FC, S_irrep (z S_???) od n; nakładka L_n i S_n(ℏω) (gradient kolorów). Eksport jednym plikiem Origin w układzie `lineshape.py` (3 wiersze nagłówka, bloki różnej długości) przez `DataTableIO` (sprawdzić obsługę kolumn różnej długości). |
| 113d | **Generator zadania na serwer.** Z przypadku: FC defektu, FC bulku, GS/ES (wyrównane do bulku - `origin_shift` jak w 104f, zapis `CONTCAR_aligned`), n_min..n_max, metoda, cutoff, σ/γ → folder z `embeding.py`, `hr_factors.py`, `lineshape.py`, `run_embeding.job`, `run.job`, `run_embeding_series.py` (szablony = skrypty użytkownika z P:, w `scripts/python/server_templates/`, z lokalnymi poprawkami dephonopy). Bez wysyłania. Wyniki znajduje 113a. |
| 113g | **Wektor jednego modu na żądanie.** `phonon_mode_vector.py --embedded-dir … --mode k` czyta jeden mod z `modes.h5` (h5py, wycinek) + `POSCAR`; animacja i strzałki dla tego modu. |

Kolejność: 113a → 113e → 113f → 113b → 113c → 113d → 113g (najpierw to, co użytkownik nazwał „bardzo ważne”).

## Otwarte

- 109d (Porównanie) nigdy nie było oglądane w aplikacji. Przed 113e warto, żeby użytkownik kliknął „Porównanie”
  i powiedział, czego brakuje względem termodynamiki - lista w 113e to propozycja.
- Skrypty i wyniki z 12-13.11 na P: prawdopodobnie niezacommitowane w `sjcm_mgr` (ostatni commit 28.09;
  `git status` na montowanym dysku nie zdążył się wykonać).
