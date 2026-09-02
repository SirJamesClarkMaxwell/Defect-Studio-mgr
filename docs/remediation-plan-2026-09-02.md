# Plan naprawczy — architektura + testy

Repozytorium: `Defect-Studio-mgr`
Data utworzenia: 2026-09-02
Wykonawca: model Haiku (plan pisany pod wykonanie krok-po-kroku, bez podejmowania decyzji projektowych)

Źródła:
- `docs/architecture-code-review-2026-09-01.md` (aktualny, nadrzędny)
- `docs/architecture-code-review-2026-07-01.md` (historyczny — większość P0/P1 już naprawiona)
- `docs/test-suite-review-2026-09-01.md`

## Zasady obowiązujące w całym planie

1. **TDD jest decyzją projektową.** Każdy krok, który zmienia zachowanie kodu, zaczyna się od
   napisania testu, który **musi najpierw nie przejść** (RED), potem implementacji (GREEN).
   Kroki czysto mechaniczne (przeniesienie pliku, usunięcie martwego kodu, konsolidacja dokumentów)
   nie mają testu jednostkowego — ich testem jest kompilacja i/lub skrypt walidacyjny, i jest to
   wprost zaznaczone w kroku.
2. **Wykonuj kroki po kolei.** Kroki są zależne — kolejność nie jest sugestią.
3. **Nie rozszerzaj zakresu kroku.** Jeśli w trakcie kroku widzisz inny problem, dopisz go do
   sekcji „Znaleziska poboczne" na końcu tego pliku i idź dalej.
4. **Po dodaniu lub usunięciu jakiegokolwiek `.cpp`/`.hpp` pod `src/` lub `tests/` regeneruj
   projekty** — premake globuje pliki w momencie generacji, nie budowania:
   ```
   scripts\Windows\GenerateProjects.bat
   ```
5. **Build w trakcie pracy: tylko Release** (ustalenie z sesji task/17). Pełna macierz
   Debug + Release dopiero przy merge do `main` (skill `full-build-verify`).
6. **Oczekiwana liczba pominiętych testów to 2** (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
   `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`) — `DS_PYTHON_CAPI_AVAILABLE=0`
   w tym buildzie. To nie jest regresja.
7. **Granice architektoniczne z `CLAUDE.md` / `AGENTS.md` obowiązują w każdym kroku.**
8. Ignoruj katalogi: `.git`, `.local`, `.vscode`, `build`, `install`, `.venv`, `Vendor`.

## Standardowe komendy

```
:: regeneracja projektów (po dodaniu/usunięciu plików źródłowych)
scripts\Windows\GenerateProjects.bat

:: build Release obu targetów
python scripts\python\build.py --config Release --target DefectStudio
python scripts\python\build.py --config Release --target DefectStudioTests

:: uruchomienie testów
build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe

:: pojedyncza grupa testów
build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe --gtest_filter=NazwaSuite.*
```

Stan wyjściowy (zmierzony 2026-09-02): 277 testów, 275 zielonych, 2 pominięte.

## Branche

Jeden branch na etap, zgodnie z zasadą „jeden branch per task" z `docs/archive/work/project/TODO.md`:

| Etap | Branch | Kroki |
|---|---|---|
| I — sprzątanie i fundament | `task/18-arch-cleanup` | 1–5 |
| II — testy undo (najważniejsze) | `task/19-undo-command-tests` | 6–8 |
| III — przesunięcia granic | `task/20-boundary-moves` | 9–11 |
| IV — ekstrakcja StructureEditor | `task/21-structure-editor` | 12 |

Merge do `main` po każdym etapie, po pełnym `full-build-verify`.

---

# ETAP I — sprzątanie i fundament (`task/18-arch-cleanup`)

Cel etapu: usunąć martwy kod, uporządkować ADR-y, przesunąć jeden typ, i **postawić checker, który
nie pozwoli reszcie planu się cofnąć**.

## Krok 1 — usuń martwy `SetCameraViewCommand`

**Znalezisko:** architecture-code-review-2026-09-01, finding 4.

**Fakt zweryfikowany:** `SetCameraViewCommand` **nie jest tworzony nigdzie w `src/`**. `grep` po
całym repo znajduje tylko jego własną definicję i wzmianki w dokumentach. `Execute` i `Undo` mają
puste ciała. Klasa przeżyła dwa audyty jako martwa abstrakcja.

**Test:** brak testu jednostkowego — to usunięcie martwego kodu. Testem jest kompilacja
i niezmieniona liczba przechodzących testów.

**Akcje:**

1. Usuń dwa pliki:
   - `src/Renderer/Commands/SetCameraViewCommand.cpp`
   - `src/Renderer/Commands/SetCameraViewCommand.hpp`
2. Sprawdź, że nic ich nie includuje:
   ```
   grep -rn "SetCameraViewCommand" src/ tests/
   ```
   Wynik musi być pusty. Jeśli coś zostało — **zatrzymaj się i zgłoś**, bo to znaczy, że fakt
   powyżej przestał być prawdziwy.
3. Regeneruj projekty (usunięto pliki źródłowe).
4. Dopisz do `docs/adr/ADR-011-state-mutation-policy.md` jedno
   zdanie w sekcji o stanie lokalnym:

   > Historia widoku kamery (`RendererWindowState::viewUndoHistory` / `viewRedoHistory`) jest
   > świadomie per-okno i pozostaje poza globalnym `UndoStack`. Kamera jest lokalnym stanem UI
   > w rozumieniu §2. Decyzję należy otworzyć ponownie tylko wtedy, gdy pojawi się funkcja
   > wymagająca atomowego cofnięcia zmiany widoku razem ze zmianą domenową.

**Weryfikacja:**
```
python scripts\python\build.py --config Release --target DefectStudio
python scripts\python\build.py --config Release --target DefectStudioTests
build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe
```

**Done gdy:** build Release przechodzi, 275 zielonych / 2 pominięte, `grep` czysty.

**Commit:** `refactor(renderer): remove unused SetCameraViewCommand, document local view history`

---

## Krok 2 — skonsoliduj katalogi ADR

**Znalezisko:** architecture-code-review-2026-09-01, finding 7.

**Fakt zweryfikowany:** istnieją dwa katalogi ADR z kolidującą numeracją —
`docs/adr/ADR-011-state-mutation-policy.md` oraz `docs/adr/ADR-001..ADR-010`.

**Test:** brak — zmiana wyłącznie dokumentacyjna.

**Akcje:**

Konsolidacja jest już wykonana: ADR-y `ADR-001`–`ADR-011` żyją w `docs/adr/`, a dawny
`0001-state-mutation-policy.md` został przemianowany na `docs/adr/ADR-011-state-mutation-policy.md`.
Ten krok jest teraz wyłącznie weryfikacją, że numeracja i odwołania nadal wskazują `docs/adr/`.
Duplikat audytu z `docs/archive/work/` pozostaje w archiwum.

**Done gdy:** jeden katalog ADR, numeracja 001–011 bez kolizji, `grep` po starej ścieżce pusty.

**Commit:** `docs(adr): consolidate ADR directories, promote state-mutation policy to ADR-011`

---

## Krok 3 — przenieś `UiConfig.hpp` do `Core/Configuration`

**Znalezisko:** architecture-code-review-2026-09-01, finding 1, pierwszy krok.

**Po co:** kasuje krawędź cyklu `Events → App`. `App` ma być composition rootem, na który wszystko
wskazuje, a nie modułem, z którego niższe warstwy importują typy.

**Fakt zweryfikowany:** `src/App/UiConfig.hpp` jest includowany dokładnie w 4 miejscach:
- `src/App/ApplicationState.hpp:15`
- `src/Events/EditorUiEvents.hpp:10`
- `src/Presentation/EditorUiState.hpp:8`
- `src/Presentation/ImGuiLayer.hpp:14`

Katalog `src/Core/Configuration/` już istnieje (zawiera `ConfigProfile.hpp`).

**Test:** brak testu jednostkowego — to przeniesienie typu bez zmiany zachowania. Testem jest
kompilacja.

**Akcje:**

1. ```
   git mv src/App/UiConfig.hpp src/Core/Configuration/UiConfig.hpp
   ```
2. W przeniesionym pliku sprawdź, czy nie includuje niczego z `App/`. Jeśli tak — **zatrzymaj się
   i zgłoś**, bo wtedy przeniesienie nie usuwa cyklu, tylko go odwraca.
3. Zamień include we wszystkich 4 plikach:
   ```
   #include "App/UiConfig.hpp"   →   #include "Core/Configuration/UiConfig.hpp"
   ```
4. Regeneruj projekty (przeniesiono plik źródłowy).
5. Potwierdź brak pozostałości:
   ```
   grep -rn "App/UiConfig.hpp" src/ tests/
   ```

**Weryfikacja:** build Release obu targetów + pełny run testów.

**Done gdy:** build przechodzi, 275/2, `grep` czysty.

**Commit:** `refactor(core): move UiConfig to Core/Configuration, break Events→App cycle`

---

## Krok 4 — checker kierunku include'ów

**Znalezisko:** architecture-code-review-2026-09-01, finding 2. **To jest najważniejszy krok
Etapu I** — bez niego każda inna naprawa z tego planu z czasem się cofnie.

**Zasada:** checker startuje z **dzisiejszą macierzą jako baseline**, razem z istniejącymi cyklami
oznaczonymi jako znane wyjątki. Ma przejść pierwszego dnia i blokować wyłącznie **nowy** drift.

**TEST FIRST (RED):**

Utwórz `scripts/python/tests/test_check_include_directions.py` (jeśli katalog `scripts/python/tests`
nie istnieje — utwórz go) z testami na czystych danych, bez czytania prawdziwego `src/`:

```
Test 1: dozwolona krawędź przechodzi
  moduł "Domain" includujący "Core/Foo.hpp" przy allow-liście Domain -> {Core, Domain}
  => brak naruszeń
Test 2: niedozwolona krawędź jest wykrywana
  moduł "Domain" includujący "Renderer/Foo.hpp" przy tej samej allow-liście
  => dokładnie 1 naruszenie, wskazujące plik i krawędź
Test 3: krawędź na liście known_exceptions nie jest raportowana jako naruszenie
Test 4: include niebędący include'em modułowym (np. <vector>) jest ignorowany
```

Uruchom je — muszą nie przejść (brak modułu do zaimportowania). To jest RED.

**IMPLEMENTACJA (GREEN):**

Utwórz `scripts/python/check_include_directions.py`. Wymagania, dokładnie:

- Moduły = katalogi pierwszego poziomu w `src/`:
  `App`, `Core`, `Debug`, `Demo`, `Domain`, `Events`, `IO`, `Presentation`, `Renderer`,
  `ScientificRuntime`, `Storage`.
- Dla każdego `.cpp`/`.hpp` pod `src/` ustal jego moduł (pierwszy segment ścieżki względem `src/`).
- Wyciągnij includy postaci `#include "Moduł/..."`. Ignoruj `<...>` i wszystko, czego pierwszy
  segment nie jest nazwą modułu.
- Wczytaj allow-listę z `scripts/python/include_rules.json` (patrz niżej).
- Naruszenie = krawędź `moduł_źródłowy -> moduł_docelowy`, której nie ma ani w `allowed`, ani
  w `known_exceptions`.
- Wyjście: dla każdego naruszenia jedna linia `plik:linia: Moduł -> Moduł (niedozwolone)`.
  Exit code 1 przy jakimkolwiek naruszeniu, 0 gdy czysto.
- Flaga `--print-matrix` wypisuje aktualną, zmierzoną macierz krawędzi (przyda się przy
  aktualizacji baseline'u po krokach 9–12).

`scripts/python/include_rules.json` — baseline. **Wygeneruj go z rzeczywistego stanu** (uruchom
`--print-matrix` i wpisz wynik), a następnie **przenieś ręcznie te trzy krawędzie do
`known_exceptions`** wraz z uzasadnieniem, bo to są udokumentowane cykle do skasowania:

```json
{
  "allowed": { "...": ["wygenerowane z --print-matrix"] },
  "known_exceptions": [
    {"from": "Events", "to": "Renderer", "reason": "finding 1 - cykl Renderer<->Events, do usuniecia"},
    {"from": "IO", "to": "Renderer", "reason": "finding 1 - cykl IO<->Renderer, do usuniecia"},
    {"from": "IO", "to": "App", "reason": "finding 1 - cykl IO<->App, do usuniecia"}
  ]
}
```

Jeśli `--print-matrix` pokaże krawędź `Events -> App`, to znaczy, że krok 3 nie został wykonany —
wróć do niego.

**Wpięcie w pipeline:** w `scripts/python/ci_check.py`, w funkcji `run()`, **przed** wywołaniem
`generate_script.run(...)`, dodaj wywołanie checkera i zwróć jego kod wyjścia, jeśli różny od 0.

**Weryfikacja:**
```
python scripts\python\check_include_directions.py
python -m pytest scripts\python\tests\test_check_include_directions.py
```
Pierwsza komenda musi zwrócić 0 (baseline przechodzi). Testy muszą być zielone.

**Done gdy:** checker przechodzi na czystym drzewie, testy checkera zielone, `ci_check.py` go woła.

**Commit:** `build(scripts): add include-direction checker with current matrix as baseline`

---

## Krok 5 — wepnij `clang-tidy` (tryb doradczy)

**Znalezisko:** test-suite-review-2026-09-01, evidence-placement problem 4.

**Fakt zweryfikowany:** `.clang-tidy` istnieje i ma sensowny zestaw checków
(`bugprone-*`, `cppcoreguidelines-*`, `performance-*`, `modernize-*`, próg rozmiaru funkcji
80 linii / 40 instrukcji), `WarningsAsErrors: ''`, i **nic go nie uruchamia** — brak wzmianki
w `scripts/` i w `premake5.lua`.

**Test:** brak testu jednostkowego. Testem jest to, że skrypt kończy się kodem 0 i produkuje raport.

**Akcje:**

1. Utwórz `scripts/python/run_clang_tidy.py`:
   - znajdź `clang-tidy` przez istniejące `scripts.python.common.tooling.detect_tool`
     (wzoruj się na tym, jak `build.py` znajduje `msbuild`);
   - jeśli nie znaleziono — wypisz `[skip] clang-tidy not found` i zwróć **0** (brak narzędzia nie
     może blokować builda);
   - uruchom na plikach `.cpp` pod `src/`, z wyłączeniem `Vendor/`;
   - zapisz surowe wyjście do `build/clang-tidy-report.txt`;
   - wypisz podsumowanie: liczba ostrzeżeń w rozbiciu na kategorie checków;
   - **zawsze zwracaj 0** w tej iteracji (tryb doradczy — `WarningsAsErrors` zostaje puste).
2. Dodaj wywołanie w `scripts/python/ci_check.py` po buildzie, przed `run_script.run(...)`.
   Kod wyjścia ignorowany zgodnie z punktem wyżej.
3. Uruchom raz i **zapisz liczbę ostrzeżeń w sekcji „Znaleziska poboczne" tego pliku**. To jest
   baseline długu — nie naprawiaj go teraz.

**Weryfikacja:**
```
python scripts\python\run_clang_tidy.py
```

**Done gdy:** skrypt działa, raport powstaje, liczba ostrzeżeń zapisana w tym dokumencie.

**Commit:** `build(scripts): run clang-tidy in advisory mode, report to build/clang-tidy-report.txt`

---

## Dodatkowo w Etapie I — narzędzia agenta

Dwie pozycje spoza audytów, z przeglądu automatyzacji Claude Code z 2026-09-02. Bez testów
jednostkowych — to konfiguracja, nie kod. Kolejność względem kroków 1–5 dowolna.

### Zamelduj context7 w repo (`.mcp.json`)

`AGENTS.md` ma sekcję „External Library Docs (Context7 MCP)" z przypiętymi ID bibliotek, ale
**w repozytorium nie ma `.mcp.json`**. Serwer jest skonfigurowany tylko na poziomie użytkownika, więc
ta instrukcja jest częściowo fikcją: Codex albo druga maszyna mogą go nie mieć włączonego i będą
zamiast tego czytać `Vendor/`, czyli dokładnie to, czego tamta sekcja zabrania.

Utwórz `.mcp.json` w katalogu głównym z wpisem serwera context7 i zacommituj go. Weryfikacja:
w świeżej sesji narzędzia `mcp__context7__*` są dostępne bez konfiguracji użytkownika.

**Commit:** `chore(mcp): check context7 server into the repo so pinned library IDs resolve`

### Subagent `python-bridge-reviewer` (`.claude/agents/`)

Katalog `.claude/agents/` nie istnieje. Każdy realny błąd znaleziony w sesjach z 2026-09-01 i
2026-09-02 leżał na granicy C++/Python: przesunięcie indeksu pasma między konwencją VASP (1-based)
a WAVECAR (0-based), uszkodzenie ścieżki sieciowej przez CRLF w parserze konfiguracji. Żadnego nie
łapie ani kompilator, ani testy.

Subagent read-only, który dla wskazanej zmiany czyta `src/ScientificRuntime/Python/*Bridge.{hpp,cpp}`
razem z odpowiadającym skryptem w `scripts/python/examples/` i porównuje: nazwy pól w payloadzie
JSON, kolejność argumentów pozycyjnych, konwencje indeksowania (0- kontra 1-based) oraz obsługę
ścieżek. Raportuje `plik:linia`, nie proponuje poprawek.

Pilne dlatego, że `docs/new-structure-wizard-design-2026-09-02.md` przepuszcza przez tę granicę dwa
nowe kontrakty — zapis POSCAR i generację POTCAR.

**Commit:** `chore(agents): add python-bridge-reviewer for C++/Python contract drift`

### Świadomie odłożone z tego samego przeglądu

- **Hook `clang-format` na edytowanych liniach** — dopiero po tym, jak krok 5 wpuści `clang-tidy`.
  Dwa narzędzia stylu w jednym tygodniu dają jedną falę zmian formatujących, w której nie widać,
  które ostrzeżenia są realne. Gdy wejdzie: formatuj **tylko zmienione linie** (`--lines=N:M`),
  nigdy cały plik — `RendererPanel.cpp` ma 4041 linii.
- **Skill `new-panel`** — kształt panelu zmienia się właśnie teraz (podgląd efemeryczny z własnym
  `windowId`, dokowanie przez `DockBuilder`, dirty per dokument). Szablon zrobiony dziś utrwaliłby
  wersję do przepisania.
- **Blokada edycji `Vendor/**`** — piąty hook przeciw czemuś, co się jeszcze nie zdarzyło.
- **Subagent `test-oracle-reviewer`** — pilnuje właściwości, którą przegląd testów uznał za już
  zdrową. Wróć po Etapie II, gdy dojdą testy cofania i round-tripy.

---

**KONIEC ETAPU I.** Uruchom `full-build-verify` (Debug + Release, oba binaria, testy zielone
w obu konfiguracjach), zmerguj `task/18-arch-cleanup` do `main`.

---

# ETAP II — testy undo komend edycji (`task/19-undo-command-tests`)

**To jest najcenniejsza część planu.** `src/Renderer/Commands/RendererAtomEditCommands.cpp`
(1531 linii, 12 klas komend) to jedyny kod w aplikacji mutujący naukowe źródło prawdy —
i ma **zero testów**. Etap IV (ekstrakcja `StructureEditor`) refaktoryzuje właśnie ten plik, więc
siatka bezpieczeństwa musi powstać **przed** nim, nie po.

## Inwentarz komend (zweryfikowany, `RendererAtomEditCommands.cpp`)

| # | Klasa | linia | Co snapshotuje na Undo | Undoable |
|---|---|---|---|---|
| 1 | `DeleteSelectedAtomsCommand` | 197 | `atoms` + `bonds` | tak |
| 2 | `DuplicateSelectedAtomsCommand` | 333 | `atoms` + `bonds` | tak |
| 3 | `TransformSelectedAtomsCommand` | 450 | **tylko `atoms`** | tak |
| 4 | `NudgeSelectedAtomsCommand` | 537 | deleguje do #3 | tak |
| 5 | `CopySelectedAtomsCommand` | 622 | — (tylko odczyt) | **nie** |
| 6 | `PasteAtomsCommand` | 686 | `atoms` + `bonds` | tak |
| 7 | `AddAtomAtCoordinatesCommand` | 799 | `atoms` + `bonds` | tak |
| 8 | `ChangeSelectedAtomTypeCommand` | 903 | **tylko `atoms`** | tak |
| 9 | `SetBondSettingsCommand` | 1021 | **tylko `bondSettings`** | tak |
| 10 | `ConnectSelectedAtomsCommand` | 1114 | **tylko `bonds`** | tak |
| 11 | `SetAtomPropertiesCommand` | 1239 | **jeden `AtomSite`** | tak |
| 12 | `SetElementStyleCommand` | 1341 | `AtomRenderStyle` (nie domena) | tak |

Pogrubione pozycje to te, gdzie snapshot jest częściowy. **To nie jest z góry bug** — może być
poprawną optymalizacją. Zadaniem testów jest **rozstrzygnąć to dowodem, nie czytaniem kodu.**

Szczególna uwaga na #9 `SetBondSettingsCommand`: jego `Execute` woła `RegenerateAutoBonds`, ale
`Undo` przywraca tylko `bondSettings` (linie 1058 i 1083). Jeśli po `Undo` bondy nie wracają do
stanu sprzed — to jest realny błąd. Test to pokaże.

## Fixture — fakty zweryfikowane

- `RendererLayer::RendererLayer(RendererStartupConfig)` woła wyłącznie `loadPersistedViews()`,
  która czyta pliki tekstowe. **Nie dotyka OpenGL.** Konstrukcja w teście headless jest bezpieczna.
  **Nie wywołuj `OnAttach()`.**
- `DomainLayer` ma bezargumentowy konstruktor i `Workspace()`.
- Komendy **nie używają `EventBus`** (zweryfikowane grepem) — nie trzeba go podpinać.
- `RebuildAndSync` używa `BuildRendererStructureData` + `SceneSystem` — czysta manipulacja danymi,
  bez GL.
- Komendy przyjmują `WeakRef<DomainLayer>` / `WeakRef<RendererLayer>`, więc fixture musi trzymać
  `Ref<>` (`MakeRef<...>`) przez cały czas życia testu.
- `ResolveAtomEditTarget` wymaga: okna w `rendererLayer.GetWindows()` o pasującym `windowId`,
  niepustego `windowState.structure.domainStructureId`, będącego poprawnym UUID, oraz istniejącego
  `StructureRecord` pod tym UUID w `domainLayer.Workspace().Structures()`.

## Krok 6 — fixture + pierwsza komenda (dowód, że fixture działa)

**TEST FIRST (RED):**

Utwórz `tests/Renderer/Commands/RendererAtomEditCommandsTests.cpp`.

Napisz w nim helper fixture'a `MakeEditFixture()`, który:
1. tworzy `Ref<DomainLayer> domain = MakeRef<DomainLayer>()`;
2. buduje prostą `CrystalStructure` — sześcian, 4–8 atomów, co najmniej dwa różne pierwiastki,
   z wygenerowanymi bondami (użyj `RegenerateAutoBonds` z `Domain/Crystal/BondGenerator.hpp`,
   żeby bondy były realne, a nie ręcznie wpisane);
3. rejestruje ją: `auto record = domain->Workspace().Structures().Add(std::move(structure), Path("test.poscar"), "Test");`
4. tworzy `Ref<RendererLayer> renderer = MakeRef<RendererLayer>(RendererStartupConfig{})`;
5. buduje `RendererWindowState` przez `BuildRendererStructureData(record->structure, ...)`,
   przekazując `ToString(record->id)` jako `domainStructureId`, ustawia deterministyczny
   `windowState.windowId = "test-window"`, i woła `renderer->AddWindow(std::move(windowState))`;
6. zwraca strukturę trzymającą `domain`, `renderer`, `record->id`, `windowId`.

Dodaj też helper porównujący:
```
bool StructuresEqual(const CrystalStructure &a, const CrystalStructure &b);
```
porównujący **atomy** (pozycja z tolerancją 1e-5, gatunek, label, charge, occupancy,
selective dynamics) **oraz bondy** (indeksy, typ/origin, widoczność) **oraz `bondSettings`**.
Ten helper jest niezależnym oraclem — nie wolno mu patrzeć na to, co dana komenda zapisuje.

Napisz **jeden** test:

```
TEST(RendererAtomEditCommandsTests, DeleteSelectedAtomsRoundTripsThroughUndo)
  fixture = MakeEditFixture()
  zaznacz atom 0 w windowState.selectedAtomIndices
  before = kopia record->structure
  command = CreateDeleteSelectedAtomsCommand(domain, renderer, atomStyleTable, elementProps, "test-window")
  CommandContext ctx;
  ASSERT_TRUE(command->Execute(ctx))                         // sukces
  EXPECT_FALSE(StructuresEqual(record->structure, before))   // faktycznie coś zmienił
  ASSERT_TRUE(command->Undo(ctx))
  EXPECT_TRUE(StructuresEqual(record->structure, before))    // wrócił dokładnie
```

Regeneruj projekty (nowy plik testowy), zbuduj, uruchom. **Ten test ma prawo najpierw nie
kompilować się lub nie przechodzić** — to jest RED. Doprowadź fixture do działania (nie zmieniaj
kodu produkcyjnego), aż test będzie zielony.

`DeleteSelectedAtomsCommand` snapshotuje i `atoms`, i `bonds` (linie 240–241, 305–306), więc
**oczekujemy, że przejdzie.** Jeśli nie przechodzi — problem jest we fixture, nie w komendzie.

**Done gdy:** jeden zielony test, fixture udowodniony.

**Commit:** `test(renderer): add atom-edit command fixture and delete-undo round-trip`

---

## Krok 7 — rozszerz na wszystkie komendy undoable

**TEST FIRST — to cały krok, tu nie ma implementacji.**

Dopisz do tego samego pliku po jednym teście round-trip dla każdej pozostałej komendy undoable
z inwentarza (#2, #3, #4, #6, #7, #8, #9, #10, #11, #12). Każdy test ma tę samą strukturę:

```
before = kopia record->structure
Execute  -> sukces, struktura się zmieniła
Undo     -> sukces, StructuresEqual(record->structure, before) == true
Redo     -> sukces, struktura == stan po Execute
```

Uwagi wykonawcze do konkretnych komend:

- **#3 `Transform`** — potrzebuje `GizmoTransformPayload` z `atomIndices` i `afterPositions`.
  Przesuń jeden atom o wektor na tyle duży, żeby zmienił zestaw bondów (np. 3.0 jednostki).
  Właśnie ten przypadek rozstrzyga pytanie „atoms vs bonds".
- **#4 `Nudge`** — używa **skupionego** okna (`windowId` jest twardo puste w kodzie, linia 568).
  Fokus ustawia się wyłącznie eventem `RendererEvents::Viewport::FocusChanged`
  (`RendererLayer.cpp:1263`). Jeśli podpięcie `EventBus` we fixture zajmie więcej niż kilkanaście
  minut — **pomiń tę komendę**, dopisz `// TODO` z powodem i odnotuj w „Znaleziskach pobocznych".
  Nudge to cienka delegacja do #3, którą #3 już pokrywa.
- **#5 `Copy`** — nie jest undoable. Napisz zamiast round-tripu test, że `Execute` **nie zmienia**
  `record->structure` i że kolejny `Paste` widzi skopiowane atomy.
- **#9 `SetBondSettings`** — zmień `globalCutoffScale` na tyle, żeby liczba bondów faktycznie się
  zmieniła (sprawdź w asercji, że `bonds.size()` po `Execute` jest inne niż przed). Bez tego test
  niczego nie dowodzi.
- **#12 `SetElementStyle`** — nie dotyka domeny. Round-trip robi się na `AtomStyleTable` i na
  `windowState.structure.atoms[i].color` / `.radius`, nie na `CrystalStructure`.

**Oczekiwany wynik:** część testów **przejdzie**, część **padnie**. To jest w porządku i o to
chodzi. Nie naprawiaj kodu produkcyjnego w tym kroku. Zamiast tego, dla każdego padającego testu
zapisz w sekcji „Znaleziska poboczne":
- nazwa komendy,
- co dokładnie nie wróciło po `Undo` (atomy / bondy / bondSettings / coś innego),
- komunikat asercji.

**Done gdy:** wszystkie komendy undoable mają test round-trip, każdy padający ma wpis w tym pliku.

**Commit:** `test(renderer): undo round-trip for all undoable atom-edit commands`

---

## Krok 8 — napraw to, co testy z kroku 7 wykazały

**Dopiero teraz** dotykasz kodu produkcyjnego, i tylko tego, na co masz padający test.

Dla każdego padającego testu z kroku 7:

1. Ustal, czy to bug, czy świadoma optymalizacja:
   - **bug** — po `Undo` użytkownik widzi inną strukturę niż przed `Execute`;
   - **optymalizacja** — komenda z definicji nie może zmienić tej części stanu (np.
     `SetAtomProperties` zmienia wyłącznie metadane jednego atomu, więc niepamiętanie bondów jest
     poprawne).
2. Jeśli **bug** — dodaj brakujący snapshot i jego przywrócenie w `Undo`. Najmniejsza możliwa
   zmiana, wzorowana na `DeleteSelectedAtomsCommand` (linie 240–241 i 305–306), który robi to
   poprawnie. Test przechodzi na zielono.
3. Jeśli **optymalizacja** — **osłab test, nie kod**: zawęź asercję do tego, co komenda faktycznie
   ma przywracać, i dopisz nad testem komentarz wyjaśniający dlaczego, z odwołaniem do linii
   w `RendererAtomEditCommands.cpp`. Nie usuwaj testu.

**Done gdy:** cały plik testowy zielony, a każde odstępstwo od pełnego round-tripu ma komentarz
uzasadniający.

**Commit:** `fix(renderer): restore full structure state on undo where round-trip tests failed`

---

**KONIEC ETAPU II.** `full-build-verify`, merge `task/19-undo-command-tests` do `main`.

---

# ETAP III — przesunięcia granic (`task/20-boundary-moves`)

Trzy niezależne przesunięcia. Po każdym uruchom checker z kroku 4 — jeśli zgłosi nową krawędź,
zaktualizuj `include_rules.json` **świadomie**, a nie odruchowo.

## Krok 9 — przenieś IO projektowe do `Storage`

**Znalezisko:** architecture-code-review-2026-09-01, finding 6.

**Fakt zweryfikowany:** `src/Storage/` zawiera wyłącznie `StorageLayer.{cpp,hpp}` (pusta warstwa),
podczas gdy ADR-004 przypisuje Storage'owi ciągłość sesji. `ProjectRootsIO`, `RecentProjectsIO`
i `ProjectManifestIO` żyją w `src/IO/`. Ich konsumenci:
`src/Presentation/EditorLayer.{cpp,hpp}` i `src/Presentation/Panels/ProjectTreePanel.{cpp,hpp}`.

**Zakres:** przenieś **tylko `ProjectRootsIO` i `RecentProjectsIO`** (ciągłość sesji).
`ProjectManifestIO` **zostaje w `IO`** na razie — manifest jest bliżej formatu pliku niż stanu
sesji, a jego przeniesienie należy do decyzji o `SaveProject`/`LoadProject`, której jeszcze nie ma.

**TEST FIRST:** sprawdź, czy istnieją testy dotykające tych plików:
```
grep -rn "ProjectRootsIO\|RecentProjectsIO" tests/
```
- Jeśli **są** — to jest twoja siatka; nie zmieniaj ich asercji, tylko include'y po przeniesieniu.
- Jeśli **nie ma** — napisz najpierw jeden test round-trip **przed przeniesieniem**, w obecnej
  lokalizacji: zapisz listę ścieżek do pliku tymczasowego, wczytaj, porównaj. Wzoruj się na
  `tests/IO/*IoEventsTests.cpp`. Ten test przeżyje przeniesienie i udowodni, że nic się nie zepsuło.

**Akcje:**
1. `git mv src/IO/ProjectRootsIO.hpp src/Storage/` oraz `git mv src/IO/ProjectRootsIO.cpp src/Storage/`
2. `git mv src/IO/RecentProjectsIO.hpp src/Storage/` oraz `git mv src/IO/RecentProjectsIO.cpp src/Storage/`
3. Zaktualizuj `#include "IO/..."` → `#include "Storage/..."` w przeniesionych plikach oraz
   w `EditorLayer.cpp`, `EditorLayer.hpp`, `ProjectTreePanel.cpp`, `ProjectTreePanel.hpp`.
   Jeśli test z kroku wyżej też includuje — popraw i tam.
4. Regeneruj projekty.
5. Uruchom checker include'ów. Jeśli pojawi się nowa krawędź `Storage -> X` — dopisz ją do
   `allowed` w `include_rules.json`, ale **tylko jeśli `X` to `Core`**. Jakakolwiek inna krawędź
   ze `Storage` oznacza, że przenoszone pliki zależą od czegoś, czego nie powinny — zatrzymaj się
   i zgłoś.

**Done gdy:** build Release zielony, testy zielone, checker zielony, `src/Storage/` ma 3 jednostki.

**Commit:** `refactor(storage): move session-continuity IO from IO to Storage per ADR-004`

---

## Krok 10 — podziel `RendererWindowState` w miejscu

**Znalezisko:** architecture-code-review-2026-09-01, finding 5, pierwszy krok.

**Zakres:** **wyłącznie** podział wewnątrz tego samego nagłówka. Bez zmiany API, bez przenoszenia
do `Presentation`, bez `const GetWindows()`. To jest przygotowanie, nie egzekwowanie.

**TEST FIRST:** brak nowego testu — to relokacja pól bez zmiany zachowania. Siatką bezpieczeństwa
są testy z Etapu II (dotykają `windowState.structure`, `selectedAtomIndices`, `sceneRegistry`)
plus `tests/Renderer/RendererStartupBootstrapTests.cpp`. **Muszą pozostać zielone bez zmiany
ich treści** — jeśli musisz je edytować, to znaczy, że zmieniłeś API i wyszedłeś poza zakres.

**Akcje:**

1. W `src/Renderer/RendererWindowState.hpp` sklasyfikuj każde ze 133 pól do jednej z trzech grup:
   - **runtime renderera** — kamera, viewport, przełączniki widoczności, ustawienia renderowania;
   - **pochodne dane domenowe** — `structure`, `sceneRegistry`;
   - **stan interakcji UI** — dragi gizmo, popupy, rubber-band, `freeLabelDragging`,
     `sceneArrowGizmoAxis`, `fallbackModalDrag`, `pinnedMeasurementDragLastMouse`,
     `addAtomPopupRequested` i podobne (audyt naliczył ich 53).
2. Wprowadź **jeden zagnieżdżony struct** w `RendererWindowState`:
   ```cpp
   struct UiInteraction { /* grupa 3 */ };
   UiInteraction ui;
   ```
   Grupy 1 i 2 zostają polami bezpośrednimi `RendererWindowState` (to zdecydowana większość
   odwołań — mniejszy diff).
3. Przenieś pola grupy 3 do `ui`. Popraw wszystkie odwołania: `windowState.freeLabelDragging`
   → `windowState.ui.freeLabelDragging`. Kompilator wskaże wszystkie miejsca — idź za błędami.
   Główny konsument to `src/Presentation/Panels/RendererPanel*.cpp`.
4. Nad `struct UiInteraction` dopisz komentarz:
   ```
   // Stan interakcji UI należący do panelu, nie do renderera. Docelowo (finding 5, krok 2)
   // przeniesiony do Presentation, co pozwoli na const GetWindows(). Na razie mieszka tu,
   // żeby granica była widoczna i sprawdzalna przed jej wymuszeniem.
   ```

**Done gdy:** build Release zielony, wszystkie testy zielone **bez edycji plików testowych**.

**Commit:** `refactor(renderer): group UI interaction fields into RendererWindowState::ui`

---

## Krok 11 — round-trip `YamlConfigSerializer`

**Znalezisko:** test-suite-review-2026-09-01, evidence-placement problem 2 i „Then, in order" #4.

`YamlConfigSerializer.cpp` ma ~1943 linie i zero testów bezpośrednich. Cały moduł `App`
(6986 linii) ma zero testów.

**TEST FIRST (RED):**

Utwórz `tests/App/YamlConfigSerializerTests.cpp` (nowy katalog `tests/App/`). Dla każdego z kształtów
konfiguracji, które serializer obsługuje (ustal je czytając jego publiczne API — **nie zgaduj**),
jeden test:

```
zbuduj obiekt konfiguracji z NIEDOMYŚLNYMI wartościami w każdym polu
serializuj do stringa
deserializuj z powrotem
porównaj pole po polu z oryginałem
```

Krytyczne: wartości muszą być **niedomyślne**. Test, w którym wszystkie pola mają wartości domyślne,
przejdzie nawet wtedy, gdy serializer nie zapisze niczego — to jest test-teatr.

Regeneruj projekty (nowy plik testowy). Uruchom. Pola, które nie przeżyją round-tripu, to
znaleziska — zapisz je w „Znaleziskach pobocznych".

**IMPLEMENTACJA (GREEN):** napraw każde pole, które nie przeżyło round-tripu. Jeśli pole jest
świadomie nieserializowane (np. stan runtime), zawęź asercję i dopisz komentarz z uzasadnieniem —
tak samo jak w kroku 8.

**Done gdy:** `tests/App/` istnieje, round-trip zielony dla każdego kształtu konfiguracji.

**Commit:** `test(app): round-trip tests for YamlConfigSerializer`

---

**KONIEC ETAPU III.** `full-build-verify`, merge `task/20-boundary-moves` do `main`.

---

# ETAP IV — ekstrakcja `StructureEditor` (`task/21-structure-editor`)

## Krok 12 — przenieś czasowniki edycji domeny do `Domain/Crystal/StructureEditor`

**Znalezisko:** architecture-code-review-2026-09-01, finding 3. Największy krok planu.

**Warunek wstępny — sprawdź go, zanim zaczniesz:** cały
`tests/Renderer/Commands/RendererAtomEditCommandsTests.cpp` z Etapu II musi być zielony.
**Jeśli nie jest — nie zaczynaj tego kroku.** To jest siatka bezpieczeństwa refaktoru; bez niej
nie masz jak stwierdzić, że nic nie zepsułeś.

**Problem:** `Renderer/Commands/RendererAtomEditCommands.cpp` sięga po
`domainLayer.Workspace().Structures().FindMutable(...)` (linia 87) i pisze wprost do
`target->record->structure.atoms` / `.bonds` (linie 269, 305, 384, 423 i dalej), oraz woła reguły
domenowe (`ApplyVacancy`, `RegenerateAutoBonds`). Moduł, który posiada reprezentację **pochodną**,
jest tym, który wie, jak zmutować **źródło prawdy**. Skutek: druga powierzchnia edycyjna (konsola
skryptowa, mostek Python, wsadowa generacja defektów) musiałaby albo zduplikować tę semantykę,
albo wołać renderer, żeby zmienić strukturę.

**TEST FIRST (RED):**

Utwórz `tests/Domain/Crystal/StructureEditorTests.cpp`. Napisz testy dla API, które **jeszcze nie
istnieje** — będą RED z powodu braku kompilacji, i to jest poprawny stan:

```
TEST(StructureEditorTests, DeleteAtomsRemovesAtomsAndTheirBonds)
TEST(StructureEditorTests, DeleteAtomsReindexesRemainingBonds)
TEST(StructureEditorTests, ApplyVacancyRemovesTheTargetSite)
TEST(StructureEditorTests, TransformAtomsMovesOnlyTheSelectedIndices)
TEST(StructureEditorTests, AddAtomAppendsSiteAndRegeneratesBonds)
TEST(StructureEditorTests, DeleteAtomsRejectsOutOfRangeIndex)
```

Te testy operują **wyłącznie na `CrystalStructure`** — żadnego `RendererLayer`, żadnego
`DomainLayer`, żadnego `windowState`. Na tym polega cały sens tego kroku: reguły edycji struktury
stają się testowalne bez okna renderera.

**IMPLEMENTACJA (GREEN):**

1. Utwórz `src/Domain/Crystal/StructureEditor.hpp` i `.cpp`. API — wyłącznie wolne funkcje, bez
   klasy, bez interfejsu, bez fabryki (ADR-006: abstrakcja dopiero wtedy, gdy zarobiła):
   ```cpp
   namespace DefectStudio::StructureEditor
   {
       [[nodiscard]] Result<void> DeleteAtoms(CrystalStructure &structure, const std::vector<std::size_t> &indices);
       [[nodiscard]] Result<void> TransformAtoms(CrystalStructure &structure, const std::vector<std::size_t> &indices, const std::vector<glm::vec3> &newPositions);
       [[nodiscard]] Result<void> AddAtom(CrystalStructure &structure, const AtomSite &site);
   }
   ```
   Każda funkcja: bierze `CrystalStructure&`, zwraca `Result<void>`, waliduje indeksy, **nie zna
   renderera, okna ani undo**. Snapshot na undo pozostaje odpowiedzialnością wołającego —
   nie wymyślaj tu nowego mechanizmu undo.
2. **Zacznij od najmniejszego zakresu:** przenieś tylko wywołania `ApplyVacancy` i
   `RegenerateAutoBonds` z komend do `StructureEditor`. To są już funkcje domenowe orkiestrowane
   z niewłaściwego modułu — czysta relokacja ~100 linii, zero nowej abstrakcji.
3. Następnie po jednej komendzie na raz przepnij `DeleteSelectedAtoms`, `TransformSelectedAtoms`
   i `AddAtomAtCoordinates` na `StructureEditor`. **Po każdej pojedynczej komendzie uruchom
   testy z Etapu II.** Nie przepinaj dwóch naraz.
4. Komendy w `Renderer/Commands` zostają jako cienkie adaptery `ICommand`: rozwiąż okno
   (`ResolveAtomEditTarget`), zrób snapshot na undo, zawołaj `StructureEditor`, odśwież pochodny
   snapshot (`RebuildAndSync`).
5. Regeneruj projekty (nowe pliki).
6. Uruchom checker include'ów. Nie powinna pojawić się żadna nowa krawędź — `Domain` nadal zależy
   wyłącznie od `Core` i `Domain`. Jeśli pojawi się `Domain -> cokolwiek innego`, **cofnij zmianę**:
   to jest naruszenie twardej granicy z `CLAUDE.md`.

**Czego NIE robić w tym kroku:**
- nie przenoś wszystkich 12 komend (tylko 3 wymienione wyżej — reszta osobnym zadaniem);
- nie wprowadzaj klasy bazowej `StructureEditCommand` (osobna decyzja, poza zakresem);
- nie zmieniaj sygnatur fabryk `Create*Command` — panele ich używają.

**Done gdy:** `StructureEditorTests` zielone, testy z Etapu II zielone **bez zmian w ich treści**,
checker zielony, build Release zielony.

**Commit:** `refactor(domain): extract StructureEditor, move domain mutation out of Renderer`

---

**KONIEC ETAPU IV.** `full-build-verify`, merge `task/21-structure-editor` do `main`.

---

# Świadomie poza zakresem tego planu

Zapisane, żeby nie wracały jako „a co z…":

- **Pełne rozerwanie cykli `Renderer ↔ IO` i `Renderer ↔ Events`** (finding 1, kroki dalsze niż
  `UiConfig`). Checker z kroku 4 trzyma je jako known-exceptions, więc nie rosną. Rozerwanie
  wymaga decyzji o kontrakcie DTO między IO a Rendererem — to osobne zadanie.
- **`const GetWindows()` i przeniesienie `UiInteraction` do `Presentation`** (finding 5, krok 2).
  Dotyka 187 miejsc w `RendererPanel*.cpp`. Robić dopiero, gdy podział z kroku 10 się obroni.
- **Prawdziwe CI** (test-suite-review, „Then, in order" #2). Wymaga decyzji o hostingu.
  `ci_check.py` po krokach 4 i 5 robi to samo lokalnie.
- **Uwidocznienie pominięć Pythona** (test-suite-review #3). Warte zrobienia, ale 2 pominięte testy
  w tym buildzie są znane i udokumentowane w `CLAUDE.md` — problem jest chwilowo martwy.
- **Rozbicie `YamlConfigSerializer`** (1943 linie). Krok 11 daje mu testy; rozbicie dopiero przy
  następnym większym rozszerzeniu schematu, zgodnie z rekomendacją z audytu lipcowego.
- **Przeniesienie `ProjectManifestIO` do `Storage`** — czeka na decyzję o `SaveProject`/`LoadProject`.
- **Testy renderowania GPU i sterowanie ImGui z harnessu** — audyt testów uznaje ich brak za
  poprawny, nie za zaniedbanie. Nie dodawać.

---

# Znaleziska poboczne

Sekcja do wypełniania **w trakcie** wykonywania planu. Nie zostawiaj jej pustej, jeśli coś
zauważysz — i nie przerywaj kroku, żeby to naprawić.

| Krok | Znalezisko | Plik:linia | Status |
|---|---|---|---|
| | | | |

## Baseline clang-tidy (wypełnić w kroku 5)

- Data uruchomienia:
- Łączna liczba ostrzeżeń:
- Rozbicie na kategorie:

## Wynik testów round-trip undo (wypełnić w kroku 7)

| Komenda | Wynik | Co nie wróciło po Undo | Werdykt (bug / optymalizacja) |
|---|---|---|---|
| | | | |
