# Plan sesji: Terminal, iPython-kalkulator, podsumowanie obliczeń, fonony, group theory labels, displacement

> Branch: `task/16-calc-tools` (przemianowany z `task/16-python-scripting` 2026-08-24 — zakres się
> zmienił: pełny epik Python scripting/`ds`/IPC z `2026-08-23-python-scripting-console.md` **zostaje
> odłożony** (użytkownik: "duże, trudne, wolałbym nie zepsuć projektu"), ale dwa jego fundamenty
> (terminal PS, `InteractiveProcess`) są potrzebne TERAZ dla mniejszego, dobrze odgraniczonego
> zestawu funkcji poniżej). Ten plik istnieje po to, żeby nowy chat (bez historii tej rozmowy) mógł
> wejść w projekt i w tę konkretną partię pracy bez doczytywania 3/4 projektu.
>
> **Nie mylić z `2026-08-23-python-scripting-console.md`** — tamten plan (embedded/IPC `ds` moduł,
> `.ipynb`, hot-reload skryptów wchodzących w scenę) jest **zamrożony**, nie częścią tej partii.
> Konsola Python tutaj to **wyłącznie kalkulator** (patrz sekcja 4) — zero bindingu do sceny/ECS.

## Orientacja w projekcie (dla nowego chata)

Defect Studio to C++23 klon VESTY do wizualizacji struktur krystalicznych i defektów (DFT/VASP).
Punkty zaczepienia specyficzne dla TEJ partii pracy:

- **`punktukas-tools`** (prywatne repo, **NIE vendorować/commitować**, `C:\Users\fzabi\punktukas-
  tools\puntukas_tools\puntukas\` lokalnie na tej maszynie do referencji przy dalszej pracy) —
  **sprawdzone bezpośrednio w źródle w tej sesji** (nie zgadywane), zob. konkretne API cytowane w
  każdej sekcji niżej. Zasada z pamięci: appka pisze cienkie subprocess-bridge'e wołające
  `punktukas`, nigdy nie reimplementuje parsowania/fizyki, którą `punktukas` już ma.
- **Istniejące bridge'e** (`src/ScientificRuntime/Python/`): `VaspOutputBridge`/`VaspOutputJob`
  (band gap, orbitale), `VaspOrbitalGridBridge`/`Job` (WAVECAR real-space grid), wzorzec do
  skopiowania dla nowych bridge'ów w tej partii (zob. `scripts/python/examples/vasp_output_load.py`
  — subprocess, `json.dumps` na stdout, `try/except ImportError` na `puntukas`).
- **Panele struktury elektronowej:** `ElectronicStructurePanel.cpp` (tabela bandów, klik przez
  zwykły `ImGui::Selectable` → dispatch `VaspOrbitalGridJob`) i `OccupationDiagramPanel.cpp`
  (diagram VB/CB — **rysowany ręcznie przez `ImPlot::GetPlotDrawList()->AddLine/AddText`, NIE przez
  `ImPlot::PlotLine`/`PlotScatter`** — ważne dla sekcji 6, hit-test na kliknięcie musi być ręczny).
- **`Domain/Electronic/ElectronicStructureModel.hpp`** — `OrbitalRecord` **już ma pole
  `std::optional<std::string> irrep`** — niewypełniane i niewyświetlane dziś nigdzie (zob. sekcja 7).
- **`Core/Platform/ProcessRunner`** — blokujący, jeden wynik, **brak** procesu długożyjącego z
  ciągłym I/O. Ten sam brak blokuje i terminal PS (sekcja 3), i konsolę (sekcja 4) — zob. sekcja 2,
  nowy wspólny prymityw `InteractiveProcess` (przeniesiony z zamrożonego planu Python, sekcja 4
  tamtego pliku, bez reszty tamtego epiku).
- **MSDF labels** (T09, już działają dla etykiet długości wiązań/kątów, `RendererPanel.cpp`
  `handlePinnedMeasurementInteraction` i okolice) — reużywalny mechanizm dla etykiet group-theory
  w 3D (sekcja 7).
- **`ProjectTreePanel::renderDirectoryContextMenu`** — istniejący wzorzec RMB-na-folderze (np.
  "Set as Bulk Reference", T07.5.5) — ten sam wzorzec dla "Show Calculation Summary" (sekcja 5).
- **Build**: pełny Debug + Release + `DefectStudioTests` po każdym nietrywialnym kroku. Nowe `.cpp`
  wymagają `python -m scripts.python.generate_projects` przed buildem.

---

## 1. Zakres (z wiadomości użytkownika, 2026-08-24)

1. Integrated PowerShell terminal — do łatwego logowania na serwer.
2. iPython w bardzo okrojonej wersji — "kalkulator", nic więcej.
3. **Nowy koncept: "Calculation Summary"** — panel per katalog: zbieżność, energia, wykres
   zbieżności. Ma sam zaproponować co jeszcze dodać na bazie OUTCAR/`punktukas`.
4. Fonony: S (Huang-Rhys), DW (Debye-Waller), średnie ω, średnia energia fononów — sprawdzić
   `punktukas` **przed** użyciem starego skryptu użytkownika. Użytkownik wybiera 2 pliki wejściowe.
5. Klik na poziom w `OccupationDiagramPanel` (nie tylko w tabeli `ElectronicStructurePanel`) też
   wybiera wyświetlany orbital.
6. Group-theory transformation labels (irrep) w `OccupationDiagramPanel`, `ElectronicStructurePanel`
   i w rendererze 3D — konfigurowalne (on/off, dokładność), użytkownik może dopisać własny label
   obok automatycznego (np. `\pi*` obok `b_1*`).
7. Serializacja tych danych do pliku pod Origin + eksport obrazka robionego w Pythonie.
8. Eksport obrazków Python dla `OccupationDiagramPanel` (matplotlib).
9. Atoms-displacement (strzałki przemieszczenia atomów między dwiema strukturami).

Punkty 7/8 **pokrywają się 1:1 z dwoma już zanotowanymi, nigdy nie zrobionymi pozycjami w
`TODO.md`** (T08.6.3 "Eksport stanów elektronowych... CSV/TSV" i T08.6.6 "Matplotlib export
skrypt") — ten plan je domyka, nie duplikuje.

---

## 2. Wspólny fundament: `InteractiveProcess`

Przeniesione z zamrożonego planu (`2026-08-23-python-scripting-console.md`, sekcja 4) — **tylko
ten jeden kawałek**, reszta tamtego pliku nadal zamrożona.

Nowy typ `Core/Platform/InteractiveProcess` (siostrzany do `ProcessRunner`, nie zamiennik):
- Windows: `CreatePipe` (stdin + stdout/stderr) + `CreateProcess` z redirected handles.
- Odczyt nieblokujący: wątek czytający z pipe'a do thread-safe kolejki linii, panel odpytuje co
  klatkę (bez `JobContext` — to nie jest jednorazowe zadanie z wynikiem końcowym).
- `WriteLine(std::string)` do stdin. `Terminate()`/destruktor zamyka pipe'y.

**Reużywane przez sekcję 3 (terminal) i sekcję 4 (konsola)** — budować raz.

---

## 3. Integrated PowerShell terminal

- Dockowalny panel (`IPanel`, wzorzec `TextEditorPanel`) nad `InteractiveProcess` odpalającym
  `powershell.exe -NoLogo -NoExit`.
- **Cel użytkownika: łatwe logowanie na serwer** — to znaczy zwykłe `ssh user@host` wpisane w tym
  panelu (Windows ma OpenSSH client wbudowany od 1809+) albo komenda mount/sshfs-win z T07.5.2 —
  panel jest ogólnym shellem, appka nie musi nic wiedzieć o "serwerach" na tym poziomie (to inna
  warstwa, T07.5.2 Server Profiles, osobna, nieblokująca tego panelu).
- **MVP:** surowy tekst, scrollback, brak prawdziwej emulacji terminala (brak kolorów/ANSI) —
  akceptowalne ograniczenie, PowerShell nadal używalny do prostych komend/loginu.
- **Stretch:** ConPTY (`CreatePseudoConsole`) dla prawdziwych kolorów/formatowania — osobna,
  bardziej złożona ścieżka Win32, nie blokuje MVP.

---

## 4. Konsola Python — **wyłącznie kalkulator**

**Świadomie najmniejszy możliwy zakres** — użytkownik wprost: "to czego potrzebuję to móc
przeprowadzać obliczenia, trochę jak za pomocą kalkulatora". Zero `ds` modułu, zero bindingu do
sceny/ECS, zero IPC do żywej appki — to jest **dokładnie ten sam mechanizm co terminal PS
(sekcja 3), tylko odpala inny proces**.

- Panel nad `InteractiveProcess` odpalającym `ipython` (jeśli jest na PATH — lepszy UX za darmo:
  `?`/`??`/magic commands appka dostaje bez pisania własnego kodu) albo `python -i` (fallback).
- Użytkownik może w tej sesji zrobić zwykłe `import puntukas`/`from puntukas.vasp import
  VaspOutput` samodzielnie — to już działa, appka nie musi nic specjalnego robić żeby to umożliwić
  (venv z `puntukas` już istnieje, zob. `.venv/Lib/site-packages/puntukas-0.1.0.dist-info`).
- **Input UI:** jedna linia + historia (strzałki góra/dół) na start. Multi-line dla bloków
  (`if`/`for`/`def`) — wykrywanie niekompletnego bloku przez `codeop.compile_command` (stdlib,
  dokładnie do tego służy — nie pisać własnego parsera).
- **Brak na start:** autocomplete (stretch, `TrieAutoComplete` z vendorowanego forka edytora, zob.
  zamrożony plan sekcja 5.2/5.5 jeśli kiedyś wróci), zero perswazji w stronę pełnego `ds`.

---

## 5. "Calculation Summary" — nowy koncept, panel per katalog

**Nowy panel** (`CalculationSummaryPanel`, wzorzec `ElectronicStructurePanel`), otwierany przez RMB
na folderze w `ProjectTreePanel` ("Show Calculation Summary" — ten sam wzorzec co "Set as Bulk
Reference", T07.5.5) albo przez otwarcie wprost ze ścieżki.

### Co dokładnie pokazuje (zweryfikowane w źródle `punktukas`, nie zgadywane)

- **Zbieżność:** `puntukas.vasp.vasprun.Vasprun` parsuje `<energy>` per `<calculation>` blok
  (`vasprun.py:387-401`, `_parse_etot`) do **`self._etot` — listy tablic: zewnętrzna = kroki
  jonowe, wewnętrzna = iteracje SCF w danym kroku.** To jest dokładnie dana potrzebna do "wykresu
  jak zbiegały obliczenia" — **ale `_etot` jest dziś prywatnym atrybutem, nie publicznym API**
  (`Vasprun.etot` property zwraca tylko `self._etot[-1][-1]`, ostatnią wartość). Nasz skrypt może
  czytać `vasprun._etot` bezpośrednio (Python nie ma prawdziwej prywatności) — **świadomy,
  udokumentowany coupling na nieoficjalne API `punktukas`, do zaakceptowania albo do naprawienia
  upstream (dodać publiczną property) jeśli chcemy być mili dla autora repo.**
- **Energia:** `VaspOutput(dir).etot` (deleguje do `Outcar.etot`, regex na "free energy TOTEN").
- **Wykres zbieżności:** oś X = numer kroku jonowego (albo elektronowego w ostatnim kroku), oś Y =
  energia — `ImPlot` (już zvendorowany i używany w `OccupationDiagramPanel`).

### Propozycja dodatkowych pól (na żądanie użytkownika, z tego co `punktukas`/OUTCAR faktycznie ma)

- **Zbieżność sił:** `Vasprun.force_max()`/`max_force` — max |siła| per krok jonowy vs `EDIFFG`
  (dziś `EDIFFG` NIE jest w `vasprun_attributes` w `punktukas` — trzeba by doczytać je samemu z
  `vasprun.xml`/OUTCAR albo zignorować i pokazać tylko trend bez progu).
  **Uwaga: `Vasprun._parse_forces` też bierze tylko `[-1]` (ostatni krok)** — pełna historia sił
  per krok wymaga tej samej techniki co dla `_etot` (`findAll` zamiast `[-1]`), do zrobienia w
  naszym własnym skrypcie, nie w `punktukas`.
- **Band gap:** `VaspOutput.bandgap`/`.vasprun.homo`/`.lumo` — już użyte przez `VaspOutputBridge`,
  reużyć wprost.
- **CPU/user/system/elapsed time:** `Outcar.cpu_time/user_time/system_time/elapsed_time` — gotowe.
- **Total drift:** `Outcar.total_drift`/`VaspOutput` — dobry sanity-check (duży drift = podejrzana
  relaksacja), gotowe.
- **Magnetyzacja/liczba elektronów:** `Vasprun.NELECT`, `ISPIN` — gotowe z `vasprun_attributes`.
- **Ciśnienie/stress:** `Vasprun.get_pressure()`/`get_stress_tensor()` — gotowe.
- **Symetria/grupa przestrzenna:** `puntukas.Symmetry(atoms)` (użyte już wewnątrz `Vasprun.
  find_equivalent_by_symmetry_kpoints`) — dodatkowy, tani insight o wynikowej strukturze.

**Nowy skrypt** (`scripts/python/examples/calculation_summary_load.py`, wzorzec
`vasp_output_load.py`) zwracający JSON z powyższym; nowy C++ bridge (`CalculationSummaryBridge`) +
`Job` + panel. **Nie duplikować** logiki `VaspOutputBridge` — ten nowy bridge to superset (dodaje
historię zbieżności), rozważyć czy `VaspOutputBridge` powinien zostać rozszerzony zamiast
tworzenia równoległego bytu (decyzja przy starcie implementacji, nie tutaj).

---

## 6. Fonony: S, DW, średnie ω, średnia energia — **ODŁOŻONE NA KONIEC (2026-08-24)**

> **Decyzja użytkownika 2026-08-24: to zaczekaj.** Użytkownik prawdopodobnie dostanie coś od
> Lukasa (autora `punktukas-tools`) w sprawie `dephonopy` — porównanie niżej jest informacyjne,
> **nie blokuje reszty tego planu i celowo idzie na sam koniec kolejności (sekcja 11)**.

### Porównanie trzech kandydatów (sprawdzone 2026-08-24, poziom pewności zaznaczony przy każdym)

| | **`dephonopy`** | **PyPhotonics** | **Lumabi** | Twój stary skrypt |
|---|---|---|---|---|
| Status | Prywatny, **nieznaleziony** publicznie ani lokalnie | **Publiczny, ugruntowany** (Comp. Phys. Comm. 2022) | **Publiczny, świeży** (JOSS 2026) | Nieznany appce (nigdy nie pokazany) |
| Instalacja | Nieznana (nie na PyPI pod tą nazwą) | `pip install pyphotonics` (potwierdzone) | Repo rozwiązuje się do `github.com/abinit/abipy` — **moduł wewnątrz AbiPy**, nie osobny pakiet | — |
| Ekosystem DFT | Nieznany, ale **`punktukas` (VASP-owe narzędzie) samo po niego sięga** — silna poszlaka że jest VASP-friendly | **VASP natywnie** (`CONTCAR_GS`/`CONTCAR_ES` + phonopy `bands.yaml`) — pasuje 1:1 do tego czym appka już operuje | **AbiPy/ABINIT-centryczny** (workflow `LumiWork` automatyzuje zadania ABINIT DFT) — **ryzyko: może nie przyjmować VASP-a wprost**, niepotwierdzone, mogłoby wymagać konwersji formatu |
| Co liczy (potwierdzone) | Nieznane (`atomic_displacements` w `hr_factors.io` — sama nazwa sugeruje przemieszczenia atomowe jako wejście do HR, spójne z podejściem 1D config-coordinate) | **Huang-Rhys factor, PL lineshape** (DW i średnie ω nie wymienione wprost w README — prawdopodobnie liczone wewnętrznie jako krok pośredni, do potwierdzenia w kodzie/dokumentacji przy realnej integracji) | Transition energies, Huang-Rhys factors, **effective phonon frequencies**, lineshapes (z papieru JOSS, PDF nieczytelny do dalszych szczegółów) | Nieznane |
| Wejście | Nieznane | **Dokładnie 2 pliki + jeden phonopy YAML** — pasuje 1:1 do życzenia użytkownika "dwa pliki do wyboru" | Nieznane precyzyjnie (prawdopodobnie phonopy force-constants + dwie geometrie, typowe dla tej klasy metod) | Nieznane |
| Licencja | Nieznana | MIT (repo) | CC-BY-4.0 (papier JOSS; licencja samego kodu w AbiPy osobno, nieco inna sprawa) | — |

**Wstępna rekomendacja (do potwierdzenia, nie ostateczna decyzja):** **PyPhotonics** wygląda na
najlepiej dopasowany praktycznie — jawnie VASP-natywny, dokładnie te same 2 pliki + phonopy
`bands.yaml` czego appka i użytkownik już oczekują, publiczny i pip-installable, więc łatwy do
zweryfikowania samemu zanim padnie ostateczna decyzja. `Lumabi` ma realne ryzyko integracyjne
(ekosystem ABINIT, nie VASP). `dephonopy` zostaje czarną skrzynką dopóki nie przyjdzie odpowiedź
od Lukasa — możliwe że to jest dokładnie ten sam rodzaj narzędzia co PyPhotonics, tylko
wewnętrzne/nieopublikowane, albo że robi coś innego/lepszego skoro `punktukas` po niego sięga.
**Nie podejmować decyzji bez tej odpowiedzi** — użytkownik świadomie to odłożył.

**UI, niezależnie od backendu:** mały panel/popup z **dwoma pickerami plików** (użytkownik wybiera
dokładnie które 2 pliki/foldery wchodzą do obliczenia — zgodnie z wyraźnym życzeniem: "dać
użytkownikowi możliwość jakie dwa pliki program ma wziąć do obliczeń"), przycisk "Compute", wynik
(S, DW, ω̄, Ē) w prostej tabelce. **Synergia z sekcją 8 (displacement):** oba mechanizmy
fundamentalnie potrzebują "przemieszczenie atom-po-atomie między dwiema strukturami o tym samym
składzie" — jeśli config-coordinate ΔQ liczone jest z tych samych dwóch plików co displacement
arrows, warto dzielić kod obliczania przemieszczeń między obiema funkcjami (do potwierdzenia gdy
wybrany backend fononowy pokaże dokładnie czego oczekuje jako wejścia).

---

## 7. Klik na poziom w `OccupationDiagramPanel`

Dziś **tylko** `ElectronicStructurePanel`'s tabela (`ImGui::Selectable`, linia 106) dispatchuje
`VaspOrbitalGridJob`. `OccupationDiagramPanel` rysuje bandy jako gołe `ImDrawList::AddLine` wewnątrz
`ImPlot::GetPlotDrawList()` (**nie** `ImPlot::PlotLine`/`PlotScatter` — więc **nie ma** wbudowanego
ImPlot hit-testu do reużycia).

**Do zrobienia:** ręczny hit-test, analogiczny do bond-picking z tej sesji
(`SelectionHitTest.cpp`) — `ImPlot::GetPlotMousePos()` żeby dostać pozycję myszy we
współrzędnych wykresu (energia/X), sprawdzić odległość do najbliższego narysowanego segmentu
poziomu (ten sam X-zakres co linia, energia najbliższa Y), próg w pikselach przeliczony na
jednostki wykresu. Klik → ten sam dispatch co `ElectronicStructurePanel` (**reużyć**, nie
duplikować — wydzielić `dispatchOrbitalForBand(...)` jeśli dziś jest wklejone bezpośrednio w
`ElectronicStructurePanel::Render`).

---

## 8. Group-theory (irrep) labels — 3 miejsca, konfigurowalne, z custom labelami

### Co już istnieje (nie wynajdywać)

- **`OrbitalRecord::irrep`** (`ElectronicStructureModel.hpp:16`) — pole już jest, `std::optional
  <std::string>`, dziś zawsze puste (nic go nie wypełnia).
- **`get_orbital_data_for_two_spins(self, start, end, ikpt=0, irreps=False, irrep_tol=1e-1,
  symprec=1e-3)`** (`puntukas/vasp/output.py:189`) — **`punktukas` już liczy irrepy, z dwoma
  gotowymi parametrami dokładności** (`irrep_tol`, `symprec`) — dokładnie to o co prosi
  użytkownik ("można włączyć, wyłączyć, zmienić dokładność"). Dziś `vasp_orbital_grid_load.py`
  (nasz skrypt) hardkoduje `irreps=False` z powodu kosztu wydajnościowego dla szerokich zakresów
  bandów (udokumentowany komentarz w kodzie) — **zmienić na parametr przekazywany z UI**, nie
  domyślnie zawsze włączone (koszt realny, potwierdzony w kodzie).

### Co nowe

1. **Ustawienia (Settings albo bezpośrednio w `ElectronicStructurePanel`):** checkbox "Show
   symmetry labels" (`irreps`), dwa floaty `irrep_tol`/`symprec` — przekazywane 1:1 do bridge'a.
2. **Wyświetlanie w `ElectronicStructurePanel` i `OccupationDiagramPanel`:** irrep string obok
   energii bandu w tabeli/przy linii poziomu.
3. **W rendererze 3D:** MSDF label (reużyty mechanizm z T09/pomiarów) przy/na izopowierzchni
   orbitalu — pokazuje ten sam string.
4. **Custom label mapping — nowość, nie ma odpowiednika dziś.** Mała, edytowalna tabela
   (irrep → własny label użytkownika, np. `"b_1*" → "\pi*"`), **wyświetlana OBOK automatycznego
   labela, nie zamiast** (dosłowne życzenie: "powinno być obok tego b_1* b_1 itd"). Persystencja:
   nowy mały plik (wzorzec `AtomStyleIO`/`panel_visibility.txt` — prosty, dedykowany format, nie
   przez `YamlConfigSerializer`), per-projekt albo per-structure (do ustalenia — prawdopodobnie
   per-projekt, bo grupa punktowa/nazewnictwo irrepów jest własnością materiału/defektu, nie
   pojedynczej struktury w oknie).

---

## 9. Serializacja pod Origin + eksport obrazków Python

**To są dwie już zanotowane, nigdy nie zrobione pozycje `TODO.md`** (T08.6.3/T08.6.6) — plan je
domyka wprost, teraz z dodatkiem irrep labeli z sekcji 8.

- **Eksport CSV/TSV** ("przyjazny Originowi" = zwykły, płaski, dobrze nagłówkowany tekst, tab albo
  przecinek) — band index, energia, occupation, localization, irrep (jeśli włączone), per kanał
  spinowy. Prosty `TextFileIO::Save`-owy zapis, zero nowej zależności.
- **Eksport obrazka Python (matplotlib)** — nowy skrypt (`scripts/python/examples/
  electronic_structure_plot.py`, już nazwany w `TODO.md` T08.6.6), rysujący occupation diagram
  1:1 ze stylem `OccupationDiagramPanel` (te same kolory/strzałki) **plus teraz irrep labels przy
  poziomach** — subprocess wywołanie z C++ (wzorzec `ProcessRunner`, nie `InteractiveProcess` —
  to jednorazowe zadanie z wynikiem, nie sesja).

---

## 10. Atoms-displacement — **PEŁNE WSPARCIE (decyzja użytkownika 2026-08-24), nie tylko MVP**

**Otwarta decyzja z poprzedniego planu (`2026-08-23-outliner-bonds-displacement.md`) rozstrzygnięta
2026-08-24: wspieramy CAŁOŚĆ, w tym różny skład (V_2 vs V_2CBCN).** Sprawdzone w źródle `punktukas`
(`atoms/base.py`) — **istnieje tylko `get_distances(p1, p2)` (znane indeksy) i `get_all_distances()`
(macierz wewnątrz jednej struktury). Zero atom-matchera między dwiema RÓŻNYMI strukturami** — to
znaczy dopasowanie atomów **piszemy sami, świadomie, bo zweryfikowaliśmy że nie ma tego gotowego**
(w przeciwieństwie do reszty tego planu, gdzie prawie wszystko jest gotowe w `punktukas`).

### Przypadek prosty (ten sam skład/liczba/kolejność atomów)

POSCAR vs własny zrelaksowany CONTCAR, albo dwa warianty tego samego defektu bez zmiany składu —
przemieszczenie per-atom to trywialna, per-indeksowa różnica pozycji (minimum-image, żeby uniknąć
artefaktów na granicy komórki periodycznej). Zero dopasowywania potrzebne, indeksy już się zgadzają.

### Przypadek ogólny (różny skład/liczba atomów) — algorytm do napisania

Wymaga **obie struktury na tej samej komórce/sieci** (inaczej "minimum-image displacement" nie ma
dobrze zdefiniowanego sensu — porównanie różnych superkomórek to inny, nie zaadresowany tu problem).

1. **Macierz odległości cross-structure** (B względem A), PBC-aware (minimum-image przez wektory
   sieci + zawijanie współrzędnych frakcyjnych — prosta algebra liniowa, nie wymaga `punktukas`,
   choć można reużyć `puntukas.Atoms`/`Cell` jeśli to wygodniejsze przy implementacji).
2. **Dopasowanie jeden-do-jednego jako optymalne przypisanie dwudzielne** (Hungarian algorithm,
   `scipy.optimize.linear_sum_assignment` — appka i tak przechodzi przez Python na tym etapie, więc
   to naturalne miejsce), **nie zachłanny najbliższy-sąsiad** — zachłanny łatwo tworzy "skrzyżowane"
   przypisania w gęstych regionach (atom X bierze najbliższego, zostawiając atomowi Y gorszy wybór
   niż istniał), optymalne przypisanie minimalizuje sumę przemieszczeń globalnie.
3. **Próg odcięcia** (max przemieszczenie, konfigurowalny przez użytkownika, w duchu bond-cutoff z
   `BondSettingsPanel`) — dopasowanie powyżej progu odrzucone jako przypadkowe, nie prawdziwa
   korespondencja.
4. **Species:** domyślnie dopasowanie **tego samego pierwiastka** (osobna macierz/przypisanie per
   gatunek) — dopuszczenie dopasowań międzygatunkowych (np. śledzenie podstawienia C→B) jako opcja,
   nie domyślne zachowanie (do potwierdzenia, zob. pytania niżej).
5. **Niedopasowane atomy:** obecne w A, brak w B → **wakancja** (reużyć istniejący koncept
   "widmowego" renderowania z `AtomStyleTable`/`VacancyRenderStyle`, wspomniany w poprzednim
   planie). Obecne w B, brak w A → **interstitial-like** marker (nowy atom bez pochodzenia).
6. **Wizualizacja:** reużycie istniejącego instancingu cylindrów wiązań (inny mesh na czubku — grot
   strzałki), jeden wektor per dopasowaną parę, kolor/długość ~ wielkość przemieszczenia.

**To jest świadomie nowy kawałek algorytmu, nie duplikacja czegoś co `punktukas` już ma** —
zweryfikowane w sekcji research tego planu, więc zgodne z zasadą "nie wynajdywać koła", nie
złamanie jej.

**UX wskazania plików:** osobny dialog z dwoma file-pickerami (reference + comparison), wzorzec NFD
już zvendorowany. Może dzielić UI z sekcją 6 (fonony) jeśli te dwie funkcje faktycznie chcą tych
samych dwóch plików wejściowych — do potwierdzenia przy implementacji (fonony i tak odłożone).

---

## 11. Proponowana kolejność

1. **`InteractiveProcess`** (sekcja 2) — fundament pod #2/#3.
2. **Terminal PS** (sekcja 3) — najmniej ryzykowne, czysty zysk, testuje #1.
3. **Konsola-kalkulator** (sekcja 4) — mały krok po #2, ten sam prymityw.
4. **Calculation Summary panel** (sekcja 5) — niezależne od 1-3, czysto nowy panel + bridge.
   Zawiera propozycję dodatkowych pól — ustalić z użytkownikiem finalny zestaw przed kodem.
5. **Klik w `OccupationDiagramPanel`** (sekcja 7) — mały, izolowany, niezależny od reszty.
6. **Irrep labels — plumbing + UI + 3 miejsca wyświetlania** (sekcja 8) — średni, ale dobrze
   odgraniczony (domyka istniejące, częściowo zbudowane API).
7. **Eksport CSV/TSV + matplotlib image** (sekcja 9) — po #6, żeby od razu zawierał irrep labels.
8. **Atoms-displacement, pełny zakres** (sekcja 10) — przypadek prosty (ten sam skład) może iść
   równolegle z resztą; przypadek ogólny (Hungarian assignment) jako drugi krok tego samego
   zadania, niezależne od 1-7.
9. **Fonony S/DW/ω̄/Ē** (sekcja 6) — **świadomie na końcu, czeka na odpowiedź od Lukasa
   (`dephonopy`)** — decyzja użytkownika 2026-08-24, nie techniczna blokada.

---

## 12. Lista pytań dla użytkownika (skonsolidowana 2026-08-24)

**Fonony (sekcja 6) — ŚWIADOMIE POMINIĘTE tutaj, czeka na odpowiedź od Lukasa. Nie odpowiadać
teraz.**

1. **Calculation Summary (sekcja 5) — czy lista pól jest OK, czy coś dodać/usunąć?**
   Proponowane: zbieżność (energia per krok jonowy+elektronowy), energia końcowa, zbieżność sił
   (max |F| per krok vs próg), band gap, CPU/user/system/elapsed time, total drift, magnetyzacja/
   NELECT, ciśnienie/stress, symetria/grupa przestrzenna.
2. **Calculation Summary — nowy bridge (`CalculationSummaryBridge`) czy rozszerzenie istniejącego
   `VaspOutputBridge`?** Rekomendacja: rozszerzenie (superset tej samej logiki), ale to zmienia
   istniejący, już używany kod — potwierdź czy to akceptowalne teraz czy wolisz osobny byt.
3. **Custom irrep label mapping (sekcja 8, pkt 4) — persystencja per-projekt czy per-structure?**
   Rekomendacja: per-projekt (nazewnictwo grupy punktowej to własność materiału/defektu, nie
   pojedynczego okna).
4. **Irrep labels w 3D (renderer) — gdzie dokładnie względem izopowierzchni orbitalu?** (obok,
   nad, z linią wskazującą jak istniejące pinned-measurement labele) — czysto wizualna decyzja,
   łatwo zmienić później, ale warto ustalić punkt odniesienia przed kodem.
5. **Displacement, przypadek ogólny (sekcja 10) — dopuszczać dopasowania międzygatunkowe
   (np. C→B podstawienie) domyślnie, czy tylko ten sam pierwiastek chyba że user włączy opcję?**
   Rekomendacja: domyślnie tylko ten sam pierwiastek, przełącznik do rozszerzenia.
6. **Displacement — domyślny próg odcięcia dopasowania** (jaka wartość ma sens fizyczny dla
   Twoich typowych struktur — ułamek stałej sieci? promień kowalencyjny × jakiś mnożnik, podobnie
   do bond cutoff?).
7. **Terminal PS (sekcja 3) — wystarczy zwykły `powershell.exe`, czy od razu chcesz PowerShell 7
   (`pwsh.exe`) jeśli jest zainstalowany?** Wpływa tylko na to, co panel próbuje odpalić najpierw.
8. **Konsola-kalkulator (sekcja 4) — czy masz `ipython` zainstalowany w venv appki, czy panel ma
   zakładać zwykły `python -i`?** (Sprawdzę sam przy implementacji, ale daj znać jeśli wiesz już
   teraz — oszczędzi jeden krok.)

---

## Weryfikacja przed merge do main

Jak w poprzednich planach: pełny Debug + Release build + `DefectStudioTests` po każdym
nietrywialnym kroku z sekcji 11. Nowe zależności Python (`Lumabi` i/lub `dephonopy` po decyzji z
pkt 1 wyżej) idą do `uv`-zarządzanego venv, nie system-wide.
