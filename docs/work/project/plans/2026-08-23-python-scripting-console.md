# Plan sesji: Python Scripting, Interactive Console, Notebooks, Terminal

> Branch: `task/16-python-scripting` (utworzony z `main` @ `b8ddc0d`, 2026-08-23). Ten plik
> istnieje po to, żeby nowy chat (bez historii tej rozmowy) mógł wejść w projekt i w tę konkretną
> partię pracy bez doczytywania całego `TODO.md`. `TODO.md` zostaje główną, żyjącą listą zadań
> całego projektu (zob. rozszerzony wpis "Python scripting panel" w sekcji Backlog, dopisany
> 2026-08-23, commit `b8ddc0d` — ten plan go rozwija w konkretną architekturę). Ten plik to
> jednorazowy plan implementacyjny dla jednej działki pracy.
>
> **Status: PLAN DO DYSKUSJI, nie zatwierdzona architektura.** Użytkownik wprost poprosił o plan +
> architekturę do przedstawienia przed kodem — sekcja 11 ("Otwarte decyzje") zawiera pytania, na
> które ten plik świadomie NIE odpowiada jednostronnie. Nie zaczynać implementacji przed
> rozstrzygnięciem sekcji 3 (fundamentalna decyzja architektoniczna) i 11.

## Orientacja w projekcie (dla nowego chata)

Defect Studio to C++23 klon VESTY do wizualizacji struktur krystalicznych i defektów (DFT/VASP).
Kluczowe punkty zaczepienia dla TEGO zadania konkretnie:

- **Python dziś = wyłącznie subprocess, jeden strzał.** `Core/Platform/ProcessRunner::RunProcess`
  (`src/Core/Platform/ProcessRunner.hpp`) jest **blokujący, run-to-completion, jeden wynik** — brak
  strumieniowego stdin/stdout, brak uchwytu do żywego procesu. Każdy istniejący "bridge"
  (`PymatgenBridge`, `ASEBridge`, `PuntukasBridge`, `VaspOutputBridge`, `VaspOrbitalGridBridge`,
  `OpenDefectJob` — wszystkie w `src/ScientificRuntime/Python/`) to: zbuduj argumenty → odpal
  skrypt → sparsuj JSON z stdout → zwróć `Result<T>`. **Nie ma dziś żadnego prymitywu do procesu
  długożyjącego z ciągłym I/O** — to jest wspólna, brakująca podstawa i dla terminala PS1, i dla
  konsoli Python (zob. sekcja 4).
- **Embedded CPython istnieje w kodzie, ale jest wyłączony.** `PythonInterpreter`
  (`src/ScientificRuntime/Python/PythonInterpreter.{hpp,cpp}`) to RAII wrapper na `Py_InitializeEx`
  pod `#if DS_PYTHON_CAPI_AVAILABLE`, ale ten flag jest **`0` w obu configach**
  (`premake5.lua:457,623`, potwierdzone w `TODO.md` "Hotfixy" — startup performance notatka).
  Dziś nic realnie nie woła `PythonInterpreter::Start()` w produkcyjnej ścieżce.
- **nanobind moduł już istnieje — ale jako pusta zabawka.** `src/ScientificRuntime/PythonBindings/
  DefectStudioPythonBridgeModule.cpp` — `NB_MODULE(ds_python_bridge, ...)` z `add()`,
  `hello_from_cpp()`, jednym toy-typem `BridgeVector3`. **Zero realnego bindingu do sceny/domeny.**
  To jest "fundament embeddingu przez nanobind" o którym mówi `TODO.md` — sam toolchain działa
  (kompiluje się, `tests/ScientificRuntime/BridgeRoundtripDemoTests.cpp` go odpala kiedy zbudowany),
  ale nazwa modułu, zawartość i sposób ładowania (embedded vs `.pyd` importowany przez zewnętrzny
  `python.exe`) są od zera.
- **Text editor (T12) już ma highlighting Pythona i Markdown "za darmo".** Vendorowany fork
  `Vendor/ImGuiColorTextEdit/TextEditor.h:618-628` ma gotowe `Language::Python()` i
  `Language::Markdown()` (plus C/C++/Lua/GLSL/JSON/SQL) jako statyczne fabryki — **`TextEditorPanel`
  (`src/Presentation/Panels/TextEditorPanel.{hpp,cpp}`) dziś ich nie woła w ogóle** (`SetLanguage()`
  nieużyte, potwierdzone w `TODO.md` T12). To znaczy: syntax highlighting dla `.py`/`.md` to
  **dosłownie dopisanie dispatchu po rozszerzeniu pliku do `SetLanguage(Language::Python())` /
  `SetLanguage(Language::Markdown())`**, nie pisanie nowego lexera.
- **Autocomplete: dwie gotowe, niekompilowane dziś ścieżki w tym samym forku.**
  `Vendor/ImGuiColorTextEdit/extras/TrieAutoComplete.{h,cpp}` — samodzielny, zero-dependency,
  autocomplete po identyfikatorach z aktualnego dokumentu (nie context-aware, ale działa od razu).
  `Vendor/ImGuiColorTextEdit/extras/LspBridge.{h,cpp}` — prawdziwy klient LSP (spawnuje serwer
  językowy jak `pylsp`, daje **prawdziwe** ipython-style completion/hover/signature) — **ale
  wymaga nowej zależności** (`#include "lsp/messagehandler.h"` itd. — osobna biblioteka klienta LSP
  tego samego autora, dziś nieobecna w `Vendor/`) **i** zainstalowanego serwera językowego Pythona
  po stronie użytkownika. Żadne z tych dwóch nie jest dziś wpięte w `premake5.lua` (tylko
  `TextEditor.cpp`/`.h` są kompilowane, `premake5.lua:304`).
- **`Core/Undo`/`UndoStack::PushExecuted`/`ScopedGroup`** (`src/Core/Undo/UndoStack.hpp`) — mutacje
  ze skryptu Python **mają** przechodzić przez to samo, budując te same `ICommand` co UI (wzorzec:
  `RendererAtomEditCommands.cpp`), niekoniecznie przez nowy mechanizm — zob. sekcja 6.
- **`ProjectWorkspace` (`src/Domain/ProjectWorkspace.hpp`) rejestry mają TYLKO `Add`/`Find(id)`.**
  `StructureRegistry`/`DefectRegistry`/`DefectConfigurationRegistry`/`CalculationRegistry` — zero
  wyszukiwania po nazwie/tagu, zero enumeracji ("daj mi wszystkie calculation records dla tego
  defektu"). `CalculationRecord` (`Domain/Defects/DefectModel.hpp`) ma `displayName`/`method`, ale
  **zero pola na ścieżkę na dysku / serwer** skąd wziąć `OUTCAR`. To jest realna, konkretna luka
  pod adresowanie `project.okeanos["6-7"]["exc_ms"]` z przykładu użytkownika — zob. sekcja 8.
- **`efsw` (proponowany w starym Backlogu do hot-reload) nie jest zvendorowany.** Zanim po niego
  sięgać — `std::filesystem::last_write_time` na pollingu przez `JobSystem` (już istnieje,
  `Core/JobSystem/JobSystemTypes.hpp:112` `IJob::Execute(JobContext&)`) prawdopodobnie wystarcza
  dla garstki obserwowanych plików skryptów (rung niżej na drabince ponytail — dopisać `efsw` tylko
  jeśli polling okaże się realnie za wolny/zbyt kosztowny CPU-wise).
- **Build**: `MSBuild build\generated\vs2022\DefectStudio.sln /t:DefectStudio
  /p:Configuration=Debug /p:Platform=x64` + `/t:DefectStudioTests` + uruchomienie testów. Nowe
  pliki `.cpp` wymagają `python -m scripts.python.generate_projects` przed buildem. Merge do `main`
  tylko po pełnym Debug + Release build.

---

## 1. Cel i zakres

Użytkownik (2026-08-23) poprosił o wsparcie dla pięciu rzeczy naraz, z jednym wspólnym mianownikiem
("ten sam `ds`/model projektu widziany identycznie"):

1. **`.md`** — podgląd/edycja plików Markdown w projekcie.
2. **`.py`** — edycja skryptów Python z hot-reloadem wystarczająco głębokim, żeby skrypt mógł
   wchodzić w interakcję ze sceną (dodawać/zmieniać atomy, czytać zaznaczenie, itd.).
3. **`.ipynb`** — Jupyter notebooki z tym samym dostępem do projektu co (2)/(4).
4. **Terminal PowerShell (`.ps1`/interaktywny)** — dockowalny panel z żywym shellem.
5. **Interaktywna konsola Python (ipython-style)** w UI, z dostępem do całego projektu przez
   składnię w stylu `project.okeanos["6-7"]["exc_ms"].OUTCAR['free energy']`.

Plus (z rozszerzonego wpisu w `TODO.md`, ten sam temat): model adresowania encji projektu,
persystencja, bezpieczny zapis przy współbieżnej mutacji (skrypt + UI naraz), undo/redo, wiele
serwerów.

**Robocza teza tego planu:** (2)+(3)+(5) to w istocie **jeden system** (jeden `ds`
package/protokół, trzy różne front-endy), nie trzy osobne integracje. (1) i (4) są od niego
niezależne technicznie, ale współdzielą UI-infrastrukturę (docked panels, w T12 już established
wzorzec) i część (4) (żywy proces z ciągłym I/O) jest tym samym prymitywem co jeden z wariantów
(5). Stąd kolejność w sekcji 10 nie jest 1→2→3→4→5.

---

## 2. Co już istnieje (podsumowanie z Orientacji, do checklisty)

| Potrzebne | Stan dziś | Gdzie |
|---|---|---|
| Syntax highlight `.py`/`.md` | Gotowe w vendorze, niepodłączone | `TextEditor.h:618-628`, `TextEditorPanel.cpp` |
| Autocomplete (podstawowe) | Gotowe w vendorze, niekompilowane | `extras/TrieAutoComplete.*` |
| Autocomplete (context-aware/LSP) | Gotowe w vendorze, wymaga nowej zależności `lsp/*` + serwer | `extras/LspBridge.*` |
| Proces długożyjący z I/O | **Brak** — tylko `RunProcess` blokujący | `Core/Platform/ProcessRunner.hpp` |
| Embedded Python | Kod istnieje, **wyłączony buildem** | `PythonInterpreter.*`, `premake5.lua:457,623` |
| `ds` moduł nanobind | Istnieje jako pusty smoke-test | `DefectStudioPythonBridgeModule.cpp` |
| Undo dla mutacji skryptowych | Mechanizm gotowy do reużycia | `UndoStack::PushExecuted/ScopedGroup` |
| Adresowanie encji po nazwie | **Brak** — tylko `Find(uuid)` | `ProjectWorkspace.hpp` rejestry |
| Ścieżka/serwer na `CalculationRecord` | **Brak pola** | `DefectModel.hpp` |
| File watcher | Brak vendora, ale polling wystarczy na start | — |
| Jupyter/`.ipynb` runtime | Brak (ani C++, ani Python-side) | — |

---

## 2.5 Rola `punktukas-tools` — nie wynajdywać koła na nowo (zweryfikowane w źródle, 2026-08-24)

> Użytkownik wprost zapytał czy to zostało wzięte pod uwagę — pierwsza wersja tego planu
> referencjonowała `punktukas` tylko pośrednio (przez `VaspOutputBridge`), bez sprawdzenia
> faktycznego API w źródle. Sprawdzone teraz bezpośrednio w
> `C:\Users\fzabi\punktukas-tools\puntukas_tools\puntukas\` (prywatne repo poza tym projektem —
> **nadal nie wolno go vendorować/commitować**, zob. `TODO.md` "Infrastruktura").

- **`puntukas.vasp.output.VaspOutput`** (`vasp/output.py:22,140`) — `.etot` to zwykła properties
  delegująca do `self.outcar.etot` (`vasp/outcar/outcar.py:26`, `HasTraits` z polem `etot`, regex na
  `"free  energy   TOTEN = ..."`). **`.OUTCAR['free energy']` z przykładu użytkownika nie istnieje
  jako dosłowna składnia w dzisiejszym `punktukas`** — realny odpowiednik to
  `VaspOutput(path).etot`. To potwierdza wcześniejszą adnotację w tym planie ("składnia
  orientacyjna, nie kontrakt"), ale ważne żeby **nie projektować bespoke schematu kluczy
  `['free energy']`/`['bandgap']` od zera** — `ds` ma czytać `.etot`/`.bandgap`/itd. bezpośrednio z
  `VaspOutput`, nie wymyślać własnego mapowania nazw pól.
- **`puntukas.vasp.multidet_relax.calculator.MultidetVasp`** (`calculator.py:14-121`) — **to jest
  dokładnie to, co przykład ZPL użytkownika robi ręcznie.** `MultidetVasp` to `ase.Calculator`
  czytający katalog z podfolderami (`single_calc_names`, domyślnie `("mixed", "triplet")`, ale
  **w pełni konfigurowalne**, więc `("exc_ms", "exc_ms_triplet", "ground_state")` też pasuje),
  budujący `VaspOutput` per podfolder i łączący `.etot` przez **podawaną przez użytkownika lambdę**
  (`energy_combine_eqn`, domyślnie `lambda mixed, triplet: 2*mixed - triplet` — dosłownie
  `2*exc_singlet - exc_triplet` z przykładu, uogólnialne do N wejść dla pełnego ZPL z members
  `ground_state`). **Wniosek: `ds` nie powinno reimplementować kombinowania energii
  multi-kalkulacyjnego (ZPL i podobne) — to już istnieje i jest ogólne.** Rola `ds` to najwyżej
  cienki convenience wrapper budujący `MultidetVasp(...)` z metadanych, które DefectStudio już zna
  (który folder to `exc_ms`, który `ground_state` — z nowego flow tworzenia defektu, `TODO.md`
  T08.6.3), nie osobna logika naukowa.
- **`puntukas.automatization`** (`band_path.py`, `cp2k_convergence.py`, `equation_of_state.py`,
  `spin_contamination.py`, `benchmark_mpi_config.py`) — **bezpośredni prior art dla zupełnie
  innego zadania w Backlogu** ("Moduł generacji testów zbieżności i defektów..."), niepowiązanego z
  tym epikiem wprost, ale warty dopisania jako cross-reference w `TODO.md` przy tamtym zadaniu, żeby
  ktoś nie zaczął tego pisać od zera później.
- **Konsekwencja dla podziału pracy `ds` (rozwija sekcję 8 niżej):** dla strony "czytanie wyników
  obliczeń" `ds` ma dokładnie **jedną** realnie nową robotę — **adresowanie** (mapowanie nazw/tagów
  projektu DefectStudio, o których `punktukas` nic nie wie — `"okeanos"`, `"6-7"`, `"exc_ms"` — na
  ścieżki na dysku). Gdy już ma ścieżkę, **oddaje ją klasom `punktukas` (`VaspOutput`,
  `MultidetVasp`, `Outcar`) zamiast cokolwiek parsować/liczyć samemu.**
- **Ważna konsekwencja dla sekcji 3:** ta połowa (`ds.project`/adresowanie + delegacja do
  `punktukas`) **nie wymaga wcale żywego, podłączonego `DefectStudio.exe`** — to czysto
  dyskowy lookup (manifest projektu/tagi → ścieżka), działający identycznie z uruchomioną appką i
  bez niej, dokładnie jak wygląda przykład użytkownika (zwykły skrypt, zero widocznego "połącz się
  z appką"). **Decyzja Opcja A/B dotyczy WYŁĄCZNIE `ds.scene`/`ds.commands`/`ds.events`** (żywy
  viewport) — nie blokuje w ogóle strony adresowania/odczytu wyników. To odblokowuje wcześniejszy
  start tej części (zob. zaktualizowana kolejność w sekcji 10).

---

## 3. Fundamentalna decyzja architektoniczna — **wymaga potwierdzenia przed kodem**

Wszystko poniżej zależy od jednej rzeczy: **gdzie żyje kod Python, który dotyka `ds`, i jak
rozmawia z żywym `DefectStudio.exe`.** Dwie realne opcje:

### Opcja A — Embedded CPython w procesie `DefectStudio.exe`

Włączyć `DS_PYTHON_CAPI_AVAILABLE`, uruchomić `PythonInterpreter`, `ds` moduł żyje in-process,
konsola/hot-reload/notebook kernel wszystkie wołają ten sam interpreter współdzielący pamięć z
C++.

- **Plus:** zero IPC, najniższe opóźnienie, najprostszy dostęp do żywych obiektów C++ (nanobind
  `def_rw`/referencje bezpośrednio na `CrystalStructure` itp.).
- **Minus:** GIL i main-thread-only-commit (reguła architektoniczna z nagłówka `TODO.md`) muszą
  współistnieć — wywołanie z Python musi albo zawsze trafiać na main thread (marshaling przez
  `EventBus`/kolejkę poleceń mimo że jest "w tym samym procesie", więc część korzyści z (a) i tak
  znika), albo main thread musi trzymać GIL podczas renderu (ryzyko zamrożeń przy długim skrypcie).
  **`.ipynb` przez prawdziwy Jupyter kernel (`ipykernel`) w tej opcji jest trudniejszy** — kernel
  Jupyter to osobny proces z własnym event loopem (ZeroMQ), więc i tak trzeba by go uruchamiać jako
  subprocess mówiący do embedded interpretera przez jakiś kanał — hybryda, nie czysta opcja A.
  Historycznie ta flaga była wyłączona (najpierw jako "zostawiony wyłącznik" per `TODO.md`, ale
  cały istniejący kod bridge'ów jest **konsekwentnie subprocess-first** — to jest odejście od
  ugruntowanej konwencji repo, nie kontynuacja.

### Opcja B — Proces `DefectStudio.exe` jako serwer, `ds` to cienki pakiet Python importowany z zewnątrz (rekomendacja)

`DefectStudio.exe` wystawia lokalny kanał IPC (named pipe na Windows, albo prosty TCP na
`127.0.0.1` — do wyboru w implementacji, nie architektonicznie różne). Pakiet Python `ds`
(zwykły, `pip install -e` do istniejącego `uv` venv, **nie** `.pyd` przez nanobind — zwykły
czysty-Python klient protokołu) łączy się do tego kanału i wystawia `ds.project`/`ds.scene`/
`ds.commands`/`ds.events` jako cienkie proxy wysyłające żądania i odbierające odpowiedzi
(JSON albo msgpack). **Ten sam pakiet `ds` importuje się identycznie** z: hot-reloadowanego
skryptu `.py` (subprocess `python script.py`), interaktywnej konsoli w UI (subprocess
`python -i` albo prawdziwy `ipython` jeśli zainstalowany), i **prawdziwego Jupyter kernela**
(`ipykernel` odpalony jako zwykły subprocess, `.ipynb` UI mówi do niego przez standardowy
protokół `jupyter_client`/ZeroMQ — appka nie reimplementuje wykonania notebooków).

- **Plus:** jeden protokół, trzy front-endy za darmo (skrypt/konsola/notebook to tylko różne
  procesy Python używające tego samego pakietu `ds`). **Main-thread-only-commit jest
  automatyczny, nie do pilnowania ręcznie** — żądanie z Python to wiadomość w kolejce, którą main
  thread konsumuje jak każdy inny `EventBus`/`CommandRegistry` event; brak wątku Python
  współdzielącego pamięć z rendererem, więc brak nowej kategorii wyścigu danych do wynalezienia.
  Zgodne z istniejącą konwencją repo (subprocess-first, `ProcessRunner`/bridges już tak robią).
  Appka może działać, restartować się, wielokrotnie łączyć bez ponownego `Py_Initialize`.
- **Minus:** trzeba zbudować serwer IPC + protokół (nowy kod, nie tylko włączenie istniejącej
  flagi) — ale to jest jednorazowy koszt fundamentu, reużywalny przez PS1 terminal (sekcja 4) i
  cały pkt 5.3/5.5. Odrobinę wyższa latencja niż in-process (nieistotne dla interakcji
  człowiek-w-pętli jak konsola/notebook; do zweryfikowania czy istotne dla hot-reloadu o wysokiej
  częstotliwości).

**Rekomendacja tego planu: Opcja B.** Spójna z resztą repo, rozwiązuje main-thread-safety niemal
za darmo, i naturalnie obsługuje `.ipynb` przez prawdziwy Jupyter zamiast wynajdywania własnego
wykonania notebooków. **To jest jednak decyzja, nie fakt — proszę o potwierdzenie przed kodem
(sekcja 11, pytanie 1).**

---

## 4. Nowy wspólny prymityw: proces długożyjący z ciągłym I/O

Niezależnie od wyniku sekcji 3, **terminal PS1 (5.4)** i (w Opcji B) **serwer IPC** obie strony
potrzebują czegoś, czego dziś nie ma: uchwytu do procesu, który żyje dłużej niż jedno wywołanie i
pozwala pisać/czytać w trakcie.

**Proponowany nowy typ:** `Core/Platform/InteractiveProcess` (siostrzany do `ProcessRunner`, nie
zamiennik — `RunProcess` zostaje dla one-shot bridges, które działają dobrze jak są):
- Windows: `CreatePipe` (dwa piped, stdin/stdout+stderr) + `CreateProcess` z redirected handles —
  ten sam poziom API co `ProcessRunner` już używa wewnętrznie (do zweryfikowania przy
  implementacji, prawdopodobnie współdzielą sporo kodu).
- Odczyt nieblokujący: osobny wątek czytający z pipe'a do thread-safe kolejki linii, UI/panel
  odpytuje kolejkę co klatkę (wzorzec podobny do `JobSystem` progress polling, ale prostszy — bez
  `JobContext`, to nie jest jednorazowe zadanie z wynikiem końcowym).
- Zapis: metoda `WriteLine(std::string)` pisząca do stdin pipe'a.
- Zamknięcie: `Terminate()`/destructor zamyka pipe'y i (jeśli trzeba) zabija proces.

To jest **fundament pod 4 i pod jeden z wariantów 5** — budować raz, reużyć dwa razy.

---

## 5. Rozbicie na funkcje

### 5.1 Markdown (`.md`)

- **MVP (tani):** `TextEditorPanel` dispatchuje `SetLanguage(Language::Markdown())` po
  rozszerzeniu `.md` przy otwieraniu pliku (ten sam mechanizm co dla `.py` niżej — jedna funkcja
  mapująca rozszerzenie na `const Language*`, używana w obu miejscach).
- **Stretch:** live rendered preview (nie tylko highlighted source) — appka nie ma dziś żadnego
  renderera Markdown→ImGui. Realna, znana biblioteka pod to: `imgui_markdown` (pojedynczy header,
  MIT) — **niezweryfikowane w tej sesji czy się dobrze integruje z tym konkretnym forkiem
  ImGui/dockingu, do sprawdzenia przy starcie tego pod-zadania**, nie zakładać z góry. Osobny panel
  albo split-view obok edytora, nie zamiana edytora.

### 5.2 Python (`.py`) z hot-reloadem

- **Syntax highlight:** jak w 5.1, `Language::Python()`.
- **Autocomplete MVP:** wpiąć `TrieAutoComplete` do `premake5.lua` (dodać pliki do
  `DefineImGuiColorTextEditProject`) i połączyć z `TextEditorPanel` — zero nowej zależności,
  działa po identyfikatorach z aktualnego pliku.
- **Autocomplete stretch:** `LspBridge` + serwer językowy (`python-lsp-server`/`jedi-language-
  server`, instalowalne do istniejącego `uv` venv) — prawdziwe, context-aware podpowiedzi. Wymaga
  nowej zależności C++ (`lsp/*` klient, do zvendorowania) — osobny krok, nie MVP.
- **Hot-reload:** polling `std::filesystem::last_write_time` na obserwowanych plikach (dispatch
  przez `JobSystem`, wzorzec `IJob::Execute`), na zmianę: re-wykonaj cały plik jako nowy proces
  Python importujący `ds` (Opcja B) — **re-exec całego pliku, nie granularny function-patching
  jak niektóre hot-reload frameworki** (dużo prostsze, mniej "magiczne", zgodne z ponytail —
  granularny reload to osobna, świadomie odrzucona na start eskalacja, dopisać tylko jeśli re-exec
  całego pliku okaże się realnie za wolny/zbyt hałaśliwy dla undo stacka).
- **Interakcja ze sceną:** przez `ds.scene`/`ds.commands` (sekcja 8) — skrypt wykonuje operacje
  identyczne do tych z UI, przechodzące przez `UndoStack` (sekcja 6).

### 5.3 Jupyter (`.ipynb`)

**Rekomendacja (zależna od Opcji B w sekcji 3): appka NIE reimplementuje wykonania notebooków.**
Deleguje do prawdziwego kernela Jupyter (`ipykernel`, pip-installable, protokół `jupyter_client`
przez ZeroMQ — biblioteki Python, nie nowy C++ vendor). Appka:
1. Odpala `ipykernel` jako subprocess (ten sam `ProcessRunner`/`InteractiveProcess` co reszta).
2. Kernel importuje `ds` tak samo jak każdy inny front-end (Opcja B: łączy się do tego samego IPC
   serwera).
3. **Największy pojedynczy nowy kawałek UI w całym tym epicu:** appka musi narysować notebook —
   lista cell (code/markdown), input, output (text/plaintext na start; obrazki/plots to później —
   VASP/matplotlib output jako PNG do wyświetlenia w cell to naturalne rozszerzenie skoro appka
   już ma pipeline `stb_image`/PNG z T15). Format pliku: `nbformat` (czytanie/zapisywanie `.ipynb`
   JSON) — biblioteka Python, appka może delegować zapis/odczyt do małego skryptu Python zamiast
   pisać własny parser `nbformat` w C++ (ten sam wzorzec co PymatgenBridge — subprocess, nie
   reimplementacja formatu).
4. **Bezpieczny zapis** — `.ipynb` to zwykły plik JSON na dysku projektu (T07.5 mount-friendly),
   atomic write (zapis do pliku tymczasowego + rename, wzorzec do zweryfikowania czy `TextFileIO`
   już to robi czy trzeba dodać) tak samo jak każdy inny plik projektu.

**Nie robić na start:** własny renderer/edytor `.ipynb` UI opierający się na jakimś istniejącym
vendorze notebooków w C++ — **taki vendor nie istnieje i nie ma sensu go szukać**, to nietypowy
format do renderowania poza przeglądarką/Jupyterem. To jest świadomie nowy, dedykowany kawałek UI.

### 5.4 Terminal PowerShell

- Dockowalny panel (wzorzec `IPanel`, jak `TextEditorPanel`) z `InteractiveProcess` (sekcja 4)
  odpalającym `powershell.exe -NoLogo -NoExit`.
- **MVP:** surowy tekst, scrollback, brak prawdziwej emulacji terminala (brak kolorów, brak
  cursor-addressing/ANSI escape parsing) — PowerShell domyślnie formatuje output (kolory, tabele)
  zakładając prawdziwy terminal; bez emulacji część outputu będzie zawierać widoczne escape-kody
  albo wyglądać płasko. **Akceptowalne dla MVP** (nadal użyteczne do prostych komend), flagowane
  jako znane ograniczenie.
- **Stretch:** prawdziwa emulacja przez Windows ConPTY (`CreatePseudoConsole`) zamiast zwykłych
  pipe'ów — daje kolory/formatowanie, ale to inny, bardziej złożony Win32 API niż `InteractiveProcess`
  z sekcji 4 (ConPTY nie jest zwykłym stdin/stdout pipe, to pseudo-terminal z własną semantyką
  resize/VT100). Osobna decyzja implementacyjna, nie blokuje MVP.

### 5.5 Konsola Python (ipython-style)

Zależna od sekcji 3:
- **Jeśli Opcja B:** konsola to dockowalny panel nad `InteractiveProcess` odpalającym `python -i`
  (albo `ipython` jeśli wykryty na PATH — lepszy UX za darmo, appka nie musi reimplementować
  ipython-owych udogodnień typu `?`/`??`/magic commands, tylko je odpalić jeśli dostępne).
  `ds` importowany w tej sesji łączy się do tego samego IPC serwera co skrypty/notebook.
- **Jeśli Opcja A:** panel woła bezpośrednio embedded `PythonInterpreter`, output przechwytywany
  przez przekierowanie `sys.stdout`/`sys.stderr` na stronie Python do callbacku C++ (standardowy
  wzorzec embeddingu, nanobind to obsługuje).
- **Input UI:** jedna linia komend + historia (strzałki góra/dół) na MVP, multi-line dla bloków
  (`if`/`for`/`def`) to rozszerzenie (wykrywanie niekompletnego bloku — standardowy problem REPL,
  Python ma `codeop.compile_command` właśnie do tego, reużyć zamiast pisać własny parser).
  Autocomplete: ta sama drabinka co 5.2 (Trie MVP, LSP stretch) — ale tu kontekst to sesja
  interpretera, nie plik, więc `TrieAutoComplete` (który wiąże się do dokumentu edytora) prawdopodobnie
  potrzebuje małej adaptacji albo osobnej, prostszej implementacji dla pojedynczej linii wejścia.

### 5.6 Multi-tab / integracja z istniejącym `TextEditorPanel`

T12 już zanotował brakujące tabs jako dług ("Nie zrobione (MVP scope): brak wielu otwartych plików
jednocześnie"). Otwieranie `.py`/`.md` w edytorze **koliduje z tym długiem już dziś** (jeden
dokument na raz) — **rekomendacja: zrobić tabs jako część tego zadania, nie osobno**, inaczej
otwarcie drugiego pliku podczas pracy ze skryptem podmienia pierwszy bez ostrzeżenia (już
zanotowane ryzyko w T12).

---

## 6. Undo/Redo dla mutacji ze skryptu

**Konkretny mechanizm, nie tylko zasada:** `ds.scene.*` funkcje (Opcja B: handler po stronie C++
serwera IPC odbierający żądanie) budują te same `ICommand` co UI (np. `DeleteSelectedAtomsCommand`,
`TransformSelectedAtomsCommand` z `RendererAtomEditCommands.cpp`) i wołają
`UndoStack::PushExecuted` — **zero nowej ścieżki mutacji, reużycie 1:1**. Jeśli jedno wywołanie
`ds` robi wiele mutacji na raz (pętla w skrypcie), owinąć w `UndoStack::ScopedGroup` żeby jeden
`Ctrl+Z` cofnął cały krok skryptu, nie każdą mutację osobno — decyzja do potwierdzenia: czy
granulacja grupowania to "cały skrypt = 1 grupa" czy "każde pojedyncze wywołanie API = 1 grupa"
(różne UX, do ustalenia w sekcji 11).

---

## 7. Bezpieczny zapis / współbieżność

W Opcji B jest to w większości rozwiązane konstrukcyjnie: Python nigdy nie ma bezpośredniego
wskaźnika/referencji do `CrystalStructure` w pamięci C++ — komunikuje się wyłącznie przez
żądania, które main thread konsumuje w swoim własnym tempie (jak każdy `EventBus`/`CommandRegistry`
event dziś). Nie ma nowej kategorii wyścigu danych do wynalezienia. **W Opcji A** to wymaga
jawnego mechanizmu (kolejka poleceń z Python do main threada, GIL scope discipline) — dodatkowy
powód do preferowania Opcji B.

---

## 8. `ds` moduł / model adresowania (`project.okeanos["6-7"]["exc_ms"]`)

Rozszerza to co już dopisane w `TODO.md` Backlogu (2026-08-23), skorygowane po weryfikacji
`punktukas` w sekcji 2.5 — **`ds.project` to warstwa adresowania, nie warstwa naukowa.** Wszystko
co dotyczy parsowania/łączenia wyników (OUTCAR, ZPL-style kombinacje) idzie przez `punktukas`
bezpośrednio (`VaspOutput`, `MultidetVasp`), nie przez nowy kod DefectStudio. Konkretne luki do
zamknięcia **przed** implementacją `ds.project`:
1. **Rejestry `ProjectWorkspace` potrzebują wyszukiwania po nazwie/tagu + enumeracji**, nie tylko
   `Find(uuid)` — nowe metody na `StructureRegistry`/`DefectRegistry`/`CalculationRegistry`
   (`FindByTag`/`All()` czy podobne, do zaprojektowania).
2. **`CalculationRecord` potrzebuje pola na lokalizację danych** (ścieżka na dysku/mount, docelowo
   referencja do `ServerProfile` z T07.5.2 gdy ten zaistnieje) — dziś nic nie łączy
   `CalculationRecord` z konkretnym katalogiem zawierającym `OUTCAR`.
3. **`ds.project["okeanos"]["6-7"]["exc_ms"]` rozwiązuje się do ścieżki/lekkiego uchwytu
   (`CalculationHandle(path=...)`), nie do sparsowanych danych.** Dalsze użycie (`.etot`,
   `.bandgap`, budowa `MultidetVasp`) to bezpośrednie wywołanie `punktukas` na tej ścieżce — `ds`
   nie cache'uje/nie reimplementuje parsowania. Jeśli cache okaże się potrzebny z powodów
   wydajnościowych (network-mounted OUTCAR, drogi re-parse), to cache **na poziomie
   `punktukas`-owych wywołań** (memoize po ścieżce+mtime), nie nowy schemat danych.
4. **Co dokładnie znaczy `"6-7"` i `"exc_ms"`** — to są **tagi/nazwy zdefiniowane przez
   użytkownika**, nie predefiniowana appka-owa lista (potwierdza to nowy wpis TODO o tworzeniu
   defektu: "typy obliczeń do ustalenia w rozmowie z użytkownikiem"). `ds.project[...]` to
   prawdopodobnie odwzorowanie na `DefectConcept.tags`/`displayName` i
   `CalculationRecord.displayName`, ale **dokładna gramatyka indeksowania (`project.okeanos["6-7"]`
   — co to jest "okeanos"? nazwa projektu? serwer? kolekcja?) nie jest ustalona przez ten plan** —
   zob. sekcja 11.
5. **`ds.project` może być czystym pakietem Python czytającym pliki projektu bezpośrednio z
   dysku** (manifest/tagi), **niezależnie od decyzji Opcja A/B w sekcji 3** i bez potrzeby żywego
   `DefectStudio.exe` — zob. sekcja 2.5, ostatni punkt. To osobny, prostszy tor pracy niż
   `ds.scene`/`ds.commands`.

---

## 9. Wiele serwerów

Zależne od T07.5.2 (Server Profiles, dziś **`[ ]` nieistniejące w kodzie**). Dla MVP tego zadania:
**adresowanie działa dla danych już osiągalnych jako zwykła ścieżka lokalna/zamontowana** (T07.5
mount-first — appka i tak nie rozróżnia lokalnej ścieżki od zamontowanej sieciowej). Pełne
"appka wie, który `CalculationRecord` leży na którym serwerze i łączy się automatycznie" wymaga
Server Profiles jako realnego typu — **blokujące dla pełnej wizji, nie blokujące dla MVP** (dopóki
użytkownik ręcznie zamontował/otworzył właściwy katalog, `ds` czyta z niego jak z lokalnego).

---

## 10. Proponowana kolejność faz

Kolejność wymuszona zależnościami. **Zmiana po sekcji 2.5:** `ds.project`/adresowanie
(dawny punkt 8) nie zależy wcale od decyzji Opcja A/B ani od `InteractiveProcess` — to osobny,
niezależny tor, może iść równolegle od samego początku, nie na końcu.

**Tor 1 — adresowanie (niezależny od Opcji A/B, czysto dyskowy, zob. 2.5/8):**
1. **Rejestry z nazwą/enumeracją (sekcja 8, pkt 1-2)** + **`ds.project` jako czysty pakiet Python**
   rozwiązujący tagi na ścieżki i oddający je bezpośrednio `punktukas` (`VaspOutput`/
   `MultidetVasp`) — może ruszyć od razu, zero zależności od reszty planu.

**Tor 2 — scena/konsola/terminal (wymaga decyzji z sekcji 3):**
2. **Decyzja Opcja A/B (sekcja 3)** — blokuje resztę toru 2.
3. **Syntax highlighting `.py`/`.md` + tabs w `TextEditorPanel` (5.1, 5.2 częściowo, 5.6)** —
   zero zależności od #2, czysty zysk, rób równolegle z #2/torem 1.
4. **`InteractiveProcess` (sekcja 4)** — fundament pod #6 i #7 (jeśli Opcja B/subprocess REPL).
5. **`ds` serwer IPC minimalny (Opcja B) + `ds.scene`/`ds.commands` read+write podstawowe
   operacje** (dodaj/usuń/przesuń atom przez istniejące `ICommand`, zob. sekcja 6) — pierwszy
   realny dowód, że skrypt Python rusza scenę i cofa się przez `Ctrl+Z`.
6. **Hot-reload file watcher (polling) dla `.py`** (5.2) — mały krok po #5.
7. **Terminal PS1 (5.4)** — niezależne od #5, ale korzysta z #4. Może iść równolegle z #5/#6.
8. **Konsola Python w UI (5.5)** — po #4 i #5 (potrzebuje obu: żywego procesu + działającego `ds`).
9. **`.ipynb` (5.3)** — najdroższe pojedynczo (nowe UI + integracja `jupyter_client`), na koniec,
   po #5 (kernel importuje ten sam `ds` — obie połowy, `ds.project` z toru 1 i `ds.scene` z toru 2).
10. **Stretch (dowolna kolejność, niski priorytet):** `LspBridge` autocomplete, markdown live
    preview, ConPTY dla terminala, wielu serwerów (zależne od T07.5.2 osobno).

---

## 11. Otwarte decyzje — **do rozstrzygnięcia z użytkownikiem przed kodem**

1. **Opcja A (embedded) vs Opcja B (IPC + subprocess) — sekcja 3.** Rekomendacja: B. To jest
   decyzja fundamentalna, zmienia kształt każdej kolejnej sekcji.
2. **Protokół IPC** (jeśli B): named pipe vs lokalny TCP, JSON vs msgpack — szczegół
   implementacyjny, nie architektoniczny, ale do wyboru na starcie #4 w sekcji 10.
3. **Gramatyka adresowania `ds.project[...]`** (sekcja 8, pkt 4) — co dokładnie jest kluczem na
   każdym poziomie zagnieżdżenia, i jak się to mapuje na `DefectConcept`/`CalculationRecord`/tagi.
4. **Granulacja undo-grupowania dla mutacji ze skryptu** (sekcja 6) — cały skrypt = 1 grupa, czy
   każde wywołanie `ds.scene.*` osobno.
5. **`.ipynb`: prawdziwy Jupyter kernel (rekomendacja) czy appka renderuje/wykonuje sama?**
   Rekomendacja: prawdziwy kernel przez `jupyter_client` — potwierdzić.
6. **Wybór serwera językowego dla stretch-owego LSP autocomplete** (`python-lsp-server` vs
   `jedi-language-server` vs inny) — nieistotne teraz (stretch), ale do zanotowania kiedy przyjdzie
   pora.
7. **Zakres hot-reloadu:** re-exec całego pliku (rekomendacja, prostsze) vs granularny
   function-patching — potwierdzić że re-exec wystarcza.

---

## Weryfikacja przed merge do main

Jak w poprzednich planach: pełny Debug + Release build + `DefectStudioTests` po każdym
nietrywialnym kroku z sekcji 10. Dodatkowo dla tego zadania konkretnie: nowa zależność Python
(`ipykernel`/`jupyter_client`/`nbformat`/LSP server) idzie do `uv` managed venv
(`scripts/Tooling.*`/`setup.py`), **nie** system-wide pip install — zweryfikować że `uv`
workflow już to obsługuje (T01 notatka: "`uv` jako dependency manager dla Python venv").
