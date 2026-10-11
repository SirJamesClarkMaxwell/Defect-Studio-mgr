# Energetyka defektów: automatyczne zbieranie obliczeń, potencjały chemiczne, eksport TSV

Data: 2026-10-11. Uzupełnia `2026-10-06-defect-energies-and-origin-export.md`, który zakładał
jeden zestaw μ i jedno obliczenie na wiersz. Zmiany po prośbie użytkownika (2026-10-11) i po
konsultacji z codex (`codex exec`, read-only).

## Prośby użytkownika (2026-10-11)

1. Moduł energii tworzenia w aplikacji: E_f(E_F), obwiednia, poziomy przejść.
2. „Od razu rozwiązać sytuację z potencjałami chemicznymi”.
3. Eksport TSV: przyjęty środek ładunku, finalne E_corr, C dla a1/a2/a3, energie tworzenia,
   energie przejść, dla różnych wartości μ.
4. Wskazać ogólny folder defektu, a aplikacja sama liczy FNV i energie tworzenia. Przy tym
   **musi się dać wybrać konkretne obliczenia jako zbieżne** („bardzo ważne”).

## Decyzje

- **Dwa wybory na (defekt, q):** obliczenie energii (TOTEN) i obliczenie potencjału (LOCPOT dla
  FNV). Zwykle to samo, ale nie zawsze: w pracy inż. GeN q−1 brało TOTEN z `HSE_Normal`, a LOCPOT
  z `Electrostatic-Correction`.
- **Wybór użytkownika jest święty.** Odświeżenie nigdy go nie nadpisuje. Domyślna propozycja to
  tylko propozycja z podanym powodem.
- **q z OUTCAR:** `q = Σ N_s·ZVAL_s − NELECT`. Nazwa folderu (`Stan_1-`, `q-1`) to podpowiedź;
  niezgodność zgłaszana, nigdy nie zaokrąglana po cichu.
- **Status obliczenia:** zakończone (stopka z czasem w OUTCAR), elektronowo zbieżne, jonowo
  zbieżne („reached required accuracy”, albo „nie dotyczy” dla statycznych), odcisk metody (HSE:
  LHFCALC/HFSCREEN/AEXX/PRECFOCK, ENCUT, PREC, ISPIN, PAW).
- **Domyślna propozycja:** zakończone i zbieżne → metoda zgodna z bulkiem → PRECFOCK Normal przed
  Fast → HSE przed PBE. Remis = „wybierz”. Nigdy według najniższego TOTEN ani daty. Tylko metadane
  nie odtworzą każdego wyboru z inż. (`HSE_Normal_Locpot_2`), więc wybory zapisywane są w projekcie.
- **Odczyt po wolnym montażu:** O: (SSHFS) czyta ~0,5 MB/s, a OUTCAR z PBE ma do 24 MB. Wykrywanie
  czyta tylko początek (~256 KB: POTCAR/ZVAL, NIONS, NELECT, INCAR) i koniec (~128 KB: ostatni TOTEN,
  zbieżność, stopka) OUTCAR, nigdy LOCPOT/CHGCAR/WAVECAR. Wynik w cache (ścieżka, rozmiar, mtime,
  wersja czytnika); „Odśwież” wymusza ponowny odczyt. Na O: jest 134 OUTCAR.
- **μ:** referencje pierwiastkowe (μ_i^ref), gospodarz (diament: Δμ_C = 0; hBN: Δμ_B + Δμ_N =
  ΔH_f(hBN)), fazy konkurencyjne (SiC, Si3N4, Ge3N4, …) jako nierówności. Aplikacja liczy
  wierzchołki obszaru dozwolonego (wierzchołki = warunki „bogate”; dla domieszek obszar jest
  nieograniczony w dół, więc „ubogie” to zestawy ręczne). Zestawy nazwane: wierzchołki + ręczne,
  każdy sprawdzany względem granic. Dziś na O: są tylko referencje pierwiastkowe (Ge, Si, N2), więc
  dla diamentu jest jeden wierzchołek (wszystkie Δμ = 0, jak w inż.), z etykietą „tylko granice
  pierwiastkowe”. Faz konkurencyjnych program nie wymyśli: trzeba je policzyć.
- **Błąd z inż.:** krzywa SiNV jest wyżej o μ_N − μ_C (+0.294 eV). Zapisany osobno, nie jest
  kryterium akceptacji.
- **Eksport TSV (UTF-8, jeden nagłówek z jednostkami, pełna precyzja, puste = brak):**
  - `states.tsv`: zestaw μ, defekt, q, ścieżki obliczeń energii i LOCPOT, E_tot, E_bulk, E_VBM,
    E_g, środek (f1, f2, f3, źródło), ε, konwencja, E_lat, C użyte, rozrzut C, E_corr, E_f(0),
    E_f(E_g), status; dla każdej osi a1/a2/a3: C, σ, nachylenie, czy użyta, okno (środek, szerokość);
  - `transitions.tsv`: defekt, q_from, q_to, ε (eV od VBM), na obwiedni, w przerwie (bez μ);
  - `formation_<zestaw>.tsv`: E_F, po kolumnie na (defekt, q) i na obwiednię defektu;
  - `mu_sets.tsv`: zestaw, pierwiastek, μ_ref, Δμ, μ, obliczenie referencyjne, status.
- **FNV wsadowo:** kolejka JobSystem, jeden przypadek naraz (pamięć i transfer z O:), zapis po
  każdym przypadku, anulowanie, wznowienie. Zapisane okna plateau zachowane; nowe wpisy dostają
  propozycję. Środek automatycznie z domieszek (FindImpurityCentre); wakanse bez domieszki → „wybierz”.

## Taski

| Task | Zakres | Warstwy |
|---|---|---|
| 95 | znacznik ładunku (FreeLabel z `charge`) związany ze Środkiem FNV | Renderer, Presentation, IO |
| 96 | `DefectEnergetics`: E_f, obwiednia, poziomy, ΔH_f, wierzchołki μ, walidacja zestawów | Domain |
| 97 | katalog obliczeń: odczyt początku/końca OUTCAR, q z ZVAL, status, odcisk metody, cache; grupowanie (defekt, q), propozycja domyślna | IO + Domain |
| 98 | `energetics.yaml`: wybory (energia/LOCPOT), bulk, μ (referencje, fazy, zestawy) | IO |
| 99 | panel „Energetyka defektów”: obliczenia + wybory, μ, wykres E_f(E_F), FNV wsadowo, eksport TSV | Presentation, App |
| 100 | akceptacja: diament z O: vs reprodukcja A/B (≤ 1 meV) | testy |

Energie wiązania (reakcje, łańcuchy) zostają z planu 2026-10-06, po tasku 100.
