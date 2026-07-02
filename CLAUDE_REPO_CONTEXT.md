# Defect Studio — kontekst repozytorium dla Claude

Ten dokument ma pomóc Claude w przygotowaniu jednego spójnego pliku rozwojowego na podstawie:

- `docs/work/project/TODO.md`
- `docs/work/project/old-ds-functionality.md`

Claude dostanie też dostęp do repozytorium, więc ten plik nie zastępuje analizy kodu. Jest mapą startową: co w projekcie już istnieje, gdzie szukać kluczowych systemów, jakie granice architektoniczne obowiązują i jak czytać oba dokumenty wejściowe.

## Cel zadania dla Claude

Zadaniem Claude jest połączenie `TODO.md` i `old-ds-functionality.md` w jeden dokument, który stanie się osią rozwoju repozytorium.

W praktyce:

- `TODO.md` traktuj jako aktualny szkielet techniczny, kolejność milestone'ów i listę statusów.
- `old-ds-functionality.md` traktuj jako katalog funkcjonalności produktowych i inspirację z poprzedniej wersji / VESTA-like workflow.
- Wynik nie powinien być prostym sklejeniem list. Ma być roadmapą: milestone'y, zależności, zakres MVP, statusy `Done / Partial / Planned / Backlog`, oraz jasne granice, co należy zrobić przed czym.
- Jeżeli oba dokumenty mówią o tym samym innymi słowami, zachowaj jedną pozycję i dopisz źródło/uzasadnienie.
- Jeżeli `TODO.md` jest nieaktualny względem kodu, pierwszeństwo ma kod.

## Źródła nawigacji

Repozytorium ma graf wiedzy w `graphify-out/`.

Przed szeroką nawigacją po kodzie używaj:

```powershell
graphify query "<pytanie>"
graphify explain "<symbol albo plik>"
graphify path "<A>" "<B>"
```

Aktualny graf po ostatnim refactorze:

- commit bazowy: `f753418`
- gałąź: `refactor/full-plan`
- rozmiar grafu: ok. 67k węzłów i 133k krawędzi
- `graphify-out/GRAPH_REPORT.md` istnieje, ale jest bardzo duży i mniej użyteczny niż celowane `query/explain/path`

Po zmianach w kodzie uruchamiaj:

```powershell
graphify update .
```

## Sens projektu

Defect Studio to desktopowa aplikacja naukowa w C++ do pracy ze strukturami krystalicznymi, defektami i danymi z workflow DFT/VASP.

Docelowo ma być:

- narzędziem typu VESTA-like, ale pisanym z czystszą architekturą,
- bazą pod dużą aplikację do analizy danych,
- bazą pod mniejsze prototypy fizyczne,
- środowiskiem, gdzie Python obsługuje ekosystem naukowy, a C++ trzyma UI, renderer, runtime i trwały model projektu.

Najważniejsza zasada: szybkie MVP, ale bez psucia fundamentów. Nie dodawać abstrakcji "na zapas", ale też nie przecinać granic warstw dla chwilowej wygody.

## Top-level layout

- `src/` — właściwy kod aplikacji C++.
- `tests/` — testy GoogleTest, podzielone podobnie do `src/`.
- `docs/work/project/` — dokumenty planistyczne; tu są wejściowe `TODO.md` i `old-ds-functionality.md`.
- `docs/work/` — review, notatki architektoniczne i dokumenty robocze.
- `install/app/` — runtime aplikacji: assets, dane startowe, konfiguracje, lokalny Python.
- `scripts/` — tooling build/setup/generate/verify, preferowane wejście do operacji developerskich.
- `Vendor/` — zależności vendoryzowane.
- `build/` — artefakty generowane; nie traktować jako źródła prawdy.
- `graphify-out/` — graf wiedzy; używać do nawigacji, nie edytować ręcznie.

Build systemem jest Premake (`premake5.lua`). Główne targety:

- `DefectStudio`
- `DefectStudioTests`

Konfiguracje:

- `Debug`
- `Release`
- `Dist`

## Organizacja `src/`

### `src/App`

Warstwa aplikacji i composition root.

Najważniejsze pliki:

- `Application.hpp/.cpp`
- `ApplicationBootstrap.cpp`
- `ApplicationState.hpp`
- `Controllers/ApplicationConfigController.*`
- `Managers/ConfigManager.*`
- `Serialization/YamlConfigSerializer.*`
- `RendererStartupComposer.*`

Rola:

- cykl życia aplikacji,
- tworzenie okna i grafiki,
- konfiguracja runtime,
- składanie warstw,
- inicjalizacja usług współdzielonych,
- kontrolery zdarzeń aplikacyjnych.

Uwaga architektoniczna: `App` ma być composition rootem i koordynatorem startu, ale nie miejscem na logikę domeny, IO, renderera ani UI. Jeżeli coś rośnie w `ApplicationBootstrap.cpp`, najpierw sprawdź, czy nie powinno trafić do modułu właściciela.

Aktualna kolejność warstw w startupie:

1. `CoreLayer`
2. `IOLayer`
3. `StorageLayer`
4. `ScientificRuntimeLayer`
5. `DomainLayer`
6. `RendererLayer`
7. `ImGuiLayer`
8. `EditorLayer`

`RendererStartupComposer` składa startup renderera: ładuje assety, prosi `ScientificRuntimeLayer` o struktury, rejestruje je w `DomainLayer::Workspace()`, a dopiero potem buduje snapshoty dla renderera.

### `src/Core`

Fundament aplikacji. Tu są systemy, które inne moduły powinny wykorzystywać zamiast tworzyć własne mechanizmy.

Najważniejsze podsystemy:

- `Core/EventSystem/BusEventSystem` — event bus dla komunikacji między warstwami i usługami.
- `Core/EventSystem/DispatchingEventSystem` — zdarzenia platformy, okna i inputu.
- `Core/Commands` — rejestr komend, wykonanie komend, integracja z undo.
- `Core/Input` — key chords, keymapy, konteksty, resolver skrótów.
- `Core/Undo` — globalny undo/redo dla komend aplikacyjnych.
- `Core/JobSystem` — joby w tle, anulowanie, retry, priority, lifecycle events.
- `Core/ProgressTrackingSystem` — stan postępu jobów i snapshoty dla UI.
- `Core/Diagnostics` — `StructuredError`, kategorie błędów, user-facing details.
- `Core/Capabilities` — capability flags dla runtime features.
- `Core/Assets` — rejestracja i walidacja assetów logicznych.
- `Core/Notifications` — powiadomienia użytkownika publikowane przez event bus.
- `Core/Logging` — logging i runtime diagnostics.
- `Core/Platform` — okno, procesy zewnętrzne, runtime platformowy.
- `Core/Utils/Path` — normalizacja ścieżek i wrappery filesystem.

`CoreLayer` inicjalizuje `EventBus`, `JobSystem`, `ProgressTracker`, `UndoStack`, `CommandRegistry`, `CommandService`, `KeymapResolver` i `ContextManager`.

Zasada: jeżeli potrzebujesz komunikacji, skrótów, zadań w tle, postępu, błędów, notyfikacji, capability albo assetów — najpierw użyj istniejącego systemu z `Core`.

### `src/Domain`

Model domenowy i runtime state projektu.

Najważniejsze elementy:

- `Domain/Crystal/CrystalStructure.*`
- `Domain/Crystal/LatticeCell.*`
- `Domain/Crystal/AtomSite.hpp`
- `Domain/Crystal/VacancySite.hpp`
- `Domain/Crystal/ElementProperties.*`
- `DomainLayer.*`
- `ProjectWorkspace.*`

Aktualnie `ProjectWorkspace` jest początkiem domenowego źródła prawdy:

- `StructureRegistry` przechowuje załadowane struktury,
- każdy rekord ma stabilne `StructureId`,
- renderer dostaje snapshot z `domainStructureId`, ale nie staje się właścicielem domeny.

Docelowo w tej warstwie powinny lądować: struktury, kolekcje, defekty, konfiguracje defektów, rekordy obliczeń, indeksy i zapytania project-scoped.

### `src/Events`

Wspólne kontrakty zdarzeń, które nie powinny żyć w pojedynczym panelu lub implementacji.

Obecnie ważne:

- `RendererEvents.hpp` — zdarzenia konfiguracji renderera i viewportu.
- `EditorUiEvents.hpp` — zdarzenia UI, layoutów, theme, persistence.

Zasada: zdarzenie powinno wyrażać intencję lub fakt na granicy modułów. Mutację stanu wykonuje właściciel stanu, nie nadawca zdarzenia.

### `src/IO`

Warstwa odczytu/zapisu plików i obsługi eventów persistence.

Przykłady:

- `AtomStyleIO.*`
- `ElementPropertiesIO.*`
- `PeriodicTableIO.*`
- `RendererMeshIO.*`
- `RendererStartupLayoutIO.*`
- `TextFileIO.*`
- `IOLayer.*`

Uwaga: `IO` ma być surowym IO i persistence, nie miejscem mostów domena-renderer. Most `CrystalStructure -> RendererStructureData` jest w `src/Renderer/StructureRendererDataBuilder.*`, bo buduje dane renderera.

### `src/Renderer`

Renderer, dane widoku, backend OpenGL i komendy viewportu.

Najważniejsze elementy:

- `RendererLayer.*`
- `RendererTypes.hpp`
- `RendererWindowState.hpp`
- `RendererConfig.hpp`
- `StructureRendererDataBuilder.*`
- `RendererStartupBootstrap.*`
- `RendererAssetBundle.*`
- `OpenGl/OpenGlRendererBackend.*`
- `OpenGl/OpenGlShaderLibrary.*`
- `Commands/RendererCommandRegistration.*`
- `Commands/RendererViewportCommands.*`

`RendererLayer` jest właścicielem stanu renderera:

- okna/viewporty,
- kamery,
- local view undo stack,
- wybór atomów w viewportach,
- globalne settings renderera,
- komunikacja z OpenGL backendem.

Lokalny undo-stack widoku jest celowy: dotyczy konkretnego viewportu/struktury i nie powinien przypadkiem zmieniać innego okna. To jest oddzielne od globalnego `Core/Undo`.

Renderer nie powinien zależeć od `App/ApplicationState.hpp`. Konfiguracja renderera jest w `RendererConfig.hpp`, a runtime apply idzie przez `RendererEvents::Config::Applied`.

### `src/Presentation`

Warstwa ImGui i panele użytkownika.

Najważniejsze elementy:

- `ImGuiLayer.*`
- `EditorLayer.*`
- `Panels/RendererPanel.*`
- `Panels/PanelRegistry.*`
- `Panels/SettingsPanel.*`
- `Panels/LoggingPanel.*`
- `Panels/TaskMonitorWindow.*`
- `Panels/ProgressMonitorWindow.*`

Zasada: UI nie powinno posiadać domeny ani renderera. Panel publikuje komendy/zdarzenia albo wywołuje jasno wyznaczone API warstwy. Przykład: `RendererPanel` prosi o selekcję atomu przez `RendererEvents::Viewport::AtomSelectionRequested`, a stan zmienia `RendererLayer`.

### `src/ScientificRuntime`

Most do Pythona i bibliotek naukowych.

Najważniejsze elementy:

- `ScientificRuntimeLayer.*`
- `Python/ScientificPythonRuntime.*`
- `Python/PymatgenBridge.*`
- `Python/ASEBridge.*`
- `Python/PythonInterpreter.*`
- `Python/ScriptRunner.*`
- `Python/PythonScriptJob.*`

Rola:

- import struktur przez pymatgen,
- konwersje przez ASE/pymatgen,
- praca w JobSystem,
- capability `PythonBridge`,
- obsługa błędów jako `StructuredError`.

Procesy Pythona powinny mieć kontrakt anulowania i timeoutu. `ScriptRunner` przekazuje to do `Core/Platform/ProcessRunner`, a joby Pythona respektują `JobContext::IsCancellationRequested()`.

### `src/Storage`, `src/Debug`, `src/Demo`

- `Storage` jest obecnie małą warstwą pod przyszły system projektu/persistence.
- `Debug` zawiera debug layer.
- `Demo` zawiera demonstracje systemów i nie powinno być traktowane jako architektura produkcyjna.

## Główne przepływy

### Startup renderera

1. `Application` inicjalizuje config, event bus, asset manager i warstwy.
2. `ScientificRuntimeLayer` startuje Python bridge, jeżeli runtime jest dostępny.
3. `RendererStartupComposer` ładuje renderer assets przez `AssetManager`.
4. Startup layout wskazuje struktury do wczytania.
5. `ScientificRuntimeLayer::LoadCrystalStructure()` zwraca `CrystalStructure`.
6. `DomainLayer::Workspace().Structures().Add()` rejestruje strukturę jako domenowe źródło prawdy.
7. `BuildRendererStructureData()` buduje snapshot dla renderera.
8. `RendererLayer` dostaje `RendererStartupConfig` i tworzy viewporty.

### Konfiguracja runtime

1. UI albo inna warstwa publikuje `AppEvents::Config::ApplyRequested`.
2. `ApplicationConfigController` waliduje i aplikuje runtime config.
3. Publikuje `AppEvents::Config::Applied` dla aplikacji/UI.
4. Publikuje `RendererEvents::Config::Applied` dla renderera.
5. `IOLayer` obsługuje persistence, gdy zdarzenie tego wymaga.

### Skrót klawiaturowy / komenda / viewport

1. Input platformowy trafia do dispatching event system.
2. `CoreLayer` używa `KeymapResolver` i `ContextManager`.
3. `CommandService` wykonuje komendę z `CommandRegistry`.
4. Komenda publikuje event przez `EventBus`.
5. Właściciel stanu, np. `RendererLayer`, obsługuje event i wykonuje mutację.

### Job Pythona

1. UI lub runtime publikuje job request.
2. `CoreLayer` przekazuje go do `JobSystem`.
3. `PythonScriptJob` wykonuje `ScriptRunner`.
4. `ScriptRunner` uruchamia subprocess przez `ProcessRunner` albo używa runtime Pythona.
5. Anulowanie idzie przez `JobContext`.
6. Błędy przechodzą jako `StructuredError`, a postęp jako job/progress events.

## Zasady projektowe

### Granice warstw

- `Domain` nie zależy od UI, rendererów ani App.
- `Renderer` może zależeć od domenowych typów wejściowych przy budowie snapshotu, ale nie jest źródłem prawdy domenowej.
- `IO` czyta i zapisuje pliki; nie powinno zawierać logiki transformującej domenę do konkretnego widoku.
- `Presentation` renderuje UI i zbiera intencje użytkownika; nie powinna po cichu mutować stanu innych modułów.
- `App` składa systemy, ale nie powinien puchnąć w orkiestrator wszystkiego.

### Komunikacja

- Cross-layer: `EventBus`.
- Platform/window/input: dispatching event system.
- Akcje użytkownika: `CommandRegistry` + `CommandService` + keymap.
- Długie operacje: `JobSystem` + `ProgressTracker`.
- User-facing błędy: `StructuredError` + notifications/logging.

### Ownership

- Właściciel stanu mutuje stan.
- Inne moduły wysyłają request/event albo używają wąskiego API.
- Nie przekazywać dużych mutable struktur między panelami bez jasnego właściciela.
- Snapshot renderera nie jest modelem domenowym.

### Minimalizm

Repozytorium ma zasadę "Ponytail": najmniejsza poprawna zmiana po zrozumieniu realnego flow.

- Najpierw użyj istniejących systemów.
- Nie dodawaj zależności, jeśli wystarczy standard library albo vendoryzowany komponent.
- Nie twórz abstrakcji bez aktualnego zastosowania.
- Nie psuj walidacji, obsługi błędów, anulowania, persistence ani bezpieczeństwa danych.

### Wątek główny

Zasada projektu: tylko main thread commituje stan widoczny w projekcie/UI. Joby przygotowują dane i publikują wynik/intencję do głównego runtime.

### Render path

Ścieżki renderowania powinny być możliwie exception-free. Błędy ładowania i walidacji obsługiwać przed wejściem w gorące ścieżki OpenGL.

## Aktualne systemy do ponownego użycia

Nie tworzyć nowego mechanizmu, dopóki nie sprawdzisz tych systemów:

- `Core/EventSystem/BusEventSystem`
- `Core/EventSystem/DispatchingEventSystem`
- `Core/Commands`
- `Core/Input`
- `Core/Undo`
- `Core/JobSystem`
- `Core/ProgressTrackingSystem`
- `Core/Diagnostics`
- `Core/Capabilities`
- `Core/Assets`
- `Core/Notifications`
- `Core/Logging`
- `Core/Utils/Path`
- `App/Managers/ConfigManager`
- `App/Serialization`
- `IO`
- `Renderer` events and `RendererLayer`
- `Presentation` panels and `EditorLayer`

## Testy i walidacja

Typowe komendy:

```powershell
python scripts/python/build.py
python scripts/python/build.py --target DefectStudioTests
build/bin/Release-windows-x86_64/DefectStudioTests/DefectStudioTests.exe --gtest_brief=1
```

Projekt ma testy dla:

- `Core`
- `Domain`
- `IO`
- `Presentation`
- `Renderer`
- `ScientificRuntime`

Uwaga praktyczna: po ostatnim refactorze pełna kompilacja testów przechodziła, a w pełnym uruchomieniu testów obserwowany był pojedynczy fail w `KeyBindingTests.ResolverRejectsConflictingBindingInSameLayerAndContext`. Przed traktowaniem tego jako aktualnego stanu uruchom testy ponownie.

## Jak połączyć `TODO.md` i `old-ds-functionality.md`

### Rola `TODO.md`

`TODO.md` jest roadmapą techniczną:

- T01-T06 to fundament i renderer MVP.
- T07-T15 to dalsze milestone'y.
- Statusy w tym pliku bywają częściowe; trzeba je sprawdzić z kodem.
- Dokument pilnuje kolejności: najpierw działający produkt end-to-end, potem polish/refactor.

### Rola `old-ds-functionality.md`

`old-ds-functionality.md` jest katalogiem funkcjonalności:

- import/export POSCAR/CHG,
- viewport i interakcje,
- selekcja, transformacje, atom operations,
- bonds, pomiary, kolekcje, grupy,
- obiekty sceny, 3D cursor,
- projekt i persistence,
- settings,
- render image,
- panele,
- skróty,
- pomysły naukowe: defekty, supercell, space group, DFT, RDF, eksporty.

Nie wszystkie te funkcje powinny wejść do MVP. Część to backlog po T15.

### Sugerowana struktura nowego dokumentu

1. Cel produktu i zasady architektoniczne.
2. Aktualny stan repozytorium.
3. Milestone map: T01-T15 z krótkim statusem.
4. Feature matrix: funkcja, źródło, status, milestone, zależności, owner module.
5. MVP path: minimalny przepływ użytkownika do osiągnięcia najpierw.
6. Backlog po MVP.
7. Definicje ukończenia dla milestone'ów.
8. Ryzyka architektoniczne i guardrails.

### Mapowanie funkcji na milestone'y

- Import/export POSCAR, CIF, XYZ: głównie T07, częściowo T10.
- Multi-structure, kolekcje, wybór aktywnej struktury: T08 + T10 + T11.
- Selekcja, transformacje, add/delete/change atom: T08.
- Undo/redo operacji sceny: T08, ale rozróżnić globalny `Core/Undo` i lokalny view undo w rendererze.
- Bonds i auto-bond: T08, późniejsze optymalizacje T09.
- Pomiary: T08/T09.
- Multi-viewport: w dużej części już istnieje w rendererze; sprawdzić kod i oznaczyć jako `Done/Partial`.
- Renderer advanced, labels, image export: T09/T15.
- Project system, manifest, recent projects, autosave: T10.
- Defekty jako pierwszorzędny model naukowy: T11.
- Settings, wygląd, skróty, conflict detection: T12.
- Space group analyzer: T13.
- CHG/CHGCAR/PARCHG, isosurfaces, volumetrics: T14.
- Offscreen render/export: T15.
- DFT workflow, VASP/QE execution, formation energy: backlog po T15 lub osobny epik naukowy.

### Rzeczy już częściowo istniejące w kodzie

W nowej roadmapie nie oznaczaj ich jako czyste `TODO` bez sprawdzenia:

- `ProjectWorkspace` i `StructureRegistry` istnieją jako początek T11.
- `CrystalStructure`, `LatticeCell`, `AtomSite` istnieją jako początek T07.
- Renderer OpenGL, atomy, bonds, grid, unit cell, viewport camera, startup assets i multi-window są mocno rozpoczęte.
- `ScientificRuntimeLayer`, `PymatgenBridge`, `ASEBridge`, `PythonScriptJob` istnieją.
- `JobSystem`, `ProgressTracker`, `EventBus`, `CommandRegistry`, `KeymapResolver`, `UndoStack` istnieją.
- `RendererConfig` jest wydzielony z `App`.
- Most `CrystalStructure -> RendererStructureData` jest w rendererze, nie w IO.

### Rzeczy wymagające ostrożności

- Nie mieszać pojęć "struktura domenowa", "scena renderera", "kolekcja UI" i "projekt na dysku".
- Nie robić z `RendererLayer` właściciela modelu naukowego.
- Nie robić z `IOLayer` warstwy biznesowej.
- Nie traktować `old-ds-functionality.md` jako wymagań na najbliższy sprint; to raczej pełny katalog ambicji.
- Nie usuwać lokalnego undo widoku renderera — jest celowy dla per-window/per-structure nawigacji.
- Nie planować volumetrics przed stabilnym modelem struktury/projektu, chyba że jako osobny spike.
- Nie dodawać nowego systemu eventów/komend/jobów obok istniejących.

## Najważniejsze pliki do sprawdzenia przy tworzeniu roadmapy

- `docs/work/project/TODO.md`
- `docs/work/project/old-ds-functionality.md`
- `docs/work/architecture-code-review-2026-07-01.md`
- `AGENTS.md`
- `README.md`
- `premake5.lua`
- `src/App/Application.hpp`
- `src/App/ApplicationBootstrap.cpp`
- `src/App/RendererStartupComposer.cpp`
- `src/Core/CoreLayer.hpp`
- `src/Core/EventSystem/BusEventSystem/EventBus.hpp`
- `src/Core/JobSystem/JobSystem.hpp`
- `src/Core/Commands/CommandRegistry.hpp`
- `src/Core/Input/KeyBinding.hpp`
- `src/Domain/ProjectWorkspace.hpp`
- `src/Domain/Crystal/CrystalStructure.hpp`
- `src/Events/RendererEvents.hpp`
- `src/IO/IOLayer.hpp`
- `src/Renderer/RendererLayer.hpp`
- `src/Renderer/StructureRendererDataBuilder.hpp`
- `src/Renderer/OpenGl/OpenGlRendererBackend.hpp`
- `src/Presentation/EditorLayer.hpp`
- `src/Presentation/Panels/RendererPanel.cpp`
- `src/ScientificRuntime/ScientificRuntimeLayer.hpp`
- `src/ScientificRuntime/Python/PymatgenBridge.hpp`
- `src/ScientificRuntime/Python/PythonScriptJob.hpp`

## Preferowany język i styl nowej roadmapy

- Po polsku.
- Konkretnie, bez marketingu.
- Jeden dokument ma prowadzić rozwój repozytorium, więc unikaj listy życzeń bez priorytetów.
- Każdy większy punkt powinien mieć: status, zależności, moduły, testowalny rezultat.
- Zachowaj rozróżnienie: `MVP`, `Next`, `Backlog`.
- Jeśli coś wymaga decyzji architektonicznej, oznacz to jako decyzję, a nie ukryty TODO.

