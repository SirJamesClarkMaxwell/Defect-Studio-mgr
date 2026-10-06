# Energie tworzenia i wiązania defektów + eksport do Origin — plan

Data: 2026-10-06. Status: plan, bez implementacji. Następnik planu FNV
(`2026-10-05-fnv-charge-correction.md`, recenzja w `2026-10-06-fnv-review.md`), który dostarcza
`E_corr`.

## Cel

Z katalogów obliczeń VASP (bulk + defekty w kilku stanach ładunkowych) policzyć:

1. energię tworzenia `E_f^q(E_F)` każdego defektu i stanu ładunkowego,
2. termodynamiczne poziomy przejść `ε(q/q')` (dolna obwiednia),
3. energie wiązania dla **konfigurowalnych reakcji i łańcuchów** (np. GeV → GeNV → GeN2V albo
   GeN2 → GeN2V),
4. wyeksportować tabele i krzywe do CSV/TSV (import w Origin).

Test akceptacyjny: liczby z pracy inżynierskiej (diament: GeV, SiV, GeNV, SiNV, GeN2V, SiN2V, GeN,
SiN). Tolerancja zgodności do ustalenia przy teście.

**Zasada UI: nie zamykać użytkownika.** Energie tworzenia i wiązania to wynik modelu przy
przyjętych założeniach (μ, ładunki, poprawka), a nie werdykt fizyczny. Aplikacja liczy oba
warianty ładunkowe, pokazuje założenia przy każdej liczbie i pozwala nadpisać każde wejście, z
zapisanym pochodzeniem.

## Fizyka (kontrakt)

```text
E_f^q(E_F) = E_def^q − E_bulk − Σ_i n_i μ_i + q (E_VBM + E_F) + E_corr^q
```

- `E_def`, `E_bulk`, referencje μ: ostatnie `free  energy   TOTEN` z OUTCAR (decyzja
  użytkownika; to samo, co `grep 'free  en' OUTCAR | tail -1`). Obecny loader już to daje:
  `Outcar.etot` (`outcar.py:97,600`), `vasp_output_load.py:53`. Ta sama konwencja dla wszystkich
  wierszy. Wiersz nieskończony lub bez zbieżności dostaje status błędu, nie liczbę
  (`output.py:483` ma informację o zbieżności, loader jej jeszcze nie eksportuje).
- `n_i` = liczba atomów i w defekcie − w bulk (wakans: n = −1, więc +μ).
- `E_VBM`, `E_g`: z obliczenia bulk reference projektu (`ProjectManifest::bulkDirectory`,
  `ProjectManifestIO.hpp:25`), domyślnie HOMO/LUMO z `vasp_output_load.py`. Nadpisywalne z
  pochodzeniem. Nie wyrównywać VBM dodatkowo do defektu, bo wyrównanie potencjału siedzi już w
  `E_corr` (FNV). Podmiana `E_g` zmienia tylko zakres E_F na wykresie, a nie energie.
- `E_F` liczone od VBM, zakres [0, E_g].
- `E_corr^q`: MVP to wartość wpisana ręcznie (np. z sxdefectalign) ze znacznikiem pochodzenia;
  później wynik modułu FNV. Dla q = 0 wynosi 0.
- Poziom przejścia: `ε(q/q') = (A_q − A_q') / (q' − q)`, `A_q = E_f^q(0)`. Stabilne są tylko
  przecięcia na dolnej obwiedni, a metastabilne pokazywane osobno. μ skraca się w ramach jednego
  składu. Eksport jako dokładne przecięcia, nie z próbkowanej krzywej. Brak obliczenia dla danego
  q to „brak danych”, nie „niestabilny”.
- Kilka konfiguracji (spin/geometria) tego samego (defekt, q) jest dozwolonych: obwiednia bierze
  minimum, tabela pokazuje wszystkie. Nie jest to wymagane, bo w inżynierce nie sprawdzano
  konkurencyjnych stanów.

Arytmetyka w C++ (`Domain/Defects/DefectEnergetics.{hpp,cpp}`): czyste funkcje + testy
jednostkowe. Python tylko czyta pliki.

## Energie wiązania: reakcje i łańcuchy

Reakcja = produkt (kompleks) + lista składników ze współczynnikami. Energia wiązania (dodatnia =
związany):

```text
E_b = Σ_k c_k E_f(składnik_k) − E_f(kompleks)
```

- **Bilans składu sprawdzany automatycznie:** `n_kompleks,i = Σ_k c_k n_k,i` dla każdego
  pierwiastka.
  - Przy niezbilansowanej reakcji aplikacja proponuje brakujący składnik: różnicę składu, np.
    GeN2V − GeN2 = V, albo GeNV − GeV = N_s. Zgłasza też, czy taki defekt jest w tabeli obliczeń.
  - Nie da się zapisać reakcji niezbilansowanej (wtedy μ się nie skracają).
- **Łańcuchy** to skrót do budowania kolejnych reakcji krokowych, np. GeV → GeNV → GeN2V daje
  GeV + N_s → GeNV oraz GeNV + N_s → GeN2V. Partner każdego kroku wynika z różnicy składu.
  Krok bez obliczenia partnera ma status „brak referencji”.
- **Warianty ładunkowe — oba liczone, oba do pokazania:**
  - „rezerwuar elektronów”: każdy człon w najniższym stanie przy danym E_F. Krzywa E_b(E_F) z
    załamaniami; przy krzywej widać wybrane q każdego członu;
  - „ustalone ładunki”: użytkownik wybiera q każdego członu (domyślnie bilans
    `q_kompleks = Σ c_k q_k`; niezbilansowane dozwolone, ale oznaczone, bo wtedy E_b zależy od E_F).
- **UI (ImPlot):**
  - lista reakcji/łańcuchów z checkboxami; zaznaczone rysowane jako E_b(E_F);
  - przesuwana pionowa linia E_F (`ImPlot::DragLineX`) z tabelą wartości w tym punkcie;
  - dla łańcucha wykres krokowy (E_b kolejnych kroków przy wybranym E_F);
  - dodawanie reakcji: wybór kompleksu z listy defektów, potem składników (lista + współczynnik),
    a aplikacja podpowiada brakujący człon.

## Potencjały chemiczne

Trzy źródła na każdy pierwiastek, do wyboru w UI:

1. **Folder potencjałów chemicznych:** ustawienie projektu wskazujące katalog z obliczeniami
   referencyjnymi (podkatalogi, np. `diamond/`, `Si/`, `Ge/`, `N2/`). Aplikacja czyta E_tot i
   skład, przypisuje obliczenie pierwiastkowe do pierwiastka i liczy `μ = E_tot / N_atomów` (N₂:
   E/2).
2. **Ręcznie wskazane obliczenie** dla danego pierwiastka.
3. **Wartość wpisana ręcznie** (eV/atom) z opisem pochodzenia.

Jeden aktywny zestaw μ w MVP. Nazwane zestawy (rich/poor) i diagram stabilności faz dopiero po
potrzebie. Dla poziomów przejść i zbilansowanych energii wiązania μ się skraca, więc te wyniki są
dostępne nawet bez μ.

## Co już jest (reużyć)

| Potrzeba | Istniejący element |
|---|---|
| `E_tot`, `NELECT`, HOMO/LUMO | `vasp_output_load.py:53,69,29` (`final_energy`, `nelect`, `homo/lumo`), parsowane w `VaspOutputBridge.cpp:50,58,87`; wołać z `includeOrbitals=false`. Krawędzie pasm są dziś `float`, energie `double` |
| Bulk reference | `ProjectManifest::bulkDirectory` (`ProjectManifestIO.hpp:25`) |
| `q` z liczby elektronów | puntukas `vasp/input.py:324,336` (walencje POTCAR; helper liczby neutralnej jest prywatny). `q = Σ N_s·ZVAL_s − NELECT` z **wyjściowego** NELECT (`output.py:321`), nie z INCAR. Brak POTCAR oznacza nieznane q (do wpisania), nie q = 0 |
| Skład (`n_i`) | miejsca z `PuntukasBridge.cpp:67`; zliczyć pierwiastki samemu (zredukowana formuła gubi krotność supercell). Bulk musi być tą samą supercell, tą samą metodą i tymi samymi PAW, inaczej wiersz jest odrzucany |
| Wykresy | ImPlot: `CalculationSummaryPanel.cpp:347`, `OccupationDiagramPanel.cpp:66` |
| Eksport CSV | przepływ `ExportOrbitalsCsv` (`ElectronicStructureSession.cpp:556`), ale z cytowaniem stringów, pełną precyzją i obsługą błędu zapisu |
| Długie operacje | `JobSystem` + `ScriptRunner` (`ScriptRunner.cpp:68` przekazuje anulowanie). `VaspOutputJob.cpp:25` go nie podłącza: naprawić we wspólnej ścieżce |

## Jedna analiza, jeden panel

FNV i energie dzielą te same wiersze (katalog obliczenia, defekt, `q`), więc jedna analiza
„Energetyka defektów” z kartami w jednym panelu:

| Karta | Zawartość |
|---|---|
| Obliczenia | wiersze: katalog, etykieta defektu, q (wykryte z NELECT, edytowalne), E_tot, status; bulk reference; źródło VBM/E_g |
| Poprawka | `E_corr` z FNV (półautomatyczny wybór plateau, patrz recenzja FNV) albo ręcznie, z pochodzeniem |
| Potencjały chemiczne | źródło μ dla każdego pierwiastka (folder / obliczenie / liczba) |
| Tworzenie | wykres E_f(E_F) z obwiedniami, tabela poziomów przejść |
| Wiązanie | lista reakcji/łańcuchów, wykres E_b(E_F), tabela przy wybranym E_F |

## Eksport

CSV lub TSV (wybór separatora). Pierwszy wiersz to nazwy kolumn z jednostkami w nawiasach
(`E_F (eV)`), więc import w Origin działa bez filtra. Trzy wiersze Long Name/Units/Comments są
opcjonalne: niezweryfikowane, czy Origin 2024 rozpoznaje je automatycznie, do sprawdzenia. Pliki:

- `formation_energies`: E_F i po jednej kolumnie na (defekt, q);
- `envelopes`;
- `transition_levels`;
- `binding_energies`: E_F i po jednej kolumnie na reakcję i wariant;
- `inputs`: provenance (E_tot, q, E_corr, μ, VBM, E_g, źródła).

`.opju` przez `originpro`: nie planowane (użytkownik: CSV/TSV wystarczy).

## Zapis

Konfiguracja analizy w jednym `analyses/energetics/<id>.json` w katalogu projektu: wiersze ze
ścieżkami względnymi, źródła μ, reakcje i łańcuchy, nadpisania z pochodzeniem oraz ostatnie
wyniki. Ten sam plik trzyma wyniki FNV. Bez cache: arytmetyka natychmiastowa. Każdy wiersz trzyma
snapshot źródła (ścieżka, mtime, rozmiar, odczytane liczby), więc po zmianie pliku wynik dostaje
status „nieaktualny”. Zapis atomowy (plik tymczasowy + rename) trzeba dodać, bo `TextFileIO`
nadpisuje plik w miejscu.

## Kolejność

1. `DefectEnergetics` (C++), czyste funkcje z testami:
   - E_f(E_F);
   - obwiednia i poziomy przejść (pominięty stan ładunkowy, metastabilne przecięcie, kilka
     konfiguracji jednego q);
   - bilans składu i podpowiedź brakującego członu;
   - E_b w obu wariantach ładunkowych;
   - rozwinięcie łańcucha w reakcje.
2. Odczyt wierszy (E_tot, NELECT, q, skład, zbieżność) i folderu μ; job z anulowaniem.
3. Panel: karty Obliczenia, Potencjały chemiczne, Tworzenie; eksport CSV/TSV.
4. Karta Wiązanie (reakcje, łańcuchy, wykresy).
5. Walidacja na liczbach z inżynierki.

## Poza zakresem (na razie)

- koncentracje defektów i samouzgodniony poziom Fermiego;
- nazwane zestawy μ i diagram stabilności faz;
- `.opju`;
- poprawki 2D/anizotropowe (decyzja promotora).

## Decyzje użytkownika (2026-10-06)

- Energia: ostatnie `free  energy   TOTEN` z OUTCAR.
- Energie wiązania: konfigurowalne reakcje i łańcuchy, lista do wyboru i wykresy ImPlot.
- Warianty ładunkowe: oba (najniższy stan przy E_F i ustalone ładunki); nie zamykać użytkownika,
  wynik to model, nie przesądzenie fizyki.
- μ: folder z obliczeniami referencyjnymi, ręczne wskazanie obliczenia albo wpisanie wartości.
- VBM i przerwa: z obliczenia czystego materiału (bulk reference projektu).
- Eksport: CSV albo TSV.
- Konkurencyjne stany spin/geometria nie były sprawdzane (obsługa opcjonalna).
- Tolerancja zgodności z inżynierką: do ustalenia przy teście.

Recenzja codex (2026-10-06) skorygowała bilans reakcji dla E_b, konwencję VBM/FNV, konwencję
energii, import nagłówków w Origin i odwołania file:line w tabeli reużycia.
