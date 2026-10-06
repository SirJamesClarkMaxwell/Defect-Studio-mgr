# Recenzja planu fononów + pytania (parkowane)

Data: 2026-10-06. Dotyczy `2026-10-05-phonons-embedding-and-visualization.md`. Recenzja własna +
codex (grill, high). Pytania czekają na odpowiedź użytkownika, gdy dojdziemy do fononów.

## Werdykt

Plan ma dobre zabezpieczenia fizyczne (masy, mody urojone, degeneracje, aktywność ≠ intensywność),
ale buduje ogólną aplikację fononową przed tym, czego prawdopodobnie potrzebuje praca:
zbieżnego sprzężenia elektron–fonon defektu i wiarygodnego pasma bocznego PL. Do przycięcia.

## Najważniejsze uwagi (wg wagi)

1. **Odwrócona kolejność.** Huang–Rhys, S(E), Debye–Waller i PL są w „rozszerzeniach” (pl. :214,
   :343), za viewerem, kreatorami i UI embeddingu. Najpierw jeden przypadek do końca: S_k, S(E),
   PL.
2. **Sam embedding FC to niepełny workflow wibronowy.** Brakuje przejścia elektronowego, geometrii
   stanu podstawowego i wzbudzonego oraz sił przejścia. Metoda Razinkovas et al., PRB 104, 045303
   (2021) osadza też siły. dephonopy to ma: `hr_factors_from_forces` (`hr_factors/fc_displacements.py:146`).
3. **Środowisko.** dephonopy nie importuje się bez `lxml` **i** `traits` (`force_constants/base.py:32`,
   `parsers/vasp.py:7`). Żadnej z nich nie deklaruje `setup.py:48`; nie ma ich ani w
   `install/app/python`, ani w `.venv`. Brak MPI ma serialny fallback (`utils/mpi.py:22`), ale
   ścieżki HDF5 wołają `barrier`/`allgather`, których atrapa (dummy) komunikatora nie ma
   (`matrix_storage/hdf5_file.py:211`, `phonons/base.py:299`). Runtime aplikacji ma pierwszeństwo
   przed `.venv`, więc naprawa tylko `.venv` nie wystarczy.
4. **IPR w dephonopy to odwrotność definicji z planu.** `iprs` daje efektywną liczbę atomów
   (jednorodnie = N), liczoną z fizycznych przemieszczeń e/√m (`phonons/base.py:195,271`).
   Etykieta musi to mówić. Kwadraty są zwykłe, nie moduły, więc nie nadaje się do zespolonych
   wektorów przy q ≠ Γ.
5. **Procedury HR odrzucają pierwsze 3 mody pozycyjnie** (`fc_displacements.py:245,412`). Potrzebna
   kontrola stabilności, identyfikacja translacji i pełne widmo; częściowe widmo daje częściowe S
   (z etykietą).
6. **Pułapki API:**
   - `calc_luminescence` ma domyślnie ZPL = 1945 meV (NV), a nie SiV;
   - przyjmuje wygładzoną gęstość S(E), nie dyskretne S_k;
   - `iter_modes(weighted=False)` mnoży przez √m, co budzi wątpliwość.
7. **Embedding mutuje wejścia w pamięci** (`bulk_fc.py:311`, `defect_fc.py:203,252`,
   `fc.py:150` ważenie masą w miejscu). Pracować na kopiach, zapisać FC przed diagonalizacją,
   sprawdzić ASR po złożeniu.
8. **Strzałki.** `renderDisplacementArrows` (`OpenGlRendererBackend.cpp:1755`) bierze
   `StructureComparisonResult`; jego próg to górny limit, a wymiary są wpisane na sztywno.
   Wspólne powinno być samo rysowanie cylindra i stożka, a wektory modów osobnym wejściem. Nie
   udawać, że mody to wynik porównania struktur.
9. **Animacja.** Pozycje podglądu muszą trafić do CPU: picking (`ViewportPicking.cpp:47`),
   wiązania (`OpenGlRendererBackend.cpp:1548`) i hash pozycji (`:726`). Sam shader nie wystarczy.
10. **Anulowanie.** Wzorzec `GroupTheoryBridge`/`AnalyzePointGroupJob` go nie podłącza
    (`AnalyzePointGroupJob.cpp:35`, `GroupTheoryBridge.cpp:202`). Możliwy hang przy zabijaniu
    drzewa procesów (`ProcessRunner.cpp:170`) jest niezweryfikowany. Naprawiać we wspólnej
    warstwie.
11. **Storage i leniwe ładowanie nie istnieją.** `StorageLayer` to szkielet. Iteracyjny czytnik
    HDF5 alokuje całą tablicę (`hdf5_file.py:211`). Wybrany wiersz czytać bezpośrednio w Pythonie.
12. **Symetria.** Istniejący skrypt groupy robi skalarne SALC, nie reprezentację wektorową 3N. Za
    to puntukas ma już operatory przemieszczeń Γ i projekcje (`symmetry/displacement.py:46`),
    używane przez `_puntukas_compat.py:668`: reużyć, nie pisać od nowa.
13. **SiV⁻/GeV⁻ mają efekt Jahna–Tellera.** Harmoniczny model przesuniętych oscylatorów wymaga
    jawnej etykiety przybliżenia.
14. **YAGNI do wycięcia:**
    - 7 zakładek i presety;
    - kreatory przesunięć i embeddingu;
    - generowane formularze skryptów;
    - heatmapy FC;
    - 4 poziomy cache i implementacja Storage;
    - animacja przy ogólnym q, mieszanie modów, strzałki prędkości;
    - IR/Raman, termodynamika, S(q,ω), unfolding, Duschinsky;
    - eksport filmów, orkiestracja MPI.

## Minimalne MVP (propozycja)

1. Jeden przypadek naukowy poza UI (skrypt): jeden defekt, ładunek i przejście, jedna nazwana
   recepta embeddingu. Wyniki: częstotliwości, udział (zdefiniowany), S_k, S(E), S_total,
   Debye–Waller, PL. Porównanie: mała komórka vs embedding.
2. Jeden stały adapter Python (ScriptRunner + JobSystem z anulowaniem). Wyniki do katalogu zadania,
   publikowane tylko kompletne.
3. Jeden panel: import katalogu wyników, wykres ω / udziału / S_k, wybór modu Γ, strzałki,
   play/pause, amplituda, faza, screenshot.
4. Podsumowania w RAM, wektory z dysku; cache dopiero po pomiarze.

Duże embeddingi liczone zewnętrznie (HPC), aplikacja tylko importuje.

## Co użytkownik musi dostarczyć (jeden defekt diamentu, np. SiV)

- Definicja: konfiguracja, ładunek, spin, stany początkowy/końcowy, metoda wzbudzenia (ΔSCF?).
- Spójne ustawienia DFT (pliki wejściowe).
- Relaksacja bulk; dawca FC bulk: supercell, `phonopy_disp.yaml` (lub `dephonopy_disp.yaml`),
  wyniki sił dla każdego przesunięcia (`disp-XXX/OUTCAR` lub `vasprun.xml`).
- Relaksacja stanu podstawowego defektu i dawca FC defektu (przesunięcia wokół tej geometrii).
- Relaksacja stanu wzbudzonego. Do embeddingu sił: obliczenie stanu podstawowego **w geometrii
  wzbudzonej** (stałe jony, wszystkie siły) oraz sił stanu wzbudzonego w tej geometrii.
- Konfiguracja embeddingu: rozmiar celu, mapowanie, cutoffy, polityka ASR/symetrii.
- Masy/izotopy, ZPL (eksperyment czy liczone), temperatura, szerokości Gaussa i Lorentza.
- Referencja do walidacji (eksperymentalne PL/ZPL lub opublikowany wynik).
- Zasoby: VASP + PAW, klaster. Python: `lxml`, `traits` (zgoda na instalację w obu runtime).
- NAC zbędne dla diamentu (niepolarny).

## Pytania do użytkownika (odpowiedzieć, gdy zaczniemy fonony)

- P1. Co jest wynikiem w pracy: analiza modów i lokalizacji, harmoniczne pasma boczne PL, czy
  widma z efektem Jahna–Tellera?
- P2. Który jeden defekt, ładunek, spin i przejście optyczne jest przypadkiem referencyjnym?
- P3. Które zbiory już istnieją: FC bulk, FC defektu, relaksacje wzbudzone, siły przejścia w
  stałej geometrii? Gdzie?
- P4. Wszystkie 8 kompleksów diamentu, czy najpierw metoda na jednym? Czy hBN to monowarstwa,
  czy bulk?
- P5. Czy przybliżenie harmoniczne z równymi krzywiznami jest akceptowalne? ZPL eksperymentalne,
  liczone czy oba?
- P6. Co musi trafić do pracy: S_k, S(E), S_total, DW, PL, irrepy, udziały, wykresy zbieżności,
  animacje?
- P7. Jakie zasoby HPC i jaki termin ograniczają rozmiary dawców i celu embeddingu?
- P8. Czy dephonopy to twój/promotora kod, który można poprawiać (deklaracje zależności, atrapa
  MPI), czy tylko używać bez zmian?
- P9. Czy animacja modów w aplikacji jest potrzebna do pracy (rysunki), czy wystarczą statyczne
  strzałki?
