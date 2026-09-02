# Architektoniczne code review repozytorium

Repozytorium: `Defect-Studio-mgr`
Data: 2026-07-01
Zakres: granice modulow, komunikacja, uzycie systemow, API, skalowalnosc, koszt utrzymania.

## Metoda

Nawigacja zaczela sie od `graphify query`, zgodnie z instrukcja repozytorium. Graf byl uzyteczny jako mapa, ale raport `graphify-out/GRAPH_REPORT.md` jest zbudowany z commita `69a98110`, a aktualny HEAD to `97ade3a98a4f7607d8d6033c81402bae6495a2f7`, wiec kazdy wniosek ponizej zostal zweryfikowany bezposrednio w aktualnym kodzie.

## Werdykt

Kod jest po sensownym refaktorze w kilku miejscach: `Renderer` nie jest juz zarazony `ImGuiKey`, skroty renderera przeszly przez `KeymapResolver`/`CommandRegistry`, GIL zostal wyjety z `Core/JobSystem` do hooka rejestrowanego przez `ScientificRuntime`, a uruchamianie skryptow Python idzie przez `ProcessRunner`, nie przez `std::system`.

To nadal nie jest jeszcze bezpieczny fundament pod duza aplikacje naukowa. Najwiekszy problem nie jest juz kosmetyczny, tylko strukturalny: rzeczywisty runtime naukowy nie ma wlasciciela w domenie. Struktury trafiaja przez `ApplicationBootstrap` prosto do `RendererWindowState`, a `DomainLayer` i `StorageLayer` sa puste. To grozi tym, ze renderer i UI zostana przypadkowym modelem projektu.

## Znaleziska

### P0. Brak domenowego zrodla prawdy dla danych naukowych

`ADR-002` i `ADR-009` mowia, ze domena ma byc zrodlem prawdy, a `ProjectWorkspace` ma byc runtime containerem dla struktur i rejestrow domenowych (`docs/work/architecture/adr/ADR-002-domain-as-source-of-truth-and-ecs-boundary.md:80`, `docs/work/architecture/adr/ADR-009-domain-runtime-model-and-scientific-entities.md:18`). Aktualny przeplyw startowy omija to:

- `DomainLayer` i `StorageLayer` sa warstwami pustymi (`src/Domain/DomainLayer.cpp:13`, `src/Storage/StorageLayer.cpp:13`).
- `ApplicationBootstrap` laduje strukture przez `ScientificRuntimeLayer::LoadCrystalStructure` (`src/App/ApplicationBootstrap.cpp:916`).
- Ten sam bootstrap natychmiast konwertuje ja do `RendererStructureData` (`src/App/ApplicationBootstrap.cpp:930`).
- `RendererWindowState` przechowuje `RendererStructureData structure` jako runtime state okna (`src/Renderer/RendererWindowState.hpp:29`).

Skutek: renderer staje sie pierwszym realnym wlascicielem struktury po imporcie. To jest slabe miejsce pod przyszle save/load, analizy, identyfikatory atomow, defekty, selection mapping i porownywanie wynikow. Przy duzej aplikacji naukowej ten blad bedzie drogi, bo kazda nowa funkcja bedzie musiala zgadywac, czy prawda jest w domenie, scenie, rendererze czy panelu.

Minimalna naprawa: zanim dojdzie kolejna fizyka, wprowadzic maly `ProjectWorkspace` z `StructureRegistry`. Import powinien zwracac `CrystalStructure` do domeny, a renderer powinien dostawac tylko pochodny snapshot z identyfikatorem domenowym. Nie trzeba od razu budowac pelnego systemu projektu, ale musi istniec jeden wlasciciel struktur.

### P0. Undo widoku renderera omija globalny system komend

Repo ma juz `UndoStack`, `CommandService` i `SetCameraViewCommand`, ale widok renderera nadal ma lokalna historie:

- `RendererWindowState` trzyma `viewUndoHistory`, `viewRedoHistory` i stan interakcji (`src/Renderer/RendererWindowState.hpp:58`).
- `RendererLayer` recznie wypycha zmiany do tych wektorow (`src/Renderer/RendererLayer.cpp:757`).
- `UndoViewChange`/`RedoViewChange` odtwarzaja lokalne snapshoty (`src/Renderer/RendererLayer.cpp:389`, `src/Renderer/RendererLayer.cpp:406`).
- `SetCameraViewCommand::Execute` i `Undo` sa puste (`src/Renderer/Commands/SetCameraViewCommand.cpp:19`, `src/Renderer/Commands/SetCameraViewCommand.cpp:24`).
- Komendy viewportu publikuja eventy, ale same nie sa undoable (`src/Renderer/Commands/RendererViewportCommands.cpp:15`, `src/App/ApplicationBootstrap.cpp:294`).

Skutek: sa dwa modele cofania zmian. Globalne `Ctrl+Z` idzie przez `UndoStack`, a widok renderera ma osobne `renderer.undo_view`. To skaluje sie zle przy pierwszej funkcji edycyjnej, ktora laczy widok, selekcje i zmiane domenowa.

Minimalna naprawa: albo usunac `SetCameraViewCommand` jako martwy abstrakt, albo lepiej dokonczyc go i przepiac zmiany kamery na `CommandService`/`UndoStack`. Lokalne wektory historii w `RendererWindowState` powinny zniknac.

### P0. `Presentation` mutuje wewnetrzny stan `Renderer`

`RendererPanel` dostaje mutowalny dostep do `RendererLayer::GetWindows()` (`src/Renderer/RendererLayer.hpp:68`, `src/Presentation/Panels/RendererPanel.cpp:76`) i operuje bezposrednio na `RendererWindowState`:

- ustawia rozmiar viewportu i modyfikuje kamere (`src/Presentation/Panels/RendererPanel.cpp:114`, `src/Presentation/Panels/RendererPanel.cpp:116`);
- odpytuje renderer o renderowanie FBO (`src/Presentation/Panels/RendererPanel.cpp:121`);
- wykonuje picking atomow i zapisuje `selectedAtomIndices` (`src/Presentation/Panels/RendererPanel.cpp:170`, `src/Presentation/Panels/RendererPanel.cpp:222`);
- toolbar kopiuje `RendererViewCamera` i publikuje czesc akcji, ale rownoczesnie edytuje parametry stanu okna (`src/Presentation/Panels/RendererPanelToolbar.cpp:180`, `src/Presentation/Panels/RendererPanelToolbar.cpp:323`);
- input panel wywoluje `BeginViewInteraction`/`CommitViewInteraction` bezposrednio na warstwie renderera (`src/Presentation/Panels/RendererPanelInput.cpp:56`, `src/Presentation/Panels/RendererPanelInput.cpp:65`).

Skutek: eventy istnieja, ale granica nie jest konsekwentna. UI zna za duzo o strukturze stanu renderera, a renderer nie kontroluje swoich invariants. Chirurgiczna zmiana w modelu okna bedzie wymagac zmian w kilku plikach UI.

Minimalna naprawa: zostawic renderowanie FBO jako jawny port renderera, ale mutacje przeprowadzac przez eventy/komendy albo waski kontroler viewportu. `GetWindows()` powinno zwracac read-only view model dla UI; selekcja powinna byc zdarzeniem z id domenowym lub indeksami mapowanymi przez bridge, nie bezposrednia edycja wektora w stanie renderera.

### P1. `App` nie jest tylko composition rootem

Kilka nizszych modulow zalezy od typow lub singletona `App`:

- `RendererLayer.cpp` includuje `App/ApplicationState.hpp` i `App/Events/ApplicationConfigEvents.hpp` (`src/Renderer/RendererLayer.cpp:5`, `src/Renderer/RendererLayer.cpp:6`).
- `RendererLayer::ApplyConfig` przyjmuje caly `ApplicationConfig`, choc potrzebuje tylko wycinka rendererowego (`src/Renderer/RendererLayer.hpp:45`).
- `ImGuiLayer.cpp` includuje `App/Application.hpp` (`src/Presentation/ImGuiLayer.cpp:22`).
- Fatal popup w `ImGuiLayer` wywoluje `Application::Get().Shutdown()` (`src/Presentation/ImGuiLayer.cpp:392`), mimo ze istnieje juz `CoreEvents::ShutdownRequested` (`src/Core/Commands/SystemCommands.cpp:50`).

Skutek: `App` przestaje byc tylko miejscem skladania systemu i staje sie zaleznoscia runtime dla warstw. To utrudni testowanie modulu renderera, reuse w prototypach i pozniejsze rozdzielenie aplikacji naukowej od shell/debug UI.

Minimalna naprawa: `RendererLayer` powinien dostawac `RendererConfig` albo dedykowany event konfiguracyjny niezalezny od `ApplicationConfig`. `ImGuiLayer` powinien publikowac `CoreEvents::ShutdownRequested` przez `EventBus` albo wykonac `app.quit` przez `CommandService`, bez zaleznosci od `Application`.

### P1. `ApplicationBootstrap` jest zbyt duzym wezlem orkiestracji

`ApplicationBootstrap.cpp` ma ponad tysiac linii i sklada w sobie crash handling, CLI, config discovery, layer setup, asset loading, Python-backed startup scene, komendy renderera i wiring core services. Najbardziej ryzykowny fragment to `setupDefaultLayers` (`src/App/ApplicationBootstrap.cpp:843`):

- tworzy warstwy techniczne (`src/App/ApplicationBootstrap.cpp:862`, `src/App/ApplicationBootstrap.cpp:868`, `src/App/ApplicationBootstrap.cpp:874`);
- laduje renderer asset bundle (`src/App/ApplicationBootstrap.cpp:881`);
- pobiera struktury przez Python runtime (`src/App/ApplicationBootstrap.cpp:916`);
- konwertuje domenowe struktury do danych renderera (`src/App/ApplicationBootstrap.cpp:930`);
- buduje okna renderera (`src/App/ApplicationBootstrap.cpp:938`);
- rejestruje komendy renderera w innym etapie (`src/App/ApplicationBootstrap.cpp:1084`).

Skutek: `App` jest poprawnym miejscem na composition root, ale obecnie ma tez zbyt duzo logiki workflow. To zwieksza koszt kazdej chirurgicznej zmiany startu aplikacji.

Minimalna naprawa: bez nowych frameworkow. Wystarczy wydzielic nudne helpery/fasady startowe: `RendererStartupComposer`, `RuntimeLayerBinder`, `ApplicationCommandBootstrap`. Celem nie jest abstrakcja dla abstrakcji, tylko zmniejszenie blast radius `ApplicationBootstrap`.

### P1. Procesy Python nie maja kontraktu anulowania, timeoutu ani zabijania procesu

`JobSystem` ma anulowanie i `JobContext::ThrowIfCancellationRequested()` przed startem joba (`src/Core/JobSystem/JobSystem.cpp:465`), ale `PythonScriptJob::Execute` po uruchomieniu `ScriptRunner::RunFile` blokuje do konca procesu (`src/ScientificRuntime/Python/PythonScriptJob.cpp:37`, `src/ScientificRuntime/Python/PythonScriptJob.cpp:51`). `ProcessRunner` na Windows czeka przez `WaitForSingleObject(..., INFINITE)` (`src/Core/Platform/ProcessRunner.cpp:193`), a API `ProcessRunOptions` nie ma timeoutu ani tokena anulowania (`src/Core/Platform/ProcessRunner.hpp:11`).

Skutek: dlugi lub zawieszony skrypt Python zajmie worker thread i nie da sie go zatrzymac przez system jobow. W prototypach fizycznych to szybko wyjdzie przy wiekszych strukturach, bledach w skryptach i zewnetrznych narzedziach.

Minimalna naprawa: dodac do `ProcessRunOptions` opcjonalny timeout/cancellation callback i zabijanie procesu potomnego. `PythonScriptJob` powinien przekazac anulowanie z `JobContext` do `ProcessRunner`.

### P2. `IO/StructureToRenderer` jest mostem, nie surowym IO

`StructureToRenderer` lezy w `src/IO`, ale includuje typy renderera (`src/IO/StructureToRenderer.hpp:8`, `src/IO/StructureToRenderer.hpp:9`) i zwraca `RendererStructureData` (`src/IO/StructureToRenderer.hpp:13`). Samo istnienie konwertera jest dobre, bo usuwa duplikacje `BuildCellEdges`/`BuildBonds` (`src/IO/StructureToRenderer.cpp:39`, `src/IO/StructureToRenderer.cpp:68`), ale jego nazwa i lokalizacja rozmywaja role `IO`.

Skutek: `IO` zaczyna byc miejscem na dowolne adaptery miedzymodulowe, nie tylko formaty/pliki/procesy. Przy wzroscie liczby widokow bedzie pokusa dodawania `XToRenderer`, `XToPlot`, `XToExport` w tym samym module.

Minimalna naprawa: przeniesc konwerter do modulu wlasciciela reprezentacji docelowej, np. `Renderer/StructureRendererDataBuilder`, albo do jawnego mostu aplikacyjnego. `IO` powinno zostac przy czytaniu POSCAR/YAML/OBJ/CSV i raw integration.

### P2. Kontrakty zdarzen sa lepsze, ale ownership jest niespojny

Na plus: `RendererPanelInput` publikuje eventy typu `OrbitDelta`, `PanDelta`, `ZoomDelta` (`src/Presentation/Panels/RendererPanelInput.cpp:62`, `src/Presentation/Panels/RendererPanelInput.cpp:106`, `src/Presentation/Panels/RendererPanelInput.cpp:116`), a `RendererLayer` subskrybuje je w jednym miejscu (`src/Renderer/RendererLayer.cpp:468`). Na minus: rownolegle zostaly direct calls i mutowalne referencje opisane wyzej.

Skutek: komunikacja jest hybrydowa. Developer nie wie, czy nowa akcja viewportu ma isc eventem, komenda, bezposrednim wywolaniem na `RendererLayer`, czy mutacja `RendererWindowState`.

Minimalna naprawa: spisac jedna regule dla viewportu: input/UI publikuje intencje, renderer zmienia stan, komendy sa jedyna droga dla akcji z keymapy/palety, a globalne undo obsluguje mutacje, ktore powinny byc odwracalne.

### P2. Serializer konfiguracji jest bardzo duzym punktem zmiany

`YamlConfigSerializer.cpp` ma okolo 1700 linii. To nie jest natychmiastowy blad architektoniczny, bo konfiguracja bywa centralna, ale przy dalszym rozroscie renderer/runtime/UI kazda zmiana schema bedzie dotykac jednego duzego pliku.

Minimalna naprawa: nie rozbijac na sile teraz. Przy kolejnym wiekszym rozszerzeniu schematu wydzielic sekcyjne funkcje/pliki serializerow: `RendererConfigYaml`, `UiConfigYaml`, `JobsConfigYaml`, z jednym facade nadal wystawionym do `ConfigManager`.

## Co jest dobre i warto zachowac

- `Core/JobSystem` nie zna juz `ScientificRuntime`; GIL jest w hooku rejestrowanym przez `ScientificRuntimeLayer` (`src/ScientificRuntime/ScientificRuntimeLayer.cpp:76`).
- `ScriptRunner` uzywa `Platform::RunProcess`, nie `std::system` (`src/ScientificRuntime/Python/ScriptRunner.cpp:69`).
- `KeymapResolver` i `CommandRegistry` sa juz realnie w sciezce skrotow renderera (`src/Core/CoreLayer.cpp:341`, `src/App/ApplicationBootstrap.cpp:216`).
- `RendererSettings.hpp` nie includuje juz ImGui i uzywa typow neutralnych (`src/Renderer/RendererSettings.hpp:1`).
- `AssetManager` ma sensowna walidacje sciezek logicznych, w tym blokade absolutnych sciezek i `..` (`src/Core/Assets/AssetManager.cpp:112`, `src/Core/Assets/AssetManager.cpp:151`).
- `PymatgenConversion` produkuje domenowe `CrystalStructure`, co jest dobra granica miedzy Pythonem i domena (`src/ScientificRuntime/Python/ScientificPythonRuntime.cpp:77`).

## Priorytet napraw

1. Wprowadzic minimalny `ProjectWorkspace`/`StructureRegistry` i przestac trzymac prawde naukowa w rendererze.
2. Usunac lokalny undo renderera albo dokonczyc `SetCameraViewCommand` i wpiac go w `UndoStack`.
3. Zamknac mutacje `RendererWindowState` za eventami/komendami albo waskim portem renderera.
4. Usunac zaleznosci nizszych warstw od `App/Application` i pelnego `ApplicationConfig`.
5. Dodac anulowanie/timeout do `ProcessRunner`.
6. Dopiero potem porzadkowac serializer i lokalizacje mostu `StructureToRenderer`.

## Konkluzja

Podopieczny usunal czesc najgorszych przeciekow z poprzedniego audytu, szczegolnie ImGui w rendererze, `std::system` i GIL w `Core`. To jest dobry kierunek. Nadal jednak najwazniejsza oś aplikacji naukowej - domenowy runtime model i persistence ownership - nie istnieje w kodzie wykonawczym. Bez tego kolejne prototypy fizyki beda naturalnie dopisywane do `RendererWindowState`, `ApplicationBootstrap` albo paneli UI. To trzeba zatrzymac teraz, zanim powstana pierwsze realne workflow analityczne.
