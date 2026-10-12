# Task 104: fonony - raport (2026-10-12)

Gałąź `task/104-phonons`, lokalnie, bez pusha i merge'a. Dane referencyjne: dimer C2 w monowarstwie
hBN 9x9x1, r2SCAN, lokalna kopia `test-directory/sjcm_mgr` (bez WAVECAR/CHGCAR/LOCPOT/TAUCAR,
gitignored).

## Co jest zrobione

| Task | Commit | Co |
|---|---|---|
| 104a | `504e642` | `scripts/python/examples/phonon_vibronic_analysis.py`: mody Γ, irrepy, N_eff/β, udziały atomów, S_k, S_tot, DW, S(ħω), PL dla ZPL obliczonego, po korekcji spinowej i eksperymentalnego |
| 104b, 104c | `bca86f8` | czytnik katalogu wyników w C++, strzałki/animacja (Domain), most + job z anulowaniem, `phonon_panel.yaml` |
| 104e | `6dc022a` | `scripts/python/examples/ccd_inputs.py`: Twój `build_ccd.py` uogólniony na dowolną parę gs/es |
| 104d | `ffa5915` | panel **Widok → Analiza → Fonony** |
| 104f | `8976a00` | embedding FC w skrypcie (`--bulk-fc-dir --mult`), automatyczne wyrównanie początku układu |
| 104g | (ten commit) | embedding (bulk FC, mult, cutoffy) i generator CCD w panelu, solver scipy `evd` |

Testy Release po 104g: 1582 zaliczonych, 5 pominiętych (te same pominięcia co wcześniej), w tym testy end-to-end przez Pythona aplikacji: zwykły i z embeddingiem 12x12x1. Python: 38 zaliczonych; 1 test `test_check_include_directions` pada, ale padał już w bazie gałęzi (`3739e5b`), więc nie wynika z tej pracy.

## Wyniki dla dimera (do sprawdzenia fizycznie)

- 162 atomy, wszystkie mody stabilne w 9x9x1.
- Dominujący mod: **A1, 200,1 meV, S_k = 0,525**, β = N/N_eff = 35,5 (zgodnie z Twoim `PL-spectra/scripts/test.py`). Mniejsze sprzężenie przez 176 i 186 meV oraz garb ~42 meV.
- **S_tot = 1,407** (9x9x1, bezpośrednio), **DW = 0,245**. S_k liczone niezależnie i porównane z dephonopy (zgodność 1e-4).
- **ZPL z energii całkowitych:** 3,452 eV (stan mieszany ΔSCF) i 3,749 eV po korekcji 2E_mix − E_T.
- **Embedding:** S_tot = 1,4222 (9x9x1), **1,4217** (12x12x1, 24x24x1 i 36x36x1). Zbieżne już przy 12x12x1. Mod A1 zbiega do 199,81 meV (S = 0,537).
- ΔQ = 0,334 amu^½ Å, max |Δr| = 0,063 Å.

## Ustalenia o danych

1. **`try_1_tryplet` to obliczenie NSW=0** w geometrii singletu, a nie relaksacja trypletu. Używam go tylko do korekcji spinowej ZPL. Jeśli chcesz krzywej dla trypletu, trzeba go zrelaksować.
2. **Embedding na kriaucius padał przez przesunięcie początku układu.** W komórce defektu B siedzi na pozycji N bulku (przesunięcie (2/3, 1/3) komórki elementarnej). dephonopy uznawał przez to każdy atom za defekt i wychodził ujemny `dcutoff`. Do tego w `embeding.py` było `.format(MULT)` zamiast `.format(*MULT)`. Skrypt sam wykrywa teraz przesunięcie: (1,228; 0,686; 0,009) Å, 160 atomów gospodarza, 2 podstawienia.
3. W dużych komórkach 2D (36x36x1) pojawiają się mody urojone ~−4 meV: długofalowe ZA (zginające). Są raportowane, nie wyrzucane; panel pokazuje je czerwoną plakietką.

## Wydajność (side quest GPU)

Wąskim gardłem jest `eigh` macierzy 3N×3N. CPU i7-13700KF vs RTX 4060 (CuPy w osobnym venv):

| Komórka | CPU numpy | CPU scipy evd | GPU f64 | GPU f32 (błąd) |
|---|---|---|---|---|
| 36x36x1 | 40 s | 17,8 s | 11,1 s | 2,8 s (0,05 meV) |
| 48x48x1 | 231 s | ~100 s | 61 s (7,1 GB VRAM) | 11 s (0,03 meV) |

Wniosek: w float64 GPU daje tylko ~1,6× względem scipy `evd` i kończy się VRAM przy ~48x48x1. W 104g domyślnym solverem jest scipy `evd`; CuPy nie jest dodane do runtime'u.

## Do przeklikania (panel Fonony)

1. Widok → Analiza → Fonony.
2. Prawy klik na folderach w drzewie projektu → Fonony → ustaw jako katalog FC (`.../9x9x1/dispersion`), stan podstawowy (`relax`), stan wzbudzony (`excited/try_1_singlet`), tryplet (`excited/try_1_tryplet`). ZPL eksperymentalne zaznacz i wpisz wartość.
3. **Policz.** Wynik trafia do `<projekt>/phonons/...` po kilku sekundach.
4. Tabela modów: sortowanie, filtr irrepów; kliknięcie w wykres β(ħω) lub S_k wybiera mod.
5. **Otwórz superkomórkę**, wybierz mod 200,1 meV: strzałki na dimerze i sąsiadach. Suwak progu udziału ukrywa strzałki, a „N atomów niesie X %” się aktualizuje.
6. ▶: animacja. Po ⏸ działa suwak fazy. ⏹ przywraca atomy na miejsce; to samo przy zmianie modu, zamknięciu panelu lub okna i przed zapisem.
7. Zamknij i otwórz projekt: te same ustawienia, wynik wczytany, nic nie startuje samo.
8. (po 104g) Embedding 12x12x1 z panelu oraz Generuj wejścia CCD do pustego folderu.

## Otwarte pytania

- **ZPL eksperymentalne:** w testach przyjąłem 4,08 eV, zakładając, że dimer C_B C_N to emiter 4,1 eV. Popraw, jeśli to inny emiter.
- **Relaksacja trypletu:** czy chcesz osobną krzywą/PL dla trypletu (wtedy relaksacja na klastrze), czy triplet służy tylko do korekcji spinowej?
- **Domyślny wzór ZPL dla PL:** czy domyślnie pokazywać ZPL po korekcji spinowej zamiast eksperymentalnego?
- **CuPy dla GPU:** dodać jako opcję (≈100 MB plus biblioteki CUDA), czy wystarczy CPU?
- **„Jak mody się transformują”:** teraz jest irrep w tabeli i kolor A1 na wykresie β(ħω). Czy chodziło o coś więcej, np. pokazanie działania operacji symetrii na wektorze modu albo śledzenie modów między komórkami 9x9x1 → embedding? Kodu Lukasa nie znalazłem poza `calculate_irreps`/`calculate_iprs` w dephonopy.
