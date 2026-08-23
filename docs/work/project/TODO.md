# DefectStudio – TODO

> **Cel:** VESTA-clone, lepiej napisany. Nowy program pisany od zera.
> Architektura z `sjcmdev` (repo B), funkcjonalność i UI z `SirJamesClarkMaxwell` (repo A, `old-ds-functionality.md`).
> Fokus: działający produkt jak najszybciej, bez przedwczesnych abstrakcji — ale z solidnym fundamentem.

> Dokument zunifikowany z `TODO.md` + `old-ds-functionality.md`, zweryfikowany względem kodu
> repo `Defect-Studio-mgr`, branch `main`, commit `e85be0c` (2026-08-13, po merge T07 C1-C6/C11).
> Tam gdzie `TODO.md` rozjeżdżał się z kodem — pierwszeństwo ma kod, z adnotacją w tekście.
> **Uwaga:** statusy w tym pliku nie były weryfikowane przez uruchomienie testów/builda —
> tylko przez przegląd źródeł.
>
> **2026-08-13 — replanning:** T07 domain foundations (C1-C6, C11) scalone do `main`. Natywny
> POSCAR I/O (C7-C10) zostaje jako otwarty dług w T07, ale nie blokuje niczego innego — odłożony.
> Zaraz po T07 wstawione **T07.5 (Project System + Remote Storage)** — wzorem `T06.5`, nie osobny
> `T10` daleko w kolejce, bo to jest praca **teraz**, nie "kiedyś". Użytkownik montuje katalog
> projektu z serwera obliczeniowego przez OS (SMB / SFTP+SSHFS-Win), więc nie trzeba wymyślać
> własnego formatu/transportu projektu od zera. `T07` Import/Export i `T11` Domain Runtime
> przepisane pod tym kątem, żeby nie dublować własności `ProjectWorkspace`/importu z T07.5.

---

## Zasady pracy

- Jeden branch per task: `task/NN-short-name`
- Merge do main tylko po pełnym Debug + Release build
- Pliki `.cpp` max ~500 linii — dzielić na moduły
- Brak wyjątków w ścieżkach renderowania — granica udokumentowana
- AI-generated code podlega aktywnemu review przed mergem
- Repo ma tryb **Ponytail**: najmniejsza poprawna zmiana po zrozumieniu realnego flow;
  reuse istniejących systemów `Core/*` zamiast nowych mechanizmów (zob. `AGENTS.md`)

---

## Granice architektoniczne (obowiązujące, nie do naruszenia bez decyzji)

- `Domain` nie zależy od UI, renderera ani `App`.
- `Renderer` może czytać typy domenowe przy budowie snapshotu, ale **nie jest** źródłem prawdy domenowej.
- `IO` czyta/zapisuje pliki; nie zawiera logiki transformującej domenę do konkretnego widoku
  (most `CrystalStructure -> RendererStructureData` żyje w `Renderer/StructureRendererDataBuilder`, nie w `IO`).
- `Presentation` renderuje UI i zbiera intencje użytkownika; nie mutuje po cichu stanu innych modułów.
- `App` składa systemy (composition root); nie ma być orkiestratorem logiki domenowej.
- Komunikacja: cross-layer przez `EventBus`; akcje użytkownika przez `CommandRegistry`+`CommandService`+keymap;
  długie operacje przez `JobSystem`+`ProgressTracker`; błędy jako `StructuredError`.
- Tylko main thread commituje stan widoczny w projekcie/UI.
- Nie mieszać pojęć: struktura domenowa / scena renderera / kolekcja UI / projekt na dysku.

---

## Aktualny status i priorytety

```
✅ DONE      : T01 – T06.5  (platform, docs, cross-platform, core, python bridge, renderer MVP,
                stabilizacja własności domeny/renderera)
🟡 PARTIAL   : T07 (domain foundations: UUID/AtomSite/Bond/BondGenerator/pymatgen SD scalone do
                main; natywny POSCAR I/O — C7-C10 — odłożone, nieblokujące)
🔨 CURRENT   : T07.5 (Project System + Remote Storage/mount + autoryzacja) — wstawione zaraz po
                T07, wzorem T06.5, bo to jest praca teraz, nie kolejny numer w kolejce
⏭ NEXT      : T08 → T09 → T11 → T12 → T13 → T14 → T15
```

**Zasada do T15:** działające minimum. Refactor/polish jako osobne taski po tym jak coś działa end-to-end.

---

## 🔥 Hotfixy — teraz, poza kolejką T-zadań

> Zgłoszone przez użytkownika jako "do zrobienia jak najszybciej", zreconciliowane względem
> istniejącego stanu kodu (2026-08-19). Drobne, izolowane poprawki — nie temat żadnego T-zadania.

- [x] **Startup performance (częściowo).** Zmierzone empirycznie: `CreateFromSpecification.total`
      ≈ 18.4s, z czego **17.8s to jeden krok — `LayerStack.build.RendererStartupConfig`** (ładowanie
      3 quick-test struktur z `install/app/data/renderer/startup_layout.yaml` przez
      `ScientificRuntimeLayer::LoadCrystalStructure`), **nie** `Py_InitializeEx` (embedded Python
      jest wyłączony na sztywno: `DS_PYTHON_CAPI_AVAILABLE=0` w `premake5.lua:457,623`, w obu
      configach — subprocess fallback, `python.exe` + cold `import pymatgen` **3×**, raz na okno).
      **Zrobione:** `Application::presentLoadingFrame()` (`Application.cpp`/`.hpp`,
      `ApplicationBootstrap.cpp`) — jedna klatka clear+swap+poll zaraz po utworzeniu okna, przed
      `setupDefaultLayers()`, żeby OS nie oznaczał okna "Not Responding" podczas blokującej fazy.
      Nie skraca realnego czasu, tylko usuwa wrażenie zawieszenia.
      **⏭ Powrót do przyspieszenia startu aplikacji (nie zrobione, prawdziwy fix):** realne źródło
      17.8s to (a) `DS_PYTHON_CAPI_AVAILABLE=0` wygląda jak zostawiony wyłącznik, nie świadoma
      decyzja — sprawdzić czy embedded Python (`_DS_PYTHON_EMBED_AVAILABLE` w premake5.lua już
      wykrywa dostępność dev headers/libs) da się bezpiecznie włączyć; (b) 3 quick-test okna z
      `startup_layout.yaml` ładowane sekwencyjnie, każde płacące pełny subprocess+cold-import —
      rozważyć jedno okno zamiast trzech, albo async load przez JobSystem (odrzucona pełna opcja
      z tego hotfixu — wymaga podniesienia `coreLayer->InitializeSystems(...)` przed
      `setupDefaultLayers()`, większy reorder bootstrapu, patrz commit history tej sekcji).
- [x] **Wiązania nie tworzą się po imporcie przez `puntukas`** (ProjectTreePanel "Open Defect" /
      `OpenDefectJob`). Root cause zweryfikowany: `puntukas.atoms.base.AtomsBase.get_positions()`/
      `get_cell()` deklarują `units="angstrom"` i wołają `U.from_angstrom(...)`, ale
      `puntukas.vasp.poscar.poscar.py:90-92` (`atoms_from_data`) przechowuje pozycje/cell już
      przekonwertowane do jednostek atomowych — `from_angstrom` na danych które nie są w
      angstromach to no-op. Efekt: odległości ×1.8897 za duże → `BondGenerator` nie łapie cutoffu
      → zero wiązań. **Zrobione:** workaround lokalny w
      `scripts/python/examples/puntukas_structure_load.py` (nie upstream w `punktukas-tools`) —
      zweryfikowane numerycznie (diament.vasp, lattice 14.19 Å) i w aplikacji przez użytkownika.
- [x] **ORTHO jako domyślny tryb kamery** (nie było w oryginalnej liście, dopisane w trakcie).
      `RendererConfig::defaultProjection` i `RendererViewCamera::m_Projection` mówiły
      "perspective" mimo że persisted `ui_settings.yaml` już miał `orthographic` — wyrównane.
- [x] PNG export z viewportu — rozszerzone w trakcie sesji do pełnego dialogu (przesunięte z
      "quick" MVP na realny kawałek T15, na żądanie użytkownika po teście pierwszej wersji).
      **Zrobione:**
      - `OpenGlRendererBackend::CaptureWindowToPng` — odczyt ostatnio wyrenderowanej klatki z FBO
        danego okna (`glReadPixels`, flip wierszy), `stbi_write_png` (już zvendorowany).
      - Komenda `renderer.export.image` (`RendererEvents::Viewport::ExportImageRequested`),
        bindowana pod **F12** (`keybindings.yaml`) — otwiera ten sam panel co przycisk toolbara.
      - **`ExportImagePanel`** (`src/Presentation/Panels/ExportImagePanel.*`, zarejestrowany w
        `EditorLayer` obok `RendererPanel`, `visibleByDefault=false`, widoczność śledzi
        `RenderExportDialogState::open`) — **prawdziwy dokowalny/skalowalny panel, nie modal**
        (druga iteracja; pierwsza była `BeginPopupModal`, zob. bug #2 niżej dlaczego to się nie
        sprawdziło z F12). Nazwa pliku proponowana z `structure.sourcePath` (stem), presety
        rozdzielczości (1080p/2K/4K/Custom), checkboxy atoms/bonds/cell/grid niezależne od
        głównego viewportu, live preview wypełniający dostępną przestrzeń panelu (letterboxed do
        aspect ratio wybranej rozdzielczości — **nigdy nie rozciąga**, skaluje się z rozmiarem
        panelu), przeciąganie preview = `camera.Pan(...)` + slider zoom (`SetDistance`) +
        "Reset View" — **plus prawdziwy per-krawędziowy crop** (4 slidery Left/Right/Top/Bottom,
        0-45% każda, `RenderExportDialogState::crop{Left,Right,Top,Bottom}`): ucina piksele na
        `glReadPixels` sub-rect w `CaptureWindowToPng` (celowo zmienia aspect ratio wyjścia — to
        jest crop, nie reframing), preview pokazuje przyciemniony overlay na obcinanych
        marginesach zamiast przerenderowywać w innym rozmiarze.
        Ścieżka zapisu: `Platform::PickSaveFile` (nowy, `Core/Platform/FileDialog.*`, NFD save
        dialog — analogiczny do istniejącego `PickFolder` z `ProjectTreePanel`), domyślnie
        `exports/`, zapamiętywana w `dialog.saveDirectory` między otwarciami.
      - **Znalezione i naprawione bugi (3):**
        1. Pierwsza wersja szukała FBO po `windowState.windowId` (UUID), ale
           `RendererPanel.cpp:122` renderuje pod kluczem `windowState.title` — poprawione.
        2. **Crash na F12** (nie na przycisku) — `RendererLayer::onExportImageRequested` wołał
           `ImGui::OpenPopup()` z poziomu event handlera inputu, poza kontekstem jakiegokolwiek
           ImGui okna (`ImGui::OpenPopup`/`BeginPopupModal` oba liczą ID przez
           `g.CurrentWindow->GetID(...)`, wymagają żywego okna z niepustym ID stackiem w miejscu
           wywołania — event handler tego nie ma). Namierzone przez wbudowany w apkę crash-handler
           (pełny symbolizowany stos w `logs/DefectStudio.log`, `[CRASH]` entry). Ostateczny fix:
           **modal zamieniony na zwykły dokowalny panel** (`ExportImagePanel`, wyżej) — zwykłe
           `ImGui::Begin()` nie ma tego wymogu, więc problem znika architektonicznie, nie tylko
           dla F12 ale dla każdego przyszłego triggera.
        3. **Powtarzające się "crashe" po fixie #2** okazały się czymś innym: `IM_ASSERT` w Dear
           ImGui to domyślnie zwykły `assert()`, który pod dołączonym debuggerem VS wykonuje
           `__debugbreak()` zamiast dialogu Abort/Retry/Ignore — więc każdy wewnętrzny sanity-check
           ImGui (nawet niegroźny) wygląda jak crash. Fix: `IM_ASSERT` przekierowany na
           log-and-continue (`DefectStudio_LogImGuiAssertFailure`, tylko Debug) — wpięty przez
           `IMGUI_USER_CONFIG` w `premake5.lua`, **nie** przez edycję `Vendor/imgui/imconfig.h`
           (to submodule; `git submodule update --force` z `GenerateProjects.bat` skasowałoby taką
           edycję przy następnym generowaniu projektów — złapane w trakcie tej sesji). Nowy plik:
           `src/Presentation/ImGuiUserConfig.hpp` + `ImGuiAssertHandler.cpp`.
      - **Do ustalenia później (dopisane na koniec planu, nie teraz):** tagowanie eksportów/struktur
        (`exc_ms`, `exc_tryp`, `gs_sing`, `gs_try` itd., niekoniecznie widoczne dla użytkownika) —
        wymaga rozmowy z resztą zespołu, zob. `rzeczy-do-dodania-jak-quirky-shell.md` (koniec pliku).
- [ ] Drag&drop z `ProjectTreePanel` (POSCAR/CONTCAR) na viewport + auto-naming okien
      (`RendererLayer::AddWindow` dziś robi tylko `push_back`, zero deduplikacji nazw).

---

## T01 – Platform Setup (`task/01-platform-setup`) ✅ DONE

- [x] Premake5 workspace dla Visual Studio 2022 (C++23, Debug/Release/Dist)
- [x] Struktura katalogów: `Core/`, `App/`, `Vendor/`, `Assets/`, `Docs/`, `Scripts/`
- [x] Vendor: GLFW, ImGui (docking), GLAD, spdlog, Tracy, yaml-cpp, nlohmann/json, GoogleTest, ImGuiNotify, imgui-command-palette, BS\_thread\_pool
- [x] `scripts/Tooling.bat` / `Tooling.ps1` + `setup.py` jako kanoniczny setup
- [x] `uv` jako dependency manager dla Python venv
- [x] PCH (`pch.hpp`) per-moduł, konwencja `.hpp`
- [x] Logging (spdlog) z poziomami i metadanymi source
- [x] `scripts/ci_check.py` – lokalne CI (Debug + Release)
- [x] mdBook w `docs/` + `SUMMARY.md`
- [x] Windows VERSIONINFO + ikona aplikacji (`.rc`) dla Debug/Release/Dist
- [x] Flagi buildów Debug / Release / Dist
- [x] `RunWithTracy.bat` + `run_with_tracy.py`
- [x] **Vendor: `glm`**, **`stb_image`/`stb_image_write`**, **`nativefiledialog-extended`**, **`entt`**
- [ ] **Vendor: `ImPlot`** – zweryfikowane w kodzie: brak w `Vendor/`, wciąż nieużyty nigdzie w `src/`

**Biblioteki:** Premake5, spdlog, yaml-cpp, Tracy

---

## T02 – Dokumentacja i testy (`task/02-docs-tests`) ✅ DONE

- [x] mdBook: pełna struktura `SUMMARY.md`
- [x] Strony architekturalne: Core, DataModel, IO, Renderer, Layers, UI
- [x] Workflow deweloperski: build, run, debug, konwencje branchy
- [x] `scripts/ci_check.py` finalizacja
- [ ] Testy regresyjne migracji konfiguracji (legacy → YAML)

---

## T03 – Cross-Platform (`task/03-crossplatform`) ✅ DONE

- [x] Linux build: Premake5 gmake2, g++ / clang
- [x] Weryfikacja Python tooling
- [x] Parytet `Tooling.bat` vs `Tooling.sh`
- [x] Windows local build matrix: msvc x Debug/Release/Dist

---

## T04 – Core: Application, LayerStack, EventSystem (`task/04-core`) ✅ DONE

- [x] `Application` lifecycle, deterministyczna pętla, `Window` wrapper
- [x] `Ref<>` / `WeakRef<>` / `Unique<>`, `EventQueue` (`std::variant`), `LayerStack`
- [x] `CoreLayer`, `ImGuiLayer`, `EditorLayer`
- [x] `EventSystem` (keyboard/mouse/window), `EventBus` (pub/sub)
- [x] `ConfigManager`, persist stylu UI (Hazel-like preset)
- [x] `JobSystem` (BS\_thread\_pool, priorytety, cancellation), `ProgressTracker`
- [x] `DebugLayer` (Debug + opcjonalny internal Release)
- [x] Cooperative cancellation tokens, `ASSERT_MAIN_THREAD` guard
- [x] Log panel + job/task monitor w UI
- [x] `Core/Commands` (`CommandRegistry`/`CommandService`), `Core/Input` (`KeymapResolver`), `Core/Undo`,
      `Core/Diagnostics` (`StructuredError`), `Core/Capabilities`, `Core/Assets` (`AssetManager` z walidacją
      ścieżek, blokadą absolute paths i `..`), `Core/Notifications` — **zweryfikowane w kodzie, obecne i
      realnie używane** (nie tylko szkielet)

**Biblioteki:** yaml-cpp, spdlog, BS\_thread\_pool

---

## T05 – Python Bridge (`task/05-python-bridge`) ✅ DONE

> Uwaga: repo używa **`nanobind`**, nie `pybind11` — decyzja architektoniczna tego repo,
> odmienna od starszego projektu w innych notatkach; nie mieszać.

- [x] `PythonInterpreter` RAII wrapper
- [x] `ScriptRunner` (dev tool) — **zweryfikowane:** uruchamia przez `Core/Platform/ProcessRunner`,
      nie `std::system` (poprawione względem starszego audytu)
- [x] `PymatgenBridge` – `Structure.from_file()`, superkomórki, symetria
- [x] `ASEBridge` – Atoms I/O, konwersja formatów
- [x] GIL management — **zweryfikowane:** hook rejestrowany przez `ScientificRuntimeLayer`,
      `Core/JobSystem` nie zna już `ScientificRuntime` (dobra granica, potwierdzona w code review 2026-07-01)
- [x] Python venv management (uv)
- [x] Error propagation: Python exceptions → `StructuredError` → UI
- [x] Testy interoperacyjności: pymatgen POSCAR roundtrip

**Ryzyko z code review 2026-07-01 zamknięte:** `ProcessRunner` ma `timeout` i `shouldCancel`,
a `PythonScriptJob` przekazuje anulowanie z `JobContext`. Zawieszony subprocess Pythona może być
terminowany przez kontrakt procesu.

**Biblioteki:** nanobind, pymatgen, ASE, numpy

---

## T06 – OpenGL Renderer (`task/06-renderer`) ✅ DONE

- [x] GL context, GLAD loader, debug callback
- [x] `Renderer` (forward, jeden pass), `ShaderLibrary` (load/compile/reload)
- [x] FBO dla offscreen render / viewport texture
- [x] Instanced atom rendering, instanced bond rendering (walec 8-stronny)
- [x] Orbit camera (LMB orbit, MMB pan, scroll zoom) + persist
- [x] OpenGL 4.3 Compute Shader infrastruktura (SSBO, dispatch, sync barrier)
- [x] **Bond transform compute shader (`bond_transform.comp`)** — **zweryfikowane: `glDispatchCompute`
      jest realnie wywoływany** w `OpenGlRendererBackend::renderBonds` (nie jest to martwy kod)
- [x] Tracy GPU profiling
- [x] Grid / unit cell box rendering, view gizmo overlay (XYZ, klik ustawia kamerę)
- [x] Exception-free zone (udokumentowana granica)
- [x] Renderer startup assets (atom styles YAML, startup layout YAML, primitive OBJ) + `AssetManager`
- [x] Auto-bond generation po promieniach kowalencyjnych — **zweryfikowane: działa już w
      `StructureRendererDataBuilder`**, ale tylko jako dane renderera (brak modelu `Bond` w domenie,
      brak persystencji/override per-parę — zob. T07/T08)
- [x] Click-select atomu + Shift+klik toggle — **zweryfikowane: działa już** przez
      `RendererEvents::Viewport::AtomSelectionRequested` → `RendererWindowState::selectedAtomIndices`
      (ad-hoc, poza ECS — zob. uwaga architektoniczna w T08)
- [ ] Debug build + `DefectStudioTests` green; Release build green — **pominięte w tej weryfikacji
      na wyraźną prośbę; nie zakładać stanu bez ponownego uruchomienia**

**Biblioteki:** GLFW, OpenGL 4.3, GLM, stb\_image, Tracy, entt (vendorowany, **ale nieużywany —
brak `entt::registry`/`entt::entity` w całym `src/`; ECS zaplanowany dopiero w T08**)

---

## T06.5 – Stabilizacja architektoniczna ✅ DONE

> Nowa sekcja na podstawie `docs/work/architecture-code-review-2026-07-01.md`. Nie w oryginalnym
> `TODO.md`, ale krytyczna: bez tego każda kolejna funkcja naukowa dopisze się do
> `RendererWindowState` / `ApplicationBootstrap` zamiast do domeny.
>
> Status po refactorze `f753418`/`d78c39b`: blocker zdjęty. Zostają follow-upy przy T08/T12,
> ale nie blokują startu T07.

- [x] **(P0)** `DomainLayer`/`StorageLayer` są obecnie puste; `ApplicationBootstrap` ładuje
      strukturę przez `ScientificRuntimeLayer::LoadCrystalStructure` i od razu konwertuje do
      `RendererStructureData` — renderer staje się faktycznym właścicielem struktury.
      → wpiąć realny przepływ: import zwraca `CrystalStructure` do `DomainLayer::Workspace()
      .Structures().Add()`, renderer dostaje tylko pochodny snapshot z `domainStructureId`.
      **Zrobione:** `RendererStartupComposer` rejestruje struktury w `ProjectWorkspace`, a
      `RendererStructureData` niesie `domainStructureId`.
- [x] **(P0)** Dwa równoległe modele undo: globalny `Core/Undo`/`UndoStack` i lokalny
      `RendererWindowState::viewUndoHistory`/`viewRedoHistory`. `SetCameraViewCommand::Execute/Undo`
      są puste. → dokończyć `SetCameraViewCommand` i wpiąć w `UndoStack`, albo świadomie usunąć
      jako martwy abstrakt i zostawić tylko lokalne view-undo (decyzja architektoniczna, nie ukryty TODO).
      **Decyzja:** zostaje lokalne view-undo per viewport/per struktura; globalne undo sceny wraca w T08.
      `SetCameraViewCommand` pozostaje cleanupem, jeśli nie będzie używany.
- [x] **(P0)** `RendererPanel` ma mutowalny dostęp do `RendererLayer::GetWindows()` i bezpośrednio
      edytuje `RendererWindowState` (viewport size, kamera, `selectedAtomIndices`, picking).
      → `GetWindows()` ma zwracać read-only view-model; mutacje przez event/komendę lub wąski
      kontroler viewportu.
      **Zrobione dla ryzykownych mutacji:** viewport size idzie przez `RendererLayer::SetViewportSize`,
      a selekcja atomu przez `RendererEvents::Viewport::AtomSelectionRequested`. **Follow-up T08:**
      wymienić mutable `GetWindows()` na read-only view model przy wprowadzaniu Scene/ECS.
- [x] **(P1)** Niższe warstwy (`RendererLayer`, `ImGuiLayer`) zależą od `App/ApplicationState.hpp`
      / `Application::Get()`. → `RendererLayer` ma dostawać `RendererConfig`/dedykowany event,
      nie cały `ApplicationConfig`; `ImGuiLayer` ma publikować `CoreEvents::ShutdownRequested`
      zamiast wołać `Application::Get().Shutdown()` bezpośrednio.
      **Zrobione:** `RendererConfig` jest w rendererze, apply idzie przez `RendererEvents::Config::Applied`,
      a `ImGuiLayer` publikuje `CoreEvents::ShutdownRequested`.
- [x] **(P1)** `ApplicationBootstrap.cpp` >1000 linii, `setupDefaultLayers` robi zbyt dużo
      (layer setup + asset loading + Python startup scene + renderer wiring + command registration).
      → wydzielić nudne fasady startowe (`RendererStartupComposer` już istnieje częściowo,
      rozszerzyć wzorzec: `RuntimeLayerBinder`, `ApplicationCommandBootstrap`).
      **Zrobione częściowo i wystarczająco na blocker:** `RendererStartupComposer` i
      `RendererCommandRegistration` są poza `ApplicationBootstrap`. Ewentualny `RuntimeLayerBinder`
      zostaje jako późniejszy cleanup, jeśli bootstrap znów urośnie.
- [x] **(P2)** `IO/StructureToRenderer` włącza typy renderera i zwraca `RendererStructureData` —
      to most, nie surowe IO. → przenieść do `Renderer/StructureRendererDataBuilder` albo
      jawnego mostu aplikacyjnego.
      **Zrobione:** przeniesione do `Renderer/StructureRendererDataBuilder`.
- [x] **(P2)** `YamlConfigSerializer.cpp` ~1700 linii — nie rozbijać na siłę teraz, ale przy
      kolejnym rozroście schematu wydzielić `RendererConfigYaml`/`UiConfigYaml`/`JobsConfigYaml`
      za jednym facade w `ConfigManager`.
      **Zrobione pierwszy krok:** emisja sekcji renderera jest w `YamlRendererConfigSection`.

---

## T07 – Data Model i POSCAR Import (`task/07-data-model`) 🟡 PARTIAL — domena scalona do main

> Port funkcjonalności z repo A (`SirJamesClarkMaxwell`, `old-ds-functionality.md` §1). Czyste klasy,
> zero zależności UI. T06.5 zdjęło blocker domena-renderer.
>
> **2026-08-13:** commity C1-C6 i C11 z `TODO-T07.md` scalone do `main` (`e85be0c`). Pozostaje
> C7-C10 (natywny POSCAR I/O) — niżej, jako otwarty dług, **nie blokuje T07.5/T08**. Import struktur
> nadal idzie przez `PymatgenBridge`, co wystarcza na potrzeby T07.5/T08.

### Model domenowy
- [x] dodanie skrótów PgDn/PgUp/Home/End na obroty o 90° wokół wybranej osi + klawisz do zapisanego
      wcześniej ustawienia kamery (konfigurowalny w Settings) — **UWAGA:** cykliczne zapisane widoki
      (save/cycle_next/cycle_previous) już zaimplementowane (`RendererCommandRegistration.cpp`,
      `renderer.view.{save_current,cycle_next,cycle_previous}`), ale nie sam obrót o 90°
      
- [x] **UUID** — `StructureId`/`DefectId`/... to teraz `Uuid` (`Domain/DomainIds.hpp` →
      `Core/Utils/Uuid.hpp`, wrapper na `stduuid`), generowany przez `GenerateUuid()`. Zgodnie z
      notatką z code review: UUID dostarcza `Core/Utilities`, nie Domain bezpośrednio.
- [x] **AtomSite rozszerzony** — `label`, `charge`, `magnetization`, `occupancy`,
      `selectiveDynamics`/`hasSelectiveDynamics` dodane (`CrystalPrimitives.hpp`), wypełniane z
      pymatgen (`PymatgenConversion.cpp`)
- [x] Derived Cartesian position utilities (`CrystalStructure::CartesianToFractional/FractionalToCartesian`)
- [x] **`Bond`** – domenowy typ (`firstAtomIndex`/`secondAtomIndex`/`lengthAngstrom`/`origin`/`visible`)
      + `BondGenerationSettings` (`CrystalPrimitives.hpp`), generowany przez
      `Domain/Crystal/BondGenerator.cpp` (`RegenerateAutoBonds`), wpięty w startup
      (`RendererStartupComposer.cpp`). Renderer czyta `structure.bonds`, nie generuje ich już sam.
      **Dług architektoniczny (świadomy, opisany w TODO-T07.md C3):** `Bond` referencjonuje atomy
      przez indeks, nie stabilny `AtomId` — do rozwiązania w T08 przy pierwszej mutacji `atoms`.
- [x] **NOWE (znalezisko w kodzie):** `VacancySite` już istnieje jako struct w
      `Domain/Crystal/CrystalPrimitives.hpp` i pole `CrystalStructure::vacancies` — ale **nigdzie
      w `src/` nie jest wypełniane ani czytane**. To gotowy fundament pod T11/§16.1 (defekty
      punktowe), obecnie martwe pole. (`Domain/Defects/DefectModel.*` już istnieje dla T11 —
      `DefectConcept`/`DefectConfiguration`/`CalculationRecord` + rejestry w `ProjectWorkspace` —
      ale jeszcze niepodłączone do `vacancies`.)
- [ ] **PARTIAL:** `ElementCatalog` – `ElementPropertiesTable` istnieje dla masy/Z/promieni;
      kolor jest osobno w `Renderer` `AtomStyleTable`/`AtomStyleIO` — separacja zgodna z zasadą
      "appearance vs physical properties", zachować

### Import / Export (z `old-ds-functionality.md` §1)

> **2026-08-13:** `punktukas-tools` (prywatne narzędzie, optional global dependency — zob.
> "Infrastruktura" niżej) już parsuje POSCAR/POTCAR/KPOINTS/WAVECAR/CHGCAR po stronie Python.
> Obniża to priorytet natywnego C++ parsera niżej (offline/no-Python fast path, nie krytyczna
> ścieżka) — checklisty C7-C10 zostają, ale nie są blockerem dla T07.5/T08/T13/T14.
>
> **Skąd przychodzi plik do importu (integracja z T07.5):** picker (`nativefiledialog-extended`)
> nie rozróżnia lokalnej ścieżki od zamontowanej sieciowej (`Z:\...`) — dla `ParsePoscarFile`/
> `PymatgenBridge` to ten sam `Path`. Jedyna realna konsekwencja dla C7/C8: odczyt/zapis na
> zamontowanym dysku może zwrócić transient I/O error (VPN padł w trakcie), więc `Result<T>`
> zwracany z `ParsePoscarFile`/`WritePoscarFile` musi nieść to jako zwykły `StructuredError`, nie
> zakładać że "plik jest, bo picker go pokazał" — patrz T07.5.2 (odporność na zerwane połączenie).
> Poza tym zero zmian w parserze z powodu mountu — to jedyny punkt styku.

- [ ] proste drzewko projektu, trochę jak w blenderze gdzie jest scene outline (zmiana potem w następnym etapie prac)
- [ ] POSCAR/CONTCAR parser natywny C++ (VASP5/6, Selective Dynamics, Direct/Cartesian,
      skala ujemna → przeliczenie na docelową objętość, auto-przeskalowanie kartezjańskich
      przez współczynnik sieciowy) — obecnie import idzie wyłącznie przez `PymatgenBridge`
      (Python); natywny C++ parser w `IOLayer` nadal nie istnieje (zgodnie z zasadą z pamięci:
      podstawowy parsing strukturalny → C++ `IOLayer`, wzbogacone dane → `ScientificRuntime`/ASE)
- [ ] POSCAR serializer – precyzja i format bez noisy diff; `canonicalizeDirectTranslation = false`
      domyślnie (znany błąd ze starego repo — pilnować przy porcie)
- [ ] Sortowanie species w POSCAR malejąco po liczbie atomów. Użytkownik może zdefiniować swoją kolejność (zapisywać jako własność projektu)
- [ ] `StructureSerializer` – serializacja do YAML na potrzeby projektu
- [ ] Multi-import: kolejny POSCAR otwiera **nowe renderer window** (nie dokłada do istniejącej
      sceny) — **decyzja 2026-08-22: Collections odrzucone (zob. T08), oddzielne okna już dają
      izolację, po którą sięgały Collections.** Auto-konwersja układu współrzędnych (Direct ↔
      Cartesian) przy imporcie zostaje.
- [ ] Multi-structure support (wiele POSCAR w jednym projekcie, każdy we własnym oknie) —
      częściowo pokryte przez `StructureRegistry` (istnieje i jest wpięte w startup rendererowy,
      patrz T06.5), workflow projektu (lista otwartych struktur, przełączanie) w `ProjectTreePanel`
- [ ] Parser unit tests: POSCAR roundtrip, edge cases (Selective Dynamics, puste struktury)
<!-- - [ ] CIF parser – import, konwersja do modelu wewnętrznego (pymatgen backend przez T05) —
      **zweryfikowane: brak w kodzie**
- [ ] XYZ / extended XYZ parser (ASE-compatible) — **zweryfikowane: brak w kodzie** -->

**Biblioteki:** yaml-cpp

---

## T07.5 – Project System + Remote Storage (`task/07.5-project-mount`) 🔨 CURRENT

> **Replanning 2026-08-13.** Wstawione zaraz po T07, wzorem `T06.5` — to jest praca teraz, nie
> pozycja daleko w kolejce jako `T10`. Zamiast wymyślać własny format transportu/synchronizacji
> danych projektu (i konkurować z gitem do dużych binarek DFT), zakładamy że **katalog projektu
> może leżeć na zamontowanym dysku sieciowym** (SMB albo SFTP przez SSHFS-Win) i traktujemy go jak
> zwykły katalog lokalny. To nie zmienia architektury z oryginalnego `TODO.md` (projekt = katalog
> + `manifest.yaml`) — zmienia tylko to, **gdzie** ten katalog może fizycznie leżeć i **jak**
> użytkownik się do niego dostaje. `StorageLayer` nadal jest pustym szkieletem — cała sekcja
> realny `[ ]` od zera. Import struktur z tego katalogu zostaje własnością T07 (patrz "Import /
> Export" wyżej) — T07.5 dostarcza tylko *skąd* ten katalog pochodzi.
>
> **Dlaczego mount, nie własny transport:** mount (SMB/SFTP) jest transparentny dla
> `std::filesystem`/`ifstream`/`nativefiledialog-extended` — zero specjalnego kodu w `IOLayer` do
> zwykłego czytania/pisania plików. Duże binarki (WAVECAR, CHGCAR mogą mieć GB) nigdy nie muszą
> trafiać lokalnie — czytane on-demand przez sieć. Git zostaje dla plików tekstowych
> (POSCAR/INCAR/notatki), gdzie chcemy historię/diff; mount przejmuje dane obliczeniowe.

### T07.5.1 — Project directory model (bez zmian względem oryginalnego planu T10)
- [x] **Zrobione, 2026-08-20.** Projekt jako katalog z `manifest.yaml` — `ProjectManifestIO`
      (`src/IO/ProjectManifestIO.hpp/.cpp`): `{uuid, name, created_at, last_modified,
      format_version, roots[], bulk_directory}`. `roots` reużywa `ProjectRootEntry` z
      `ProjectRootsIO` (ten sam kształt `{id, path, label}`) — **label to dziś wolny tekst, nie
      referencja do `ServerProfile`** (ten typ nie istnieje, T07.5.2 to czysty plan).
- [x] Create / Open / Recent project workflow — menu Plik: "Nowy" (odmawia nadpisania istniejącego
      `manifest.yaml`), "Otworz", "Ostatnie projekty" (`RecentProjectsIO`,
      `install/users/default/config/recent_projects.yaml`, cap 10, move-to-front). "Zapisz" to
      ręczny re-save (idempotentny — każda strukturalna zmiana i tak zapisuje natychmiast).
      Ostatnio otwarty projekt auto-otwiera się przy starcie (najnowszy wpis z recent list).
- [x] `ProjectMetadata`: uuid/name/created_at/last_modified/format_version — jak wyżej. `uuid`
      przez istniejący `Core/Utils/Uuid.hpp` (`GenerateUuid()`/`ToString()`), timestampy jako
      epoch-seconds (`Time::Now()`) — **brak formatowania na czytelny string nigdzie w kodzie**,
      świadomie pominięte (nic dziś tego nie wyświetla).
- [ ] Auto-save z konfigurowalnym interwałem — **nie potrzebne**: każda strukturalna zmiana
      (add/remove/change-folder root, bulk reference) zapisuje natychmiast, nie ma "dirty state"
      do zbierania na interwał
- [ ] Tracking dodanych plików (lazy resource loading)
- [ ] `PathResolver` – normalizacja ścieżek project-relative vs external — **brak w kodzie**
- [ ] Walidacja brakujących plików przy otwieraniu + relink / rebuild flow — **na mounted drive
      to też pokrywa przypadek "mount padł"** (brak sieci/VPN), nie tylko przeniesiony plik
- [ ] Zapis ukrytych atomów, widoków, dodanych bondów, overrides kolorów, stanu kolekcji,
      pozycji kamery (`old-ds-functionality.md` §10)
- [x] **Rejestracja wielu folderów/dysków jako jeden projekt — zrobione, 2026-08-20** (patrz
      T07.5.4 niżej, teraz część manifestu zamiast osobnego pliku gdy projekt jest aktywny).
- [ ] Świadomie poza scope tego przejścia (nie proszone, trzyma robotę w ryzach): tags dla
      defektów/kolekcji, migration pipeline formatu pliku, canonical vs recovery/snapshot save
      split, project rename UI, zamykanie/przełączanie aktywnego projektu w trakcie sesji.
- [x] **Ponytail placeholder, zostaje celowo (nie do zastąpienia przez manifest — inny scope):**
      `EditorLayer.cpp` — `project_windows.txt`, per-okno camera/selection/electronic-structure
      state, zapis na `OnDetach`, odtwarzane przez replay `OpenStructureRequested`. To stan
      renderera per-window, nie stan projektu — świadomie osobny plik.
- [x] **Ad-hoc fallback (brak aktywnego projektu) — zostaje, `ProjectRootsIO`/`project_roots.yaml`**
      — nie każdy `+ Add Root` musi od razu być "prawdziwym projektem"; session-durable roots bez
      Create/Open dalej działają dokładnie jak wcześniej, po prostu nie przeżywają jako *nazwany*
      projekt dopóki user nie zrobi Nowy/Otworz.
- [ ] Export kolekcji do POSCAR
- [ ] Tags dla defektów i kolekcji
- [ ] Application startup project (ostatni otwarty)
- [ ] Lekki migration pipeline dla ewolucji formatu pliku projektu
- [ ] Canonical save vs recovery/snapshot save split

### T07.5.2 — Remote Storage: mount jako pierwsza ścieżka
> Ponytail rung 1: sam mount **nie wymaga naszego kodu** — Windows (Map Network Drive / SMB) i
> SSHFS-Win+WinFsp (SFTP → literka dysku) robią to na poziomie OS. Nasza appka po prostu widzi
> zamontowany katalog jak lokalny. To co budujemy, to wygoda dookoła tego, nie sam transport.

- [ ] **Server Profiles** — zapamiętane definicje połączenia: host, port, protokół (`smb`/`sftp`),
      zdalna ścieżka root, nazwa użytkownika, metoda autoryzacji (klucz/hasło). Persystencja przez
      istniejący `ConfigManager`/yaml-cpp (wzorzec jak `ui_settings.yaml`/`keybindings.yaml`),
      per-user, **nie** per-projekt — użytkownik ma jeden zestaw serwerów używany w wielu projektach.
- [ ] **Connect flow (MVP, bez auto-mount):** dialog "Connect to Server" pokazuje instrukcję +
      przycisk otwierający natywny "Map Network Drive"/`sshfs-win` (przez `Core/Platform/ProcessRunner`
      — już istnieje, użyty dziś do subprocess Pythona, reużyć, nie pisać nowego wrappera). Appka
      **nie** próbuje sama zarządzać stosem SMB/SFTP — zbyt platform-specific na v1.
- [ ] Po połączeniu: `Path` zwrócona przez picker (`nativefiledialog-extended`, już zvendorowany)
      wskazuje na zamontowaną literę dysku — otwiera się jak każdy inny katalog, zero zmian w
      `IOLayer`/`AssetManager` (AssetManager blokuje absolute paths/`..` **tylko** dla własnych
      assetów aplikacji, nie dla user-wybranych plików projektu — nie mylić tych dwóch ścieżek).
- [ ] Odporność na zerwane połączenie: operacje I/O na plikach projektu mają zwracać
      `StructuredError`/`Notification` zamiast crashować, gdy mount zniknie w trakcie sesji
      (VPN padł, serwer restart) — rozszerzenie istniejącego `Result`/`StructuredError`, nie nowy
      mechanizm (patrz też T07 Import/Export, ten sam mechanizm dla `ParsePoscarFile`/
      `WritePoscarFile`). UI: "Reconnect" affordance zamiast martwego panelu.
- [ ] **Stretch (nie w MVP):** in-app SFTP browser bez realnego OS-mounta (osobna pozycja w
      Backlogu, `libssh2`/podobne jako nowy Vendor) — zostaje tam, dopóki mount-first nie okaże się
      niewystarczający (np. serwer bez WinFsp-friendly dostępu).
- [ ] **Per-serwer dobór plików submit/`run_computations` — zażądane 2026-08-23.** Różne serwery
      obliczeniowe mają różne konwencje submit-scriptów (`run_computations`, scheduler-specific
      pliki, itd.) — appka ma dobierać/generować właściwy wariant na podstawie tego, do którego
      **Server Profile** jest aktualnie podłączony dany root/folder, nie jeden hardcoded szablon.
      Zależne od Server Profiles wyżej istniejąc jako realny typ (dziś projekt referencuje roots
      wolnym tekstem, zob. T07.5.4 "`source`/`ServerProfile` referencja"); prawdopodobnie ten sam
      mechanizm co "auto POTCAR/INCAR/KPOINTS/run_computations per folder" z nowego defekt-creation
      flow (T08.6.3 wyżej) — jeden template-per-server system, nie dwa osobne.

### T07.5.3 — Autoryzacja i poświadczenia (bezpieczeństwo — nie upraszczać)
> **Twarda zasada: appka nigdy nie przechowuje sekretów (hasło, passphrase, treść klucza
> prywatnego) we własnych plikach YAML.** Tylko referencje (ścieżka do klucza, nazwa użytkownika)
> idą do `ConfigManager`. To jest granica bezpieczeństwa, nie do "uproszczenia" nawet w Ponytail.

- [ ] **Klucze SSH (SFTP):** appka **nie generuje, nie przechowuje, nie czyta** bajtów klucza
      prywatnego. Referencja to tylko ścieżka (`~/.ssh/id_ed25519` domyślnie, wybieralna). Jeśli
      klucz ma passphrase — oddajemy to `ssh-agent` (OpenSSH już jest częścią Windows/Linux), appka
      shelluje się przez `ProcessRunner` i pozwala OS/agentowi obsłużyć prompt. Zero własnej
      kryptografii.
- [ ] **"Pokaż/skopiuj klucz publiczny"** — wygodny przycisk czytający `<key>.pub` (to jest
      publiczne, nie sekret) do schowka, żeby dodać do `authorized_keys` na serwerze.
- [ ] **Hasła (SMB):** **auto-wypełnianie = Windows Credential Manager, nie nasz kod.** Windows już
      to robi za darmo — checkbox "Remember my credentials" w natywnym dialogu Map Network Drive
      zapisuje hasło w Credential Manager i podstawia je przy kolejnym połączeniu. Jeśli appka ma
      sama inicjować connect (zamiast odsyłać do natywnego dialogu), użyć `CredWriteW`/`CredReadW`
      (`wincred.h`, część Win32, zero nowej zależności) do zapisu/odczytu hasła kluczowanego nazwą
      profilu serwera. **Nigdy** pola `password`/`passphrase` w naszym YAML.
- [ ] Linux/macOS credential store (`libsecret`/Keychain) — **odłożone**, appka rozwijana na
      Windows jako platforma docelowa (zob. `TODO-T07.md` build rule); dodać gdy realnie potrzebne.
- [ ] Settings panel: lista Server Profiles z edycją (host/port/protokół/użytkownik/ścieżka klucza),
      bez pola na sekret — sekret zawsze przez natywny dialog/Credential Manager, nigdy przez nasz
      formularz w postaci zwykłego tekstowego pola.

---

## T08 – Scene, ECS, Atom Rendering, Selection (`task/08-scene`)

> ECS (entt) jako fundament sceny od początku. **Zweryfikowane: entt jest zvendorowany, ale
> `entt::registry`/`entt::entity` nie występuje NIGDZIE w `src/` — to zadanie zaczyna się od zera**,
> mimo że część funkcjonalności (selekcja, auto-bond) już działa poza ECS na poziomie renderera
> (zob. T06). Trzeba zdecydować: migrować istniejącą ad-hoc selekcję do ECS, czy budować równolegle
> i podmienić — to jest decyzja architektoniczna, nie tylko implementacja.

- [ ] `SceneRegistry` – entt registry jako właściciel wszystkich obiektów sceny
- [ ] Komponenty ECS: `TransformComponent`, `AtomComponent`, `BondComponent`, `VisibilityComponent`,
      `SelectionComponent`, `CollectionComponent`
- [ ] `SceneSystem` – update loop iterujący po komponentach
- [ ] Załaduj POSCAR → utwórz encje w registry → wyświetl atomy (instanced render z T06)
- [ ] **PARTIAL → zweryfikowane:** click-select atomu (raycasting-podobne, przez
      `AtomSelectionRequested`) i Shift+klik toggle **już działają**, ale na poziomie
      `RendererWindowState::selectedAtomIndices`, nie przez `SelectionComponent` w ECS — do migracji
- [ ] Box-select (B), circle-select (C), RMB context menu
- [ ] Multi-select (Shift+click) — toggle już częściowo istnieje (zob. wyżej), box/circle brak
- [ ] Undo/Redo stack dla akcji sceny (Ctrl+Z / Ctrl+Y) — snapshot-based na start. **Uwaga:**
      nie mylić z lokalnym view-undo renderera (`viewUndoHistory`) opisanym w T06.5 — to ma iść
      przez globalny `Core/Undo`/`UndoStack`
- [ ] Dodaj atom przez wpisanie współrzędnych (frakcyjne/kartezjańskie); popup: pierwiastek + tryb
      koordynat; wstawienie pod 3D cursorem lub w centrum selekcji (`old-ds-functionality.md` §3.3)
      — **nie zrobione jeszcze, potrzebuje popupu UI**, zob. notatka niżej.
- [x] **Usuń atom (`Delete`) i duplikuj (`Ctrl+D`, zob. poprawka 2026-08-22 niżej) — zrobione 2026-08-21.** Nowy
      `src/Renderer/Commands/RendererAtomEditCommands.{hpp,cpp}` — pierwsza realna hydraulika
      mutująca żywą `CrystalStructure` otwartego okna (wcześniej nic tego nie robiło, sprawdzone w
      kodzie: `ApplyVacancy`/`ApplyInterstitial`/`ApplyReplacement` w `DefectModel.cpp` miały zero
      callerów). Łańcuch: `RendererLayer::GetFocusedViewportWindowId()` (albo jawny windowId) →
      `windowState.structure.domainStructureId` → `DomainLayer::Workspace().Structures()
      .FindMutable(id)` (**nowa metoda**, rejestr był tylko `Add`+const `Find`, żadnej mutacji) →
      `ApplyVacancy`/`ApplyInterstitial` (**wypromowane z anonimowej przestrzeni nazw w
      `DefectModel.cpp` do publicznego API** — dokładnie ta sama logika co defekty punktowe, bez
      duplikacji) → `BuildRendererStructureData` (rebuild) → `SceneSystem::SyncSceneWithStructure`
      + `ApplySelectionAndVisibilityToScene` (duplicate zaznacza nowe kopie, jak w Blenderze).
      Prawdziwy `ICommand` z `Undo()` (nie event-fire-and-forget jak hide/show/invert) —
      przechodzi przez globalny `Ctrl+Z`/`Ctrl+Y` (`Core/Undo`/`UndoStack`), zgodnie z
      `docs/adr/0001-state-mutation-policy.md`. Undo/Redo symetryczne: `Undo` przywraca snapshot
      `atoms`+`bonds` sprzed operacji **i** oryginalne zaznaczenie, więc domyślny `Redo` (=ponowne
      `Execute`) trafia w te same atomy. `ApplyVacancy` **świadomie** dopisuje `VacancySite` do
      `structure.vacancies` przy zwykłym Delete — usuwanie atomu w tej appce to koncepcyjnie
      wakancja w modelu domenowym (§16.1/T11), nie osobna ścieżka; pierwsze realne wypełnienie tego
      dotąd martwego pola.
      **Nie zrobione w tym przejściu:** Change type (brak dedykowanego klawisza — flagowane już w
      planie sesji, wystawić jako pole/popup, nie globalny skrót), kopiuj/wklej (`Ctrl+C/V`),
      Add atom popup (wyżej) — obie potrzebują nowego UI (element combo + pola), osobny krok.
- [ ] Ukryj (`H`) / odkryj wszystkie (`Alt+H`) — toggle `VisibilityComponent` per okno/struktura
      (Collections odrzucone 2026-08-22, zob. T08 niżej — nie ma per-kolekcja podziału)
- [ ] `SceneOutliner` panel – lista struktur z entt view (jedna struktura na okno), toggle
      widoczności, F2 rename
- [ ] `ObjectProperties` panel – właściwości wybranego atomu/struktury (translate przez pola numeryczne,
      uniform XYZ snap)
- [ ] Auto-bond generation z modelem trwałym: global cutoff + **per-para pierwiastków z override**
      (dziś jest tylko global cutoff na poziomie renderera — per-para override z
      `old-ds-functionality.md` §4.2 brak), spatial hash grid (jedna struktura per okno, więc bez
      dodatkowego "między kolekcjami" wykluczenia — Collections odrzucone 2026-08-22, zob. T08)
- [ ] Manual bond add/remove z persystencją w projekcie — **decyzja 2026-08-22: skrót `J`** (zaznacz
      2 atomy → `J` łączy je wiązaniem; pierwotny wybór `F` z 2026-08-21 odrzucony — zajęty przez
      "flip pinu etykiety o 180°", zob. rozwiązaną kolizję niżej); ukryj pojedyncze wiązanie bez
      usuwania z modelu, auto-ukrycie przy usunięciu atomu; etykiety długości wiązań w 3D (§4.4,
      zob. też "Pomiary" niżej — to ten sam etykietowy mechanizm)
- [x] ImGuizmo transform (G/R/S) dla zaznaczonych atomów — zrobione 2026-08-21, branch
      `task/08-scene/imguizmo-transform`. Vendorowany **`sjcmdev/ImGuizmo`** (nie ImViewGuizmo —
      to inny fork, kamera-nawigacja/compass, nie ma translate/rotate/scale w ogóle; ustalone przy
      starcie tego etapu, `ImViewGuizmo` submoduł dodany i od razu wycofany). Fork bez własnego
      premake5.lua → dorobiony lokalny `DefineImGuizmoProject()` (StaticLib, wzorzec `nfd`) zamiast
      `include "Vendor/ImGuizmo"` jak przy ImPlot. `G`/`R`/`S` (`renderer.gizmo.mode_*`) przełączają
      operację; pivot = żywy centroid zaznaczenia liczony co klatkę (bez cache — trwały pod
      rotate/scale, bo sztywna transformacja wokół własnego centroidu go nie rusza). Drag na żywo
      mutuje `RendererStructureData` (hot path rendera) dla natychmiastowego feedbacku; puszczenie
      przycisku commituje wynik do domenowej `CrystalStructure` jako jedna komenda undo
      (`TransformSelectedAtomsCommand`, przez nową `renderer.gizmo.commit_transform`, hidden z
      palety — potrzebuje payloadu z draga, nie ma sensu jako gołe wywołanie). **Nie zrobione:**
      axis/plane constraints (`G`+`X/Y/Z`, `Shift+X/Y/Z`) — ImGuizmo sam w sobie nie ma takiego
      trybu, wymagałoby osobnej modalnej warstwy wejścia, odłożone.
      **Wygląd poprawiony 2026-08-22:** `ImGuizmo::Manipulate()` zawsze rysował półprzezroczysty
      kwadrat plane-handle dla TRANSLATE/SCALE mimo że apka nie ma w ogóle draga po płaszczyźnie
      (zgłoszone jako "nie wygląda jak w Blenderze") — dla tych dwóch operacji `Manipulate()` już się
      nie wywołuje, zamiast tego własny rysunek (`AddLine` + `AddTriangleFilled` grot + kropka w
      pivocie) w `RendererPanel::renderTransformGizmo`. ROTATE nadal przez natywny `Manipulate()`
      (ring bez tego problemu).
- [ ] **Scene Objects – Empty**: punkt pomocniczy z lokalnym układem osi, transformowalny (G/R),
      "align active empty Z to selected atoms", align to world/camera (§8.1)
- [ ] **Scene Objects – Origin i Light**: jednoinstancyjne obiekty specjalne, transformowalny Light (§8.2)
- [x] **3D Cursor — zrobione 2026-08-22 (renderer-only, nie ECS/domain, nie persystowany).**
      `RendererWindowState::cursor3DPosition`/`cursor3DPlaced`, `SelectionToolMode::Cursor3D`
      (vertical toolbar "3D point" tool - klik w viewport = `Cursor3DSetPositionRequested`, snap do
      atomu pod kursorem albo płaszczyzna przez `camera->Target()`), context menu "3D Cursor" →
      Set Here / Move to Selection Center / First / Last Selected / Origin. Nadal brakuje: pivot do
      transformacji (gizmo pivot dalej liczy się jako centroid zaznaczenia, nie cursor3D) i punkt
      wstawienia dla "dodaj atom przez współrzędne" (linia 531 wyżej) - obie rzeczy do zrobienia gdy
      ten popup faktycznie powstanie.
- [x] **Ctrl+A/C/V/D — zrobione 2026-08-22.** Select All / Copy / Paste / Duplicate. `Ctrl+D` =
      duplicate (jak w Blenderze); stary `renderer.view.set_as_project_default` przeniesiony na
      `Shift+D` (odwraca poprawkę 2026-08-21 opisaną wyżej - użytkownik wolał zgodność z Blenderem
      od uniknięcia przewiązania). Copy/Paste = in-process clipboard (`std::vector<AtomSite>`,
      anonimowa przestrzeń nazw w `RendererAtomEditCommands.cpp`), nie system schowka OS, nie
      persystowany.
- [ ] Blender-like Shift+A add menu (menu kontekstowe też) - context menu Copy/Paste/Duplicate/
      Delete/Hide/Change type/Select All/Clear Selection/3D Cursor zrobione 2026-08-22
      (`RendererPanel::renderViewportContextMenu`); Shift+A "add" menu i Empty/Groups submenu
      ze starego projektu nadal nie zrobione (Empty/Groups nie mają odpowiednika w domain modelu
      tego projektu - wymaga decyzji, nie samego UI).
<!-- - [ ] N side-panel (toggle + strip) -->
- [ ] **Pomiary — rozszerzone 2026-08-21, teraz oparte o etykiety-encje (zob. MSDF w T09):**
      odległość między ostatnimi 2 zaznaczonymi atomami, kąt między ostatnimi 3, centrum masy
      zaznaczenia (§5, oryginalny zakres) — **plus automatyczne, zawsze-aktualne etykiety długości
      wiązań i kątów jako MSDF-owe encje w scenie** (nie tylko ad-hoc pomiar na zaznaczeniu), pełna
      macierz skrótów `M` — zob. osobny punkt niżej. Długość wiązania: geometria cylindra już liczy to na GPU co klatkę
      (`bond_transform.comp`, sprawdzone w kodzie — auto-recalc przy ruchu atomu przez gizmo jest
      w praktyce **już rozwiązany**, wystarczy że CPU re-uploaduje start/finish z żywych pozycji po
      drag'u, zero nowego compute shadera potrzebne); kąt — zwykła trygonometria CPU, tanie nawet
      dla wielu wiązań naraz, nie potrzebuje GPU.
- [x] **Macierz skrótów `M` (etykiety wiązań/kątów) — zrobione 2026-08-22.** 8 kombinacji:
      Ctrl = zasięg (zaznaczenie / wszystkie widoczne atomy), Alt = akcja (dodaj / usuń), Shift = typ
      (wiązanie / kąt) — `M`/`Shift+M` istniały wcześniej (dodaj dla zaznaczonych), reszta
      (`Alt+M`, `Ctrl+M`, `Ctrl+Alt+M`, `Alt+Shift+M`, `Ctrl+Shift+M`, `Ctrl+Alt+Shift+M`) nowa.
      Dodawanie zawsze add-only (`ToggleMeasurementPin(..., removeIfPresent=false)`) więc powiększanie
      zaznaczenia nigdy nie kasuje już przypiętych etykiet; usuwanie filtruje po zbiorze indeksów
      atomów (`RemovePinsWithinSet`), nie po istnieniu wiązania w `structure.bonds`, więc sprząta też
      piny osierocone edycją struktury. Pojedynczy pin nadal usuwa się przez klik + `Delete`
      (`RendererPanel::handlePinnedMeasurementInteraction`) — bulk-owe skróty nie zastępują tego.
      Domyślnie etykiety długości wiązań czytają się wzdłuż wiązania
      (`PinnedMeasurement::alignToBondDirection = true`); skrót **`A`** przełącza globalnie
      wszystkie już przypięte i przyszłe piny wiązań na upright i z powrotem
      (`RendererWindowState::bondLabelsAlignToDirection`,
      `RendererLayer::onLabelsToggleBondAlignmentRequested`) — etykiety kątów zawsze upright, flaga
      ich nie dotyczy. `Ctrl+A` (select all) zaznacza teraz tylko widoczne atomy, nie całą komórkę.
      Stary `Alt+M` (`renderer.labels.toggle` — surowe auto-etykiety na każdym wiązaniu, osobny
      mechanizm od pinów) stracił skrót na rzecz nowego "usuń zaznaczone" — komenda żyje dalej,
      dostępna z command palette po nazwie.
- [x] **Narzędzie Measure w pionowym pasku toolbara — naprawione 2026-08-23 (zgłoszone jako "nie
      działa").** Dwa realne, niezależne bugi:
      1. Przyciski `##ToolMeasureBond`/`##ToolMeasureAngle` (`RendererPanelToolbar.cpp`) były
         jednorazowymi akcjami (`active` hardcoded `false`, fire-and-forget event) zamiast trybem
         narzędzia jak `Sel`/`3D cursor` — nie dało się ich w ogóle "zaznaczyć"/podświetlić. Naprawa:
         `SelectionToolMode` dostał `MeasureBond`/`MeasureAngle` (`RendererTypes.hpp`), przyciski
         teraz przez `publishToolToggle` jak reszta paska. Kliknięcie w viewport podczas aktywnego
         narzędzia (`RendererPanel::handleMeasureToolClick`, nowa metoda obok
         `handleCursor3DPlacement`) dokłada atom do zaznaczenia (reużywa `handleAtomPick` +
         addytywny `AtomSelectionRequested`) aż do 2 (bond) / 3 (angle), wtedy strzela ten sam event
         co klawisz `M`/`Shift+M` i czyści zaznaczenie — narzędzie zostaje aktywne, gotowe na kolejną
         parę bez ponownego klikania w toolbar. Wejście w tryb (`onSelectionToolToggleRequested`)
         czyści zaznaczenie z poprzedniego narzędzia, żeby pierwszy klik nie dociągnął przypadkowo
         starych atomów do pary.
      2. **Realny bug w `AddBondPinsWithinSet` (nie tylko UX):** funkcja szukała pary WYŁĄCZNIE wśród
         istniejących `structure.bonds` — dla 2 zaznaczonych atomów bez chemicznego wiązania między
         nimi (żadna para poza auto-bond cutoff) nic się nie działo, nawet przez stary klawisz `M`.
         `AddAnglePinsWithinSet` miał już fallback na surowy kąt 3-punktowy gdy nic nie jest
         zbondowane (`atomSet.size() == 3` bez trafienia) — bond-owa wersja fallbacku nie miała w
         ogóle. Dodany analogiczny fallback: `atomSet.size() == 2` i zero trafień w pętli po
         `structure.bonds` → pin surowej odległości między dowolnymi dwoma atomami. To był
         prawdopodobny powód "wybieram 2 atomy, label się nie pojawia" niezależnie od bugu #1.
      3. **Trzeci bug, ten sam dzień, po zgłoszeniu że nadal "działa tylko między atomami z
         wiązaniem":** fallback z #2 poprawnie tworzył pin w `pinnedMeasurements`, ale
         `OpenGlRendererBackend::renderLabels` (2-atomowa gałąź) w ogóle nie miał na to szans —
         szukał długości/midpointu WYŁĄCZNIE przez dopasowanie do `structure.bonds` (ten sam
         wzorzec co bug #2, tylko w renderze zamiast w tworzeniu pinu); bez trafienia `break` nigdy
         nie następował i `AppendBondLabelInstances` nigdy się nie wołało — pin istniał, nic się nie
         rysowało. Naprawa: rotation/length/midpoint liczenie wydzielone do lokalnej lambdy
         `appendLengthLabel(posA, posB)` używanej zarówno przy trafieniu w `structure.bonds` (jak
         wcześniej), jak i w nowej gałęzi `if (!matchedBond)` — surowe pozycje atomów, bez
         rozwiązywania obrazu periodycznego (fallback-owy pin i tak zawsze ma
         `bondPeriodicOffset = 0`).
      4. **Na życzenie 2026-08-23:** `M`/`Shift+M` z pustym zaznaczeniem (0 atomów) teraz aktywują
         narzędzie Measure Bond/Angle zamiast cicho nic nie robić (`onLabelsToggleSelectedBondRequested`/
         `AngleRequested` wołają teraz `onSelectionToolToggleRequested` bezpośrednio, gdy
         `selectedAtomIndices.empty()`) — spójne z kliknięciem w toolbar. Z 1 zaznaczonym atomem (za
         mało na parę/trójkę) zachowanie bez zmian: cichy no-op, jak dotąd.
         Zweryfikowane: `MSBuild /t:DefectStudio`+`/t:DefectStudioTests` (Debug) czyste, 212/212
         testów przechodzi.
- [x] **Ciągły ruch trzymanym klawiszem — zrobione 2026-08-22.** Dotychczasowy mechanizm (GLFW
      `GLFW_REPEAT`, `repeatable: true` w `keybindings.yaml`) dawał ~10-15 Hz i "rwany" ruch, a dla
      `Ctrl+Shift+Arrow` w ogóle przestawał się powtarzać po Alt-Tabie (potwierdzone diagnostycznymi
      logami, usuniętymi po naprawie). Zastąpione per-klatkowym pollingiem `ImGui::IsKeyDown` w
      `RendererPanel::applyContinuousNudge`/`applyContinuousPan` (skalowane przez `deltaTime`, jedna
      komenda undo na całe przytrzymanie zamiast jednej na każdy tick powtórzenia) - wywoływane
      bezwarunkowo co klatkę, nie tylko gdy mysz hoveruje viewport (wcześniejsza regresja: zależność
      od hover wyłączała ciągły ruch przy zjechaniu myszą poza viewport mid-hold).
      `Ctrl+Shift+Arrow` = ciągłe przesuwanie zaznaczonych atomów (było, teraz płynne);
      `Alt+Shift+Arrow` = nowe, ciągłe panning kamery (`Alt+Arrow` samo w sobie dalej orbituje po
      staremu, `Shift+Arrow` samo to pojedynczy krok pan — nierozszerzony, nie był "rwany").
- [ ] **Tryby zaznaczania (nowe, 2026-08-21):** atoms / atoms+bonds / bonds-labels (tylko etykiety,
      do przesuwania ich gizmem bez ruszania atomów) / atoms+bonds+labels — skróty **`Ctrl+1..4`**
      (zwykłe `1/2/3`/`Alt+1/2/3` zajęte przez align-axis a/b/c/a*/b*/c*, decyzja 2026-08-21: axis
      align zostaje na 1/2/3). Wymaga żeby bond-y i etykiety były realnymi ECS entity z
      `SelectionComponent` — **etykiety już to mają (zob. T09 MSDF, zrobione 2026-08-22:
      `LabelComponent`+`SelectionComponent`), bond-y nadal nie** (`BondComponent` istnieje, ale
      `SyncSceneWithStructure` nie dokłada mu `SelectionComponent`/`VisibilityComponent` jak robi to
      dla atomów) — nazwy trybów robocze, do dopracowania.
- [ ] **Bond scaling (uwaga na przyszłość, 2026-08-21):** jak wiązania będą niezależnie skalowalne
      gizmem (dziś nie są — geometria cylindra to funkcja pozycji dwóch atomów, nie ma własnego
      transformu), pivot skalowania musi być środkiem WŁASNYM wiązania (midpoint atomów, przeliczany
      na żywo), nie world origin ani pozycja jednego z atomów — inaczej skalowanie przesunie
      wiązanie zamiast tylko zmienić jego grubość/długość.
- [ ] **Displacement arrows (nowe, 2026-08-21):** strzałki pokazujące jak atomy przemieściły się
      względem zadanej geometrii referencyjnej — konkretnie POSCAR→CONTCAR (relaksacja) i
      ground→excited state. **Puntukas ma to już rozwiązane liczbowo:**
      `puntukas.atoms.base.AtomsBase.get_distances(p1, p2, pbc=True)` (minimum image convention,
      poprawnie liczy przemieszczenie przez granicę periodyczną) — reużyć przez subprocess bridge
      (wzorzec `PuntukasBridge`), **nie** przez PyVista/VTK renderer z puntukas (`visual/pyvista/
      objects.py` ma tylko glyph-rendering, niereużywalne w naszym OpenGL). Rysowanie natywne:
      strzałka = stożek+walec, reużyć instancing z bond-renderingu (ten sam wzorzec co cylindry
      wiązań, inny mesh na czubku). **Do zweryfikowania przed implementacją:** czy `BondGenerator`
      (periodic bond generation, commit `ea85335`) już ma natywną, poprawną minimum-image
      odległość w C++ — jeśli tak, może nie trzeba w ogóle Pythona dla tego konkretnego przypadku
      (POSCAR/CONTCAR to ta sama komórka, sam displacement, nie pełne `get_distances` API).
- [ ] Drag & drop pliku POSCAR/CONTCAR/CHG z eksploratora na viewport
- [ ] CWD fix: ustaw CWD na katalog exe przy starcie (`GetModuleFileNameA`)
- [ ] Touchpad support

### Replanning 2026-08-22 (rozszerzony zakres, na żądanie użytkownika)

> Skonsolidowane po zamknięciu MSDF labels (T09). Nie duplikuje bulletów wyżej (Add atom popup,
> Shift+A menu, `SceneOutliner`/`ObjectProperties`, auto-bond per-para override, `Ctrl+1..4`,
> displacement arrows — wszystkie już tam, tylko odsyłacz) — dokłada to czego tam nie było.

- [ ] **Element Catalog editor panel (nowe) — z `old-ds-functionality.md` §11.1, brak jakiegokolwiek
      trackingu w TODO dotąd.** UI edytujące globalną tabelę kolor/promień per pierwiastek (dane już
      istnieją: `AtomStyleTable`/`AtomStyleIO`, `ElementPropertiesTable` z T07 — brakuje tylko panelu
      do ich edycji, nie modelu danych). Periodic Table picker już istnieje
      (`RendererPanel::drawPeriodicTableWindow`) jako osobna rzecz — Element Catalog to tabelaryczny
      widok wszystkich pierwiastków naraz (kolor, promień, ewentualnie inne flagi wyglądu), nie picker
      pojedynczego.
- [ ] **Customizacja pojedynczego atomu (nowe, szersze niż stary projekt).** Stary projekt miał
      tylko override **per pierwiastek** (Element Catalog + per-projekt override, T12 już to
      trackuje). To co teraz proszone: override **per konkretny atom** (np. "ten jeden węgiel ma być
      czerwony/większy, reszta węgli bez zmian") — inny, drobniejszy poziom niż per-species. Wymaga
      pola na `AtomSite`/`RendererAtomData` (dziś koloru/promienia atom dostaje wyłącznie z tabeli
      per-species, zero per-instancji override) + UI w `ObjectProperties` (wyżej) do go ustawiać.
      Zdecydować przy starcie: renderer-only (jak dziś appearance) czy persystowane w projekcie —
      **jeśli persystowane, to prawdopodobnie domenowe pole**, nie renderer-side.
- [ ] **`Ctrl+C`/`Ctrl+V`/`Ctrl+D` dla `ProjectTreePanel` (nowe) — kopiuj/wklej/duplikuj pliki i
      foldery w drzewku projektu**, jak w eksploratorze plików. Inne niż istniejące `Ctrl+C/V/D` na
      atomach w viewporcie (`RendererAtomEditCommands.cpp`) — kolizja nazw skrótów tylko jeśli oba
      konteksty mogłyby być aktywne naraz; do zweryfikowania przy implementacji czy `KeymapResolver`
      context (`renderer.viewport.focused` vs focus na drzewku) już to naturalnie rozdziela, czy
      potrzeba osobnego kontekstu dla `ProjectTreePanel`.
  - [ ] **Rozszerzenie — zażądane 2026-08-23:** oprócz copy/paste/duplicate, też **New File**,
        **New Folder**, i **Move** (drag-drop w drzewku albo Cut+Paste) — pełny zestaw jak w
        eksploratorze VSCode. Move to jedyna z tych operacji bez dzisiejszego odpowiednika nigdzie
        w appce (copy/duplicate ma już analogię w atom-clipboard) — do zweryfikowania osobno.
- [ ] **Per-folder toggle "ukryj pewne pliki" w `ProjectTreePanel` — zażądane 2026-08-23.** Context
      menu na konkretnym folderze dostaje opcję włącz/wyłącz ukrywanie (np. plików tymczasowych/
      wynikowych, do doprecyzowania jakich dokładnie przy implementacji) — per-folder, nie globalny
      toggle. Analogiczne miejsce w kodzie co "Set as Bulk Reference" (T07.5.5) —
      `renderDirectoryContextMenu`, per-folder RMB, nie root-only.
- [ ] **Integrated PowerShell terminal panel (nowe)** — dockowalny panel z żywym PowerShell (Windows;
      docelowo też bash/sh na Linux per T03 cross-platform). Wzorzec do zbadania: `Core/Platform/
      ProcessRunner` już obsługuje subprocess z I/O (używany dla Pythona) — sprawdzić czy da się
      reużyć do interaktywnego PTY-like terminala, czy to inny mechanizm (Windows ConPTY). Osobny
      panel od zaplanowanego "Python scripting panel" (Backlog, niżej) — jeden to ogólny shell, drugi
      to REPL z dostępem do `ds` module/sceny; oba mogą żyć w tej samej grupie doków ("Integrated
      tools"), ale to dwa różne zadania implementacyjne.
- [x] **Kolizja klawisza `F` — rozwiązana 2026-08-22.** Plan z 2026-08-21 rezerwował `F` pod "zaznacz
      2 atomy → połącz wiązaniem", ale gołe `F` już zajęte przez "flip pinu etykiety o 180°"
      (`RendererPanel::handlePinnedMeasurementInteraction`, raw `ImGui::IsKeyPressed`, nie przez
      `KeymapResolver`). Zamiast analizować czy zaznaczenia (pin vs 2+ atomy) faktycznie nachodzą na
      siebie w praktyce — po prostu **manual bond add dostaje inny klawisz: `J`** (zob. wyżej). `F`
      zostaje wyłącznie przy flip pinu, zero dzielenia klawisza.
- [x] **Collections i Groups — odrzucone 2026-08-22 (decyzja użytkownika, nie odłożone).**
      `old-ds-functionality.md` §6/§7 miały Collections/Groups jako sposób trzymania wielu
      struktur/podzbiorów w jednej scenie z osobnym stanem widoczności. **Powód odrzucenia: appka
      izoluje struktury przez osobne renderer windows, nie przez sub-sceny** — każdy import dostaje
      własne okno (zob. multi-import w T07), więc problem który Collections miały rozwiązywać
      (widoczność/kolor/eksport per podzbiór atomów w jednej scenie) tu nie występuje. `Groups` był
      od początku "zbędny bez konkretnego use-case" — odpada razem z Collections. `CollectionComponent`
      w kodzie zostaje jako martwy placeholder (usunięcie to osobne, niepilne sprzątanie — nic go nie
      odczytuje, zero kosztu w utrzymywaniu). **Skutek dla zależnych zadań** (już zaktualizowane
      wyżej): multi-import → osobne okno zamiast nowej Kolekcji; auto-bond per-para → bez klauzuli
      "między Kolekcjami" (jedna struktura per okno i tak to daje za darmo); `SceneOutliner` → lista
      struktur, nie struktur+kolekcji; "Extract to New Collection" (był zakomentowany) → usunięty,
      bez sensu bez Kolekcji.
- [ ] **Drobne luki z przeglądu `old-ds-functionality.md` (2026-08-22):**
  - [ ] Axis overlay: tryb **relatywny** (osie liczone względem zaznaczonych atomów, nie tylko
        globalny) — §2.2, dziś gizmo osi w rogu viewportu jest tylko globalne.
      - [ ] Render Image: tryb override tła (białe tło) i nadpisania kolorów atomów przy eksporcie —
        §12, `ExportImagePanel` dziś ma tylko toggle widoczności warstw (atoms/bonds/cell/grid/
        labels), nie color override.
      - [ ] Podstawowa kontrola oświetlenia (pozycja/intensywność/kolor ambient-diffuse-specular) —
        §11.3. Prawdopodobnie prerekwizyt dla "PBR lighting" z T09, nie ten sam zakres: to jest zwykłe
        Phong/Blinn światło konfigurowalne z UI, PBR to osobny, większy krok jakości.
  - [ ] Stats panel (liczba atomów/wiązań/wydajność) i Viewport Info (diagnostyka) — z §13, oba
        zero trackingu, oba niski priorytet (informacyjne, nie blokują niczego).

### Priorytetyzacja i kolejność implementacji (2026-08-22)

> Konsoliduje "luźne końce T08" + Replanning wyżej + dwie rzeczy zostawione osierocone w T12/Backlog
> przy poprzedniej konsolidacji (multi-tab text editor + INCAR highlighting, Python/ipython console)
> — Collections/Groups wyłączone, odrzucone. Priorytet = wartość dla użytkownika / ile innych zadań
> odblokowuje. Trudność = szacunek na podstawie tego co już istnieje w kodzie.

1. **`SceneOutliner` panel** — Priorytet: wysoki. Trudność: łatwe–średnie. Bez Collections zakres
   się skurczył (lista struktur, nie struktur+kolekcji) — głównie UI nad już istniejącym `entt`
   view. Nic samo w sobie nie blokuje, ale wszystko poniżej wygodniej testować z listą obok.
2. **`ObjectProperties` panel** — Priorytet: wysoki. Trudność: średnie. Numeryczne pola transform +
   miejsce, gdzie wyląduje UI per-atom customization (#10) — budować przed nią, nie po.
3. **Manual bond add/remove (`J`)** — Priorytet: wysoki. Trudność: średnie. Jawnie brakująca, częsta
   interakcja (parytet ze starą appką); bez zależności od reszty listy, może iść równolegle z #1/#2.
4. **Python console (integrated ipython-style REPL)** — Priorytet: wysoki. Trudność: trudne. Już raz
   jawnie zażądane (2026-08-21, priorytet podniesiony z Backlogu) — wysoka wartość mimo dużego
   nakładu (RAII/GIL przez `ScientificRuntime`, hot-reload przez `efsw`, autocomplete jak w ipythonie
   to osobny, większy podkrok). Niezależne od #1-3, można ciągnąć równolegle innym torem.
5. **Element Catalog editor panel** — Priorytet: średni. Trudność: łatwe–średnie. Czysty UI nad już
   istniejącym `AtomStyleTable`/`ElementPropertiesTable` — zero nowego modelu danych, szybki zysk.
6. **Text editor: multi-tab + syntax highlighting dla INCAR/skryptów** — Priorytet: średni. Trudność:
   łatwe–średnie. Oba od dawna zanotowane jako "Nie zrobione (MVP scope)" przy już wysłanym panelu
   (T12, `EditorLayer`) — highlighting to włączenie istniejącego, nieużywanego `SetLanguage()` na
   forku `ImGuiColorTextEdit`; tabs to nowy stan (lista otwartych dokumentów + dirty per tab + prompt
   przy zamykaniu niezapisanego).
7. **Add atom przez współrzędne (popup)** — Priorytet: średni. Trudność: średnie. Samodzielne, jasno
   wyspecyfikowane (§3.3 starej appki), zero zależności blokujących.
8. **`BondComponent` + `SelectionComponent`/`VisibilityComponent`** — Priorytet: średni. Trudność:
   średnie. Techniczny fundament pod #9 i pod "ukryj pojedyncze wiązanie" z #3 — brak samodzielnej
   wartości UX, ale odblokowuje dwie rzeczy naraz.
9. **`Ctrl+1..4` tryby zaznaczania** — Priorytet: średni. Trudność: łatwe (po #8). Trywialne po #8,
   bez sensu przed nim.
10. **Auto-bond: per-para override + model trwały + spatial hash grid** — Priorytet: średni.
    Trudność: trudne. Scope już mniejszy bez klauzuli "między Kolekcjami"; nadal największy
    pojedynczy kawałek pracy w tej grupie (persystencja + spatial index).
11. **Per-atom customization (kolor/rozmiar per instancja)** — Priorytet: średni. Trudność:
    średnie–trudne. Wymaga decyzji renderer-only vs domenowe pole **przed** kodem, potem żyje w
    `ObjectProperties` (#2).
12. **`ProjectTreePanel` `Ctrl+C/V/D`** — Priorytet: niski–średni. Trudność: łatwe. Mechanicznie
    kopiuje już istniejący wzorzec z atom-clipboard (`RendererAtomEditCommands.cpp`) — tani, nie
    blokuje niczego innego.
13. **Scene Objects: Empty / Origin / Light** — Priorytet: niski–średni. Trudność: średnie. Głównie
    potrzebne jako pełny cel dla Shift+A (#14); bez nich menu "Add" i tak działa (samo dodawanie
    atomów).
14. **Shift+A add menu** — Priorytet: niski–średni. Trudność: łatwe (atom-only) / średnie (pełne, po
    #13). Można wypuścić okrojone (tylko Add Atom) zanim #13 gotowe.
15. **Displacement arrows (POSCAR→CONTCAR)** — Priorytet: niski–średni. Trudność: trudne. Wartościowe
    dla DFT-workflow, ale wymaga weryfikacji czy `BondGenerator` ma już poprawny minimum-image w
    C++, inaczej dokłada Python/puntukas bridge.
16. **Integrated PowerShell terminal** — Priorytet: niski. Trudność: trudne. Infrastruktura (ConPTY
    albo rozszerzenie `ProcessRunner`), nie blokuje żadnej innej pozycji z tej listy.
17. **Drobne luki** (axis overlay relative, render-export override, basic lighting, Stats/Viewport
    Info) — Priorytet: niski. Trudność: łatwe (każde z osobna). Wypełniacz między większymi
    zadaniami, nie samodzielny etap.

**Poza kolejnością:** bond scaling pivot (notatka na przyszłość, nie zadanie — zob. wyżej, aktualne
dopiero gdy wiązania dostaną własny transform).

**Rekomendowana kolejność sesji:** 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12 → 13 → 14 → 15
→ 16, z #17 dobieranym opportunistycznie (małe, łatwo wcisnąć między większe kawałki bez przerywania
toku). #4 (Python console) niezależne — może się przesunąć wcześniej/później bez psucia reszty
kolejności, jeśli inna osoba/sesja ciągnie je równolegle.

**Biblioteki:** entt

---

## T08.5 – Interaction & View Modifiers (`task/08.5-interaction`)

> Fast-track wstawiony między T08 a T09 na prośbę użytkownika. Selekcja box/circle, hide/show/invert,
> nazwane widoki + domyślny widok projektu — pełny plan commit-po-commicie (C1-C12) w
> `docs/work/project/plans/rzeczy-do-dodania-jak-quirky-shell.md` (zewnętrzny plan reconciliation).

- [x] Box-select (`B`) / circle-select (`C`, live scroll-resizable brush) — **skróty przeniesione
      z `Alt+B`/`Alt+C` na zwykłe `B`/`C` w Etapie A, 2026-08-21** (align-axis przeniesione na
      1/2/3, zwalniając litery — zob. `keybindings.yaml`).
      **Circle-select semantyka poprawiona 2026-08-21:** było odwrotnie niż w Blenderze (plain
      click = replace, Shift+drag = dodaj, Ctrl+drag = odejmij) — teraz trzymanie LPM maluje
      zaznaczenie ciągle (dodaje), Shift przełącza pędzel na odejmowanie
      (`RendererPanel::handleCircleSelectDrag`, `RendererPanel.cpp`), Ctrl-branch usunięty jako
      zbędny wobec Shift. Zweryfikowane: Release build czysty.
- [x] Hide selection (`H`) / show all (`Alt+H`) / invert selection (`I`) — pełny `ViewModifier` pipeline
- [x] `RendererViewSnapshot` rozszerzony o selekcję/widoczność (index + position-based, cross-structure
      resolve po najbliższej pozycji)
- [x] Nazwane widoki + domyślny widok projektu (set `Ctrl+D` / apply `Alt+D`, session-scoped do
      czasu T07.5.1)
- [x] Okna renderera: stabilna tożsamość (`title###windowId`) niezależna od wyświetlanej nazwy,
      rename z UI
- [ ] Select by element — **odłożone**, brak zastosowania na teraz (decyzja użytkownika)

**Biblioteki:** brak nowych (reuse `CommandRegistry`, `KeymapResolver`, ECS z T08)

---

## T08.6 – Electronic Structure Foundations (`task/08.6-electronic-structure`)

> Fast-track, promocja z Backlogu (VASP OUTCAR/WAVECAR integration) na wprost prośbę użytkownika —
> rysowanie funkcji falowej defektu jest priorytetem. Pełny research (puntukas API, zweryfikowany
> w źródle nie z tutoriala) w zewnętrznym planie `rzeczy-do-dodania-jak-quirky-shell.md`, sekcja
> T08.6. **Zależność z T14**: wymaga fast-tracku generycznego jądra isosurface (compute shader) —
> zob. T14 niżej.

### T08.6.1 — Parsing (puntukas)
- [x] `VaspOutputBridge` (subprocess przez `VaspOutput.from_directory`, ten sam wzorzec co
      `PuntukasBridge`) — band-gap/HOMO/LUMO + per-band orbital data (energy/occupation/
      localization/irrep, oba kanały spinowe), oba gracefully `null` przy braku vasprun.xml/WAVECAR
- [x] `VaspOutputJob` (`IJob`) — **zrobione, 2026-08-20**: `ElectronicStructureSession::
      DispatchOutputLoad`/`DispatchBulkLoad` odpalają go per-window i dla bulk reference,
      `pollOutputJob`/`pollBulkJob` odbierają wynik. Trigger UI (T08.6.3) już istnieje.
- [x] `VaspOrbitalGridBridge`/`VaspOrbitalGridJob` — **dodatkowo zrobione, nie było w oryginalnym
      planie**: czyta WAVECAR przez punktukas (`Wavecar.phi → real_space_wfs()`), jeden orbital
      (spin/k-point/band) na raz, siatka przez temp raw-float32 plik (za duża na JSON-line).
      Dispatch na klik banda w `ElectronicStructurePanel`, wynik konsumowany przez GPU isosurface
      (`RendererLayer::RegenerateOrbitalIsosurface` → compute shader, zob. T14). Dziś **jeden
      orbital na raz** — brak batch/prefetch sąsiednich bandów, zob. plan niżej.

### T08.6.2 — Model domenowy
- [x] `Domain/Electronic/ElectronicStructureModel` — `BandGapData`, `OrbitalChannelData`,
      `OrbitalRecord`, `ElectronicStructureData` (niezależne od `ScientificRuntime`, konwerter
      osobno jak `PymatgenConversion`)
- [x] Filtr progu `localization_factor` (`LocalizationThresholdSettings`)
- [x] Klasyfikacja singlet/triplet z occupation per kanał spinowy (`ClassifySpinMultiplicity`) —
      heurystyka nadal niewalidowana fizycznie przez użytkownika, zob. plan niżej
- [x] **Bulk reference — zrobione, ale ręczne/nie-projektowe.** `ElectronicStructureSession`
      (`m_BulkDirectory`/`SetBulkDirectory`/`DispatchBulkLoad`/`BulkGap()`) — jeden bulk katalog
      dzielony między wszystkimi otwartymi oknami defektów, band-window ustawiony (band-index
      problem z 2026-08-13 rozwiązany). **Dziś jedyny sposób ustawienia:** ręczny tekst/Browse +
      przycisk "Reload bulk gap" w `ElectronicStructurePanel` — trzeba mieć panel otwarty i wiedzieć
      gdzie jest bulk. Default zahardkodowany (`O:\hBN\bulk`, ten sam wzorzec placeholderu co
      `ProjectTreePanel`'owy root — zob. T07.5.1). **Auto-wire z poziomu projektu → osobny task,
      zob. "Plan: bulk reference z poziomu projektu" niżej.**

### T08.6.3 — UI
- [x] Vendor ImPlot — zvendorowany + wpięty w `premake5.lua` (`DefectStudio` i
      `DefectStudioTests`), build zweryfikowany (Debug, 0 errors/warnings)
- [x] Panel struktury elektronowej — `ElectronicStructurePanel` (controls: calc-dir, band table,
      localization threshold, bulk reference) + `OccupationDiagramPanel` (osobne okno, diagram
      VB/CB shaded z ImPlot, gap-centered default view, autofit) — dwa dockable panele nad jedną
      `ElectronicStructureSession`
- [x] Klikalny poziom → render funkcji falowej w 3D — band click dispatchuje
      `VaspOrbitalGridJob` → `RegenerateOrbitalIsosurface` (kompute shader, T14)
- [ ] **Control Panel** (zob. sekcja niżej) jako naturalny dom dla suwaków tego panelu — dziś
      suwaki żyją w `ElectronicStructurePanel` samym, nie w centralnym Control Panelu z T12
- [ ] Eksport stanów elektronowych do plików przyjaznych OriginLab (CSV/TSV)
- [x] **Drag-drop WAVECAR na otwartą strukturę w viewporcie — zrobione 2026-08-20**, zob. T08.6.4
      niżej. POSCAR/CONTCAR **nie** przez drag-drop — zostaje RMB "Open Defect" (już istniało,
      `ProjectTreePanel::openDefectAt`)
- [ ] **Dodawanie nowego defektu jako skrót klawiszowy — zażądane 2026-08-23.** Inne niż istniejące
      "Open Defect" (otwiera już istniejący POSCAR/CONTCAR) — to tworzy defekt **od zera**:
      1. skrót klawiszowy odpala flow,
      2. wybór serwera/folderu docelowego (drzewko, prawdopodobnie reużywa
         `nativefiledialog-extended`/`ProjectTreePanel` roots, nie nowy widget od zera),
      3. tworzy strukturę folderów dla **różnych typów obliczeń** — **konkretne typy do ustalenia
         w rozmowie z użytkownikiem przed implementacją, nie zgadywać z tego wpisu** (przykłady z
         reszty kodu: `exc_ms`/`exc_ms_triplet`/`ground_state` już pojawiają się jako nazwy
         podfolderów defektu w przykładach użytkownika, ale to nie jest potwierdzona lista),
      4. auto-generuje/kopiuje `POTCAR` (wybór pseudopotencjałów per-pierwiastek — pokrywa się z
         "Eksport POTCAR" w Backlogu, reużyć nie duplikować), `INCAR`, `KPOINTS`,
         `run_computations` (submit script) per folder obliczenia.
      Zależne od T07.5.2 (Server Profiles, dziś `[ ]`) dla kroku 2 jeśli ma być prawdziwy wybór
      serwera a nie tylko lokalnej ścieżki; zależne od T11 `DefectConcept`/`CalculationRecord` dla
      wpięcia nowo utworzonego defektu w model domenowy, nie tylko w pliki na dysku.
- [ ] **Matplotlib export skrypt** (nowe, z rozmowy 2026-08-20) — statyczny PNG/SVG occupation
      diagram 1:1 ze stylem `OccupationDiagramPanel`, do publikacji. Zob. plan niżej.

**Biblioteki:** ImPlot (zvendorowany), puntukas (Python, już opcjonalna zależność)

---

## Plan – multi-root, drag-drop, bulk auto-wire, matplotlib export (rozmowa 2026-08-20)

> Rozwinięcie T07.5.1/T08.6 z konkretną kolejnością. Kolejność wymuszona zależnościami: multi-root
> manifest jest fundamentem dla bulk auto-wire i dla project-open auto-load — nie da się zrobić
> tamtych dwóch porządnie bez tego pierwszego.

### T07.5.4 — Multi-root project registration
> Potwierdzone jako praktyczne (nie hipotetyczne): wiele serwerów podłączonych naraz, albo jeden
> serwer z danymi rozrzuconymi po kilku folderach/mountach.
- [x] **Zrobione, 2026-08-20 — dwa przejścia.** Pierwsze (rano) budowało N osobnych dockable
      `ProjectTreePanel` — **user to poprawił po kliknięciu**: "jak dodajemy folder to to powinno
      być jako collapsable element w drzewku projektu... trochę jak kolejny entry". Przerobione:
      **jeden panel, N collapsible sekcji** (`ImGui::CollapsingHeader` per root, RMB "Change
      Folder..."/"Remove"). `m_SelectedPath`/`m_ExpandedPaths`/`m_VisibleFlatList` zostały
      panel-level (nie per-sekcja) — klucze to bezwzględne ścieżki, już globalnie unikalne między
      sekcjami, więc `handleKeyboardNavigation`/`renderDirectoryContents` nie wymagały zmian
      logiki, tylko wywołania w pętli. `m_InstanceId`/custom copy-ctor z pierwszego przejścia
      **usunięte** — z jednym panelem znów wystarcza `= default`.
- [x] Roots to teraz `EditorLayer::currentRootsMutable()` — wskazuje albo na aktywny projekt
      (`m_ActiveProject->roots`, patrz T07.5.1) albo na ad-hoc listę (`m_AdHocRoots`,
      `ProjectRootsIO`) gdy żaden projekt nie jest otwarty. Jeden `ProjectTreePanel::SetRoots(...)`
      pcha aktualną listę do panelu po każdej mutacji — panel sam nic nie wie o projektach.
- [x] Add/remove/change-folder — "+ Add Root..." (popup: folder-pick + label) na toolbarze,
      "Change Folder..."/"Remove" per-sekcja (RMB na nagłówku). Zdarzenia
      (`ProjectEvents::RootAddRequested`/`RootRemoveRequested`/`RootPathChangedRequested`) bez
      zmian od pierwszego przejścia — `EditorLayer` tylko mutuje inną listę zależnie od stanu.
- [ ] `source`/`ServerProfile` referencja zamiast wolnego tekstu — zależne od T07.5.2 (nieistniejące)

### T07.5.5 — Bulk reference auto-wire z poziomu projektu
- [x] **Zrobione, 2026-08-20.** RMB na **dowolnym folderze w drzewku** (nie tylko root-level —
      ten sam `renderDirectoryContextMenu` co "Open Defect"), "Set as Bulk Reference" →
      `ProjectEvents::BulkDirectoryChangeRequested` → `EditorLayer::onBulkDirectoryChangeRequested`
      woła `SetBulkDirectory`+`DispatchBulkLoad` na `ElectronicStructureSession` od razu (działa
      nawet bez otwartego projektu, session-only) **i** zapisuje do `manifest.yaml` jeśli projekt
      jest aktywny.
- [x] Bulk directory w manifest.yaml (`bulk_directory` pole) — przeżywa restart jako część
      projektu. `ElectronicStructureSession::m_BulkDirectory` hardcoded default (`O:\hBN\bulk`)
      usunięty (→ puste) — dev-machine ścieżka nie ma co być wbudowanym defaultem skoro jest
      realne miejsce do jej ustawienia.
- [x] `ElectronicStructurePanel::renderBulkReferenceControls()` — **usunięte** wolne pole
      tekstowe + przycisk Browse (user: "nie zniknęła ta opcja z electron configuration" — była to
      świadoma prośba o usunięcie). Zostaje: read-only wyświetlenie ścieżki + hint wskazujący na
      RMB w drzewku + istniejący przycisk "Reload bulk gap".
- [x] Przy `openProject()`: auto-`SetBulkDirectory`+`DispatchBulkLoad` od razu jeśli manifest ma
      ustawiony bulk root, zanim jakikolwiek defekt-window zostanie otwarty.

### T08.6.4 — Drag-drop WAVECAR + jasny podział z context menu (doprecyzowane 2026-08-20, zrobione 2026-08-20)
> User rozstrzygnął: **WAVECAR = drag-drop, POSCAR/CONTCAR = context menu** — nie oba na oba sposoby.
> User: "drag-drop WAVECAR nie działa. jest to trochę kluczowe w momencie kiedy nie może znaleźć
> sama WAVECAR" — chodzi o przypadek gdy `calculationDirectory` (domyślnie folder POSCAR/CONTCAR,
> zob. `ElectronicStructureSession::Update`) **nie** jest folderem z WAVECAR; drop pozwala to
> ręcznie skorygować bez edycji configu.
- [x] `ImGui::BeginDragDropSource` na plikach `WAVECAR` w `ProjectTreePanel` (payload = surowa
      ścieżka jako `char*`, typ payloadu `"DS_WAVECAR_PATH"`) — `renderDirectoryContents`, tylko
      gdy `label == "WAVECAR"`.
- [x] Drop target: otwarte okno struktury w viewporcie (`RendererPanel::renderStructureWindow`,
      zaraz po `ImGui::Image`) — nowy event `RendererEvents::Viewport::WavecarDropped{windowId,
      wavecarPath}`, `EditorLayer::onWavecarDropped` ustawia `calculationDirectory` (=
      `wavecarPath.parent_path()`) w `ElectronicStructureSession::WindowState` danego okna i od
      razu woła `DispatchOutputLoad`. Drop na okno bez struktury nie jest możliwy z definicji —
      target to samo `ImGui::Image` viewportu, które istnieje tylko dla już-otwartego okna.
- [x] POSCAR/CONTCAR **zostaje jak jest** — RMB "Open Defect" (`ProjectTreePanel::openDefectAt`,
      już działa), **nie** dodano drugiej ścieżki przez drag-drop dla tych plików (osobna od
      T08 linijki ~540 "Drag & drop pliku POSCAR/CONTCAR/CHG **z eksploratora**" — to inny wektor,
      OS Explorer → appka, zostaje jako odrębny, niezrealizowany task; pozycja w "🔥 Hotfixy"
      (linijka ~149) "Drag&drop z `ProjectTreePanel` (POSCAR/CONTCAR) na viewport" jest
      **superseded** przez tę decyzję — nie implementować, POSCAR/CONTCAR zostaje context-menu-only)
- [x] **Bug po pierwszym teście użytkownika: drop "działał" (job kończył się OK) ale nic się nie
      pokazywało** — `ElectronicStructurePanel`/`OccupationDiagramPanel` renderują/pollują tylko
      `FindFocusedWindow()` (ostatnio zogniskowane okno viewportu), a sam drag-drop nigdy nie
      klika okna docelowego, więc `m_LastFocusedViewportWindowId` w `RendererLayer` się nie
      zmieniał — dane ładowały się w tle, ale żaden panel ich nie odbierał. Fix: `RendererPanel`
      woła `ImGui::SetWindowFocus()` zaraz po zaakceptowaniu dropu (przenosi fokus/tab na wierzch,
      efekt widoczny klatkę później — ten sam istniejący mechanizm `FocusChanged`, który normalny
      klik w viewport i tak by wywołał).
- [x] **Drugi bug z tego samego testu: WAVECAR na dysku sieciowym (`O:\`) — job kończył się OK,
      band-gap/HOMO-LUMO ładowały się poprawnie, ale orbitale zawsze "Orbitals unavailable (no
      WAVECAR)" mimo że plik fizycznie istniał.** Przyczyna: `vasp_output_load.py::_orbitals_payload`
      łapał `FileNotFoundError` (WAVECAR faktycznie brak) i `AssertionError` (WAVECAR jest, ale
      puntukas nie mógł odczytać nagłówka — już wcześniej udokumentowany w komentarzu przypadek:
      "k-point count read as 0" na dysku sieciowym, prawdopodobnie uszkodzony/nie w pełni
      przesłany plik) w jeden, nierozróżnialny komunikat. Fix: Python zwraca teraz osobno
      `orbitals_error` (string, tylko dla `AssertionError`) w JSON payloadzie; przeciągnięte przez
      `VaspOutputBridge`/`VaspOutputConversion`/`ElectronicStructureData::orbitalsError` do
      `ElectronicStructurePanel`, który pokazuje realny komunikat błędu (na czerwono) zamiast
      generycznego "no WAVECAR" gdy WAVECAR **jest**, tylko nieczytelny. Nie naprawia samego
      odczytu (to leży w puntukas/sieciowym I/O, poza zasięgiem tej appki) — tylko diagnozowalność.
      **Follow-up po tym fixie:** dla konkretnego pliku użytkownika (`O:\hBN\convergance_test\882\
      V_2\obliczenia\singlet\HSE\WAVECAR`, 4.77 GB) zdiagnozowane bezpośrednim odczytem bajtów —
      **~74% pliku to zera**, rozrzucone od 0.02 GB do 4.74 GB (nie tylko początek/koniec ucięty).
      To nie bug appki/puntukas — plik jest uszkodzony/niedokończony transfer na dysku sieciowym
      (prawdopodobnie delta-sync narzędzie jak rsync bez `--whole-file`). Wymaga pełnej, nie-delta
      re-kopii pliku od źródła; nic do zrobienia po stronie kodu.

### T08.6.7 — Orbital render w image export pipeline (rozmowa 2026-08-20, odłożone wcześniej w sesji, zrobione 2026-08-20)
- [x] `RenderExportDialogState::previewState.orbitalChannelUp/Down` (już istniały, nigdy nie
      wypełniane przez `ExportImagePanel`) — teraz `.enabled` **są** przełącznikami "pokaż
      orbitale" (bez osobnej flagi), kolory/opacity edytowalne tymi samymi widgetami co
      `ElectronicStructurePanel::renderWavefunctionControls`.
- [x] Live preview w dialogu eksportu odświeża siatkę na klucz `"__export_preview__"` co klatkę
      (ten klucz i tak renderuje się co klatkę niezależnie — bez tańca "renderuj dwa razy").
- [x] Tabelka batch: checkboxy per band (reużywa `FilterByLocalizationThreshold` — te same dane co
      żywy band table), "Select all/none", przycisk "Export N selected orbitals" — **nie zamyka
      dialogu** (w przeciwieństwie do zwykłego "Export"), bo musi renderować progress klatka po
      klatce (`ExportImagePanel::stepOrbitalBatchExport`, wołane co klatkę niezależnie od tego,
      która sekcja dialogu jest widoczna).
- [x] Nazwa pliku per orbital: `{filename}_band{N}.png`; domyślny `dialog.filename` teraz z
      `windowState->title` zamiast `sourcePath` stem — użytkownik prosił wprost o nazwę na
      podstawie nazwy okna (dotyczy też zwykłego pojedynczego eksportu).
- [x] **Znaleziony podczas planowania, kluczowy constraint**: `RegenerateIsosurfaceGpu(windowKey)`
      (z fixu isovalue wyżej) no-opuje na kluczu bez wcześniejszego `RenderWindow`. Dla
      `"__export_full__"` (renderowany tylko na żądanie, nie co klatkę) to wymaga renderu
      dwukrotnie na band: raz żeby ustanowić/odświeżyć viewport, regenerate mesh, raz jeszcze żeby
      go faktycznie narysować (`ExportImagePanel::renderOrbitalBandForCapture`).
- [x] Nowa metoda `ElectronicStructureSession::TryGetOrDispatchGrid` (cache-or-dispatch, bez
      dotykania GPU) + `WindowState::gridFetchErrors`/`GridFetchError` (nowość — `pollGridJobs`
      wcześniej zapisywał błąd tylko dla aktywnego GPU-slotu, więc batch dispatch dla banda spoza
      dwóch aktywnych slotów retry'owałby permanentnie zepsuty band w nieskończoność; teraz zapisuje
      błąd dla **każdego** klucza, batch stepper odróżnia "czeka" od "poddał się po błędzie").
- [x] Nowa `RendererLayer::RegenerateOrbitalIsosurfaceForChannel` — jak `RegenerateOrbitalIsosurface`,
      ale bez `findWindowById` (synthetic export-key nigdy nie jest w `GetWindows()`).
- [x] `ExportImagePanel` przeniesiony w kolejności rejestracji za `m_ElectronicStructureSession` w
      `EditorLayer::initializePanelsIfNeeded` (był przed, więc `Ref` byłby pusty).

### T08.6.5 — Batch/prefetch orbitali z WAVECAR
- [ ] Dziś `VaspOrbitalGridJob` ładuje jeden orbital (spin/k-point/band) na klik — brak prefetch
      sąsiednich bandów w band table. Rozszerzyć `ElectronicStructureSession` o kolejkę N-najbliższych
      bandów wokół ostatnio klikniętego, low-priority w `JobSystem` (nie blokować głównego joba)

### T08.6.6 — Matplotlib static export
- [ ] Nowy skrypt (`scripts/python/examples/electronic_structure_plot.py` albo we wspólnym module)
      — bierze band window / VBM-CBM shading / split-spin-channel / localization threshold jak
      `OccupationDiagramPanel`, żeby nie duplikować logiki filtrowania po stronie C++ **i** Python
      osobno: albo czyta CSV/TSV z eksportu T08.6.3 (kolejność: **CSV/TSV export najpierw**, ten
      skrypt na nim, nie na surowym JSON-line z `VaspOutputBridge`), albo bierze te same argumenty
      CLI co `VaspOutputBridge`'owy Python-side call i filtruje identycznie
- [ ] Output PNG/SVG, kolory/strzałki 1:1 z `OccupationDiagramPanel` (nie osobna paleta)

---

## T09 – Advanced Renderer (`task/09-advanced-render`)

> Po działającym T08. Poprawiamy jakość renderowania.

- [x] **Compute shader dla bond transform** — **przeniesione z `[ ]` na `[x]`: zweryfikowane,
      już zaimplementowane i dispatchowane w T06**, nie tylko zaplanowane
- [x] **MSDF (Multi-channel SDF) dla 3D labels zamiast billboard quads — zrobione 2026-08-22.**
      Renderowanie MSDF (`sjcmdev/msdf-atlas-gen`, `MsdfFont`, `labels.vert/frag`) zrobione wcześniej
      w tej sesji (bond/angle label matrix). **ECS entity — domknięte 2026-08-22:** nowy
      `LabelComponent` (`SceneComponents.hpp`, back-reference do `pinnedMeasurements[index]`) +
      `SceneRegistry::LabelEntities()`/`LabelEntityAt` (wzorzec identyczny do Atom/BondEntities) +
      `SceneSystem::SyncLabelEntities`/`UpdateLabelTransforms`/`SyncLabelSelection` — jedno entity per
      pin, z `TransformComponent` (żywa pozycja anchor+offset, odświeżana co klatkę) i
      `SelectionComponent` (mirror `selectedPinnedMeasurement`). Sync wywoływany raz na batch po
      `AddBondPinsWithinSet`/`AddAnglePinsWithinSet`/`RemovePinsWithinSet` (nie per-pin w pętli —
      `SyncLabelEntities` niszczy/tworzy wszystkie encje, O(n²) inaczej) i po Delete pojedynczego pinu.
      **Gizmo — `RendererPanel::renderLabelTransformGizmo`:** Translate to 3-osiowe strzałki, ten sam
      wzorzec shaft+arrowhead i fallback screen-space pick/drag co gizmo atomów, ale osobny stan
      (`labelGizmo*` pola w `RendererWindowState`) bo operuje na jednym punkcie (`PinnedMeasurement::
      worldOffset`) zamiast listy atomów. `PinnedMeasurement::screenOffset` (vec2, camera-plane-only)
      **zamieniony na `worldOffset`** (vec3, pełny world-space) — stary click-drag-na-etykiecie nadal
      działa (pisze do tego samego pola przez rzut cameraRight/cameraUp), gizmo dokłada ruch wzdłuż
      dowolnej osi świata. Efekt uboczny: usunięty stary "swim przy orbicie kamery" z
      `OpenGlRendererBackend::renderLabels` (offset był przeliczany z bieżącej macierzy widoku co
      klatkę — teraz to stały wektor world-space). **Rotate/Scale i Undo/redo dociągnięte
      2026-08-22, zob. szczegóły w "Pomiary" → "Rotate/Scale na gizmie etykiet + Undo/redo" niżej.**
      **Image-export pipeline — domknięte 2026-08-22:** prawdziwy bug, nie tylko brakująca funkcja —
      `RenderExportDialogState::previewState` to świeży `RendererWindowState`, nie kopia żywego okna,
      więc `pinnedMeasurements` zostawało puste i piny **nigdy nie trafiały do eksportu** niezależnie
      od checkboxa "Labels" (który i tak steruje tylko trybem auto-all-bonds, osobnym od pinów).
      Naprawione w obu miejscach otwierających dialog (`RendererLayer::onExportImageRequested` (F12) i
      `RendererPanelToolbar.cpp` przycisk "Export PNG...") — kopiują teraz `pinnedMeasurements` +
      `bondLabelsAlignToDirection`, bez podświetlenia zaznaczenia w eksporcie
      (`selectedPinnedMeasurement` zostaje -1). Zweryfikowane: Debug build czysty (0 warningów).
      **Rotate/Scale na gizmie etykiet + Undo/redo — domknięte 2026-08-22.**
      - [x] **Rotate/Scale.** `PinnedMeasurement` ma nowe `rotationOffsetRadians`/`scale`.
            `renderLabelTransformGizmo` rozgałęzia się na `windowState.gizmoOperation`: Translate to
            wciąż 3-osiowe strzałki (bez zmian), Rotate/Scale to jeden pierścień wokół pivotu bez
            osi X/Y/Z do wyboru — billboard etykiety ma tylko jedną sensowną oś obrotu (własną normalną
            skierowaną w kamerę) i jedną sensowną skalę (jednolity rozmiar glifu), więc nie ma między
            czym wybierać per-oś. Rotate: kąt myszy wokół pivotu akumulowany przyrostowo (atan2,
            znak y odwrócony bo screen-space jest y-down a lokalne "up" etykiety w `labels.vert` jest
            y-up). Scale: stosunek promienia bieżącego do promienia z początku dragu (styl Blenderowego
            `S` — powrót myszy do promienia startowego zawsze wraca do startowej skali), clamp
            [0.1, 8.0]. Renderowanie: `AppendLabelInstances` (`OpenGlRendererBackend.cpp`) mnoży cały
            `localOffsetSize` (offset+size) przez `scale` — jednolite skalowanie wokół własnego
            zakotwiczenia etykiety, nie world origin; `rotationOffsetRadians` dochodzi do już liczonego
            `rotationRadians` dla pinów wiązań, a dla pinów kąta (wcześniej zawsze upright, 0) jest
            teraz jedynym źródłem rotacji.
      - [x] **Undo/redo.** Osobny, lokalny stos per-okno (`pinnedMeasurementUndoHistory`/
            `RedoHistory` na `RendererWindowState`, snapshot całego wektora `pinnedMeasurements`) —
            **nie** globalny `Core/Undo`/`Ctrl+Z`, tym samym wzorcem co już istniejący lokalny
            view-undo kamery (`Ctrl+Alt+Z`/`Ctrl+Alt+Shift+Z`, decyzja architektoniczna z T06.5: stan
            renderera nie będący domeną dostaje własny stos i własny skrót, nie miesza się z domenowym
            undo atomów). Nowy skrót **`Ctrl+Alt+U`** / **`Ctrl+Alt+Shift+U`** (`renderer.labels.undo`/
            `.redo`, cały łańcuch event→command→keybinding wzorem `renderer.undo_view`/`redo_view`) —
            `M`-rodzina (wszystkie 8 kombinacji Ctrl×Alt×Shift+M) była już w pełni zajęta przez macierz
            pinowania, podobnie `Z`/`Y`, więc `U` (od "Undo") to pierwszy wolny, nieskolidowany chord.
            `PushPinnedMeasurementUndoSnapshot` (wolna funkcja, `RendererLayer.hpp/.cpp` — wołana
            zarówno z wewnętrznych helperów `AddBondPinsWithinSet`/`AddAnglePinsWithinSet`/
            `RemovePinsWithinSet`, jak i bezpośrednio z `RendererPanel.cpp` przy F-flip/Delete/starcie
            każdego typu dragu) pushuje snapshot **raz na całą logiczną edycję** (cały bulk-add, cały
            drag), nie per-klatkę/per-pin w pętli — z guardem: jeśli bulk add/remove nic faktycznie nie
            zmienił (np. `M` nad już w pełni popinowanym zaznaczeniem), pushnięty snapshot jest zdejmowany
            zamiast zaśmiecać historię pustym krokiem. Undo/redo resetuje zaznaczenie pinu i
            w-trakcie-dragu flagi (indeks mógł przestać być aktualny po przywróceniu innego wektora),
            resync ECS przez `SceneSystem::SyncLabelEntities`. Zweryfikowane: Debug build czysty
            (0 warningów).
- [x] Multi-viewport — wiele viewportów z niezależnymi ustawieniami kamery i renderowania
      (potwierdzone: `RendererWindowState` per-window, `m_Viewports` w backendzie)
- [ ] Quick image export (PNG/JPG z aktualnego viewportu)
- [ ] PBR lighting (opcjonalny, publication-quality renders)
- [ ] SVG export pipeline

---

## T11 – Domain Runtime Model (`task/11-domain-runtime`)

> **Rozgraniczenie z T07.5 (2026-08-13):** T07.5 jest właścicielem *persystencji* projektu (katalog
> na dysku/mount, `manifest.yaml`, lifecycle otwierania/zapisu). T11 jest właścicielem *runtime
> modelu domenowego* nad tym co już jest otwarte (defekty jako encje, kolekcje danych obliczeniowych)
> — nie duplikować "ProjectWorkspace lifecycle" w obu miejscach.

- [ ] `StructureRegistry` – **klasa już istnieje** i działa standalone (Add/Find/Records), wpięta
      w startup rendererowy (T06.5); brakuje runtime importu z UI (zależne od T07.5 Open/Recent —
      dziś jedyna ścieżka wypełnienia to startup, nie akcja użytkownika w trakcie sesji)
- [ ] `DefectConcept` – defekt jako pierwszorzędna encja naukowa (pozycja, typ, tagi, stany ładunku).
      **`Domain/Defects/DefectModel.hpp/.cpp` już istnieje** (`DefectConcept`/`DefectConfiguration`/
      `CalculationRecord` + rejestry w `ProjectWorkspace`, `BuildDefectedStructure`) — scalone przy
      okazji T07, ale niepodłączone do `CrystalStructure::vacancies` (istnieje jako martwe pole,
      zob. T07). Ten task domyka wiring, nie pisze typów od zera.
- [ ] Filter view po charge state i spin channel
- [ ] Project-scoped query/index helpers
- [ ] **Defekty punktowe — rozszerzenie `DefectConcept` (z `old-ds-functionality.md` §16.1):**
  - [ ] Interstitial: wstawienie atomu w pozycji wysokiej symetrii (podpowiedź z symetrii komórki, zależne od T13)
  - [ ] Antisite: zamiana pierwiastka atomu z zachowaniem historii
  - [ ] Substitutional dopant: jak antisite, z maskowaniem przy eksporcie
  - [ ] Eksport z/bez defektu: toggle "pristine vs defected" do porównania
- [ ] **Podział pracy z `punktukas-tools` (2026-08-13, zob. Infrastruktura):** formation energy,
      charge density difference i inne ciężkie obliczenia na `CalculationRecord` idą przez
      `punktukas`/pymatgen po stronie Python (`ScientificRuntime`), **nie** reimplementacja w C++.
      Nasza strona: model danych (`DefectConfiguration`/`CalculationRecord`) + UI do ich przeglądania.

---

## T12 – Config, Settings, UX Polish (`task/12-ux`)

- [ ] Settings window (dwie kolumny: label – wartość, wyrównane) — **PARTIAL: `Settings.cpp` już
      ma >2000 linii UI, ale nie weryfikowano układu kolumn w tym przeglądzie**
- [ ] Appearance panel vs Viewport Settings vs Render Image – czysta taksonomia
- [ ] Per-project element appearance overrides (osobny plik od scene\_state)
- [ ] Import/export/reset workflow dla overrides
- [ ] Persist ImGui dock/panel layout
- [ ] Konfigurowalny skróty klawiaturowe i zachowanie myszy
- [x] **Shortcut conflict detection — przeniesione z `[ ]` na `[x]`: zweryfikowane, w pełni
      zaimplementowane.** `KeymapResolver` wykrywa i loguje konflikty (`m_Conflicts`,
      `GetConflicts()`), a `Settings.cpp` już to wyświetla w UI (dwa miejsca użycia
      `GetConflicts()` w panelu ustawień)
- [x] **Saved-view keybindy ujednolicone — zrobione 2026-08-21.** `RendererViewSnapshot` ma teraz
      `name` (auto `"View N"` na `Shift+V`, persystowane jako 4. segment w istniejącym formacie
      pipe-delimited). `RendererLayer` wystawia Rename/Delete/Move/Apply na
      `m_SharedSavedViews`, nowa zakładka "Saved Views" w Settings (tabela z Apply/Rename/Up/Down/
      Delete) — nie ślepy cykl. **Świadomie zostaje jak było:** lista wciąż globalna dla
      wszystkich okien (nie per-window/per-projekt) — per-projekt scoping to T07.5.1, osobny task.
- [x] **Text editor panel dla plików projektu — zażądane sesją 2026-08-21 ("bardzo ważne w
      codziennej pracy"), zrobione, branch `task/12-ux/text-editor-panel`.** Vendorowany
      `sjcmdev/ImGuiColorTextEdit` (fork z inną, nowszą wewnętrzną architekturą niż klasyczny
      BalazsJako — `TextEditor::Render()` zwraca `documentChanged` zamiast osobnej flagi dirty;
      `TextDiff.cpp`/`dtl.h` — opcjonalna funkcja diff, nie kompilowane, nieużywane). Fork bez
      premake5.lua → lokalny `DefineImGuiColorTextEditProject()` (StaticLib, wzorzec `nfd`/
      `ImGuizmo`). Jeden dockowalny panel (bez tabs — otwarcie kolejnego pliku podmienia treść),
      double-click na dowolnym liściu w `ProjectTreePanel` → `ProjectEvents::TextFileOpenRequested`
      → `EditorLayer` ładuje przez `TextFileIO::Load` do jedynej instancji panelu. `Ctrl+S` zapisuje
      przez `TextFileIO::Save` (gating przez `IsWindowFocused`, żeby nie kolidować z globalnym
      `system.save`/`project.save`). **Nie zrobione (MVP scope):** brak promptu "niezapisane zmiany"
      przy podmianie pliku, brak syntax highlightingu dla INCAR/skryptów (`SetLanguage()` istnieje w
      forku, nieużyte), brak wielu otwartych plików jednocześnie (tabs) — oba dwa
      spriorytetyzowane razem jako #6 w T08 "Priorytetyzacja i kolejność implementacji" wyżej.
- [ ] F2 rename w Scene Outliner (zależne od T08 SceneOutliner)
- [ ] Viewport resolution tuning (redukcja GPU load)
- [ ] Range selection z Shift w kolekcjach
- [ ] Hierarchia ustawień: Application / Project / Scene-Workspace
- [ ] Save current layout jako user default
- [ ] Keyboard shortcut reference panel

---

## T13 – Space Group Analyzer (`task/13-spacegroup`)

> Analiza symetrii. Backend: spglib przez Python bridge (T05). **Zweryfikowane: brak jakichkolwiek
> śladów spglib/spacegroup w `src/` — od zera.**

- [ ] Space group detection z konfigurowalną tolerancją (spglib backend)
- [ ] Space group browser – przeglądanie listy grup 1–230
- [ ] Automatyczna generacja symetrycznie równoważnych pozycji
- [ ] Wyświetlenie operatorów symetrii dla wybranej grupy przestrzennej
- [ ] Toggle: praca bez symetrii / z wymuszoną symetrią
- [ ] Sprawdzenie kompatybilności struktury z daną grupą przestrzenną
- [ ] Standaryzacja komórki, redukcja do prymitywnej, ekspansja do konwencjonalnej
- [ ] Wyświetlenie pozycji Wyckoffa, krotności i symetrii miejsca dla każdego atomu
- [ ] **Supercell (z `old-ds-functionality.md` §16.2, logicznie należy tu / do T07):**
  - [ ] Generowanie superceli N×M×K
  - [ ] Dopasowanie macierzy transformacji (np. do konkretnej orientacji powierzchni)
  - [ ] Import superceli i rozpoznanie relacji do komórki prymitywnej
- [ ] **Analiza struktury (§16.3):** identyfikacja nieekwiwalentnych pozycji atomowych,
      packing factor (APF), wykrywanie nieciągłości sieciowych (atomy poza komórką, duplikaty,
      atomy za blisko siebie)

**Biblioteki:** spglib (przez Python bridge), pymatgen (`SpacegroupAnalyzer`)

---

## T14 – Volumetrics: CHG/PARCHG/CHGCAR (`task/14-volumetrics`)

> Compute shader jako jedyna ścieżka isosurface. Bez CPU Marching Cubes jako main path.

- [ ] Scalar field data model (oddzielony od atom/bond data)
- [ ] CHG/CHGCAR/PARCHG parser (format VASP, multi-block) – port z repo A. Z
      `old-ds-functionality.md` §1.4: nagłówek struktury z pliku CHGCAR (opcjonalne zastąpienie
      struktury sceny), interpretacja VESTA-like dla PARCHG (blok 1 = gęstość totalna, blok 2 =
      magnetyzacja, kanały spin-up/spin-down), walidacja spójności siatki/gatunków/kolejności
      atomów/pozycji względem sceny
- [ ] Wewnętrzna binarna reprezentacja/cache dla szybszego reload
- [ ] Lazy loading bloków (background JobSystem)
- [ ] Block statistics: min, max, mean, memory footprint
- [x] **OpenGL 4.3 Compute Shader – isosurface mesher** (SSBO grid → dispatch → mesh) —
      **zrobione 2026-08-20, `isosurface_march.comp`**. Marching **tetrahedra** (6-tet cube
      decomposition, 16-case table), nie marching cubes literalnie — świadomy wybór, niższe
      ryzyko transkrypcji niż 256-case marching cubes dla pierwszego przejścia bez GL test
      coverage. `RegenerateIsosurfaceGpu` to dziś **jedyna produkcyjna ścieżka**
      (`RendererLayer::RegenerateOrbitalIsosurface`), zgodnie z założeniem "compute shader jako
      jedyna ścieżka" wyżej. CPU mesher (`IsosurfaceMesher.cpp`) zostaje jako reference/test-only
      (`IsosurfaceMesherTests.cpp`), nie main path — pokrywa punkt niżej.
- [ ] ~~SSBO z probability density |ψ|²~~ — **świadomie zbudowane inaczej**: grid niesie **signed
      real part** funkcji falowej (φ, nie |ψ|²) właśnie po to, żeby zachować znak +/- lobe do
      VESTA-like koloringu (zob. `VaspOrbitalGridBridge` komentarz). |ψ|² zabiłoby znak. Zostawić
      checkbox jako "nie dotyczy" zamiast odznaczać jako zrobione — to inna decyzja, nie to samo zadanie.
- [ ] Request/commit model dla compute dispatch (render thread owner, nie bezpośrednia mutacja live-state)
- [x] Backend abstraction: Compute Shader / CPU fallback — CPU (`IsosurfaceMesher.cpp`) i GPU
      (`isosurface_march.comp`) istnieją równolegle, GPU jest production path, CPU debug/test-only
- [x] **Bug: globalny ISOVALUE między oknami** (zgłoszone przez użytkownika po teście, naprawione
      2026-08-20) — GPU-owe bufory izopowierzchni (`m_IsosurfaceGpuVao`/`VertexSsbo`/`CounterSsbo`)
      były jedynymi zasobami OpenGL trzymanymi jako **globalne 2-slotowe tablice na cały backend**
      zamiast per-okno (w przeciwieństwie do reszty `OpenGlViewportResources`) — każde okno ze
      spin-up/spin-down orbitalem renderowało z tego samego VAO, więc zmiana iso-value (albo
      dowolna regeneracja siatki) w jednym oknie nadpisywała mesh widoczny w każdym innym. Fix:
      przeniesione do `OpenGlViewportResources` (per-`windowKey`, lazy `ensureIsosurfaceBuffers`),
      `RegenerateIsosurfaceGpu` przyjmuje teraz `windowKey`. Świadomie **nie** naprawione przy
      okazji: `RendererLayer::RemoveWindow` nadal nie zwalnia `OpenGlViewportResources` zamkniętego
      okna (pre-existing gap, cała struktura, nie tylko izopowierzchnia) — dopasowane do istniejącego
      (niedoskonałego) cyklu życia zamiast wprowadzać nowy mechanizm sprzątania tylko dla tego pola.
- [x] Single-iso i dual-iso rendering (positive/negative lobes, VESTA-like) — dispatch dwa razy
      (dodatni/ujemny lobe) z memory barrier, osobne kolory per lobe (`orbitalChannelUp`/`Down`
      positive/negative color w `RendererWindowState`)
- [ ] Per-surface controls: iso value, kolor, opacity, widoczność; persystencja stanu w manifeście projektu
- [ ] Async CPU-side preprocessing i immutable compute input preparation
- [ ] VESTA-like workflow panel (side-by-side surfaces, mniej tekstu)
- [ ] Reduced preview resolution (performance guardrail)
- [ ] Polyhedra visualization (coordination polyhedra wokół wybranych atomów)
- [ ] **Pomiary zaawansowane (§16.5, logicznie tu bo zależą od struktury+analizy):**
  - [ ] Mapa odległości do wybranego defektu (distance field, kolorowanie atomów)
  - [ ] Dystrybucja długości wiązań jako histogram
  - [ ] Analiza środowiska koordynacyjnego (Voronoi + polyhedra)
  - [ ] RDF (Radial Distribution Function) z wbudowanym wykresem (zależne od `ImPlot`, zob. T01)

---

## T15 – Offscreen Render Pipeline (`task/15-render-export`)

- [ ] Render dialog z podglądem frame
- [ ] Offscreen render do PNG/JPG przy podanej rozdzielczości
- [ ] Presets rozdzielczości (1080p, 2K, 4K, custom)
- [ ] Crop rectangle w pipeline eksportu
- [ ] Render look overrides (białe tło, override kolorów atomów)
- [ ] Bond label scale consistency across resolutions
- [ ] Ustawienia renderu oddzielne od viewport appearance
- [ ] Domyślna nazwa pliku: `[project_name]_[struktura]_[timestamp]`

---

## Backlog – po T15

> Dodawać w miarę potrzeb. Nie implementować przed stabilizacją T01–T15.

- [ ] Structure Authoring wizard (prototypy, supercell builder, cell definition)
- [ ] Plik projektu jako archiwum ZIP (`.dsproj`) – miniz/libzip
- [ ] Remote SSH / SFTP browser bez OS-mounta (`libssh2`) — **zob. T07.5.2 stretch**; budować tylko
      jeśli mount-first (SMB/SSHFS-Win) okaże się niewystarczający w praktyce
- [ ] VASP OUTCAR/WAVECAR integration
- [ ] **Moduł poprawki FNV (Freysoldt-Neugebauer-Van de Walle) — zażądane 2026-08-21, priorytet
      wysoki:** korekta energii formacji defektów naładowanych (finite-size/image-charge, potencjał
      elektrostatyczny defekt vs pristine, alignment poza rdzeniem defektu). Wiąże się z formation
      energy calculatorem z bullet "Energetyka i DFT" niżej — bez tego formation energy dla defektów
      z niezerowym ładunkiem jest błędne przy skończonej superkomórce.
- [ ] **Moduł generacji testów zbieżności i defektów do obliczeń wielkoskalowych — zażądane
      2026-08-21:** automatyczna seria testów zbieżności (k-points/ENCUT, seria inputów + parsing
      wyników), masowa/high-throughput generacja struktur defektowych (kombinacje pozycji/typów/
      ładunków) pod batch obliczenia. **Prior art znaleziony 2026-08-24 przy okazji T16:**
      `punktukas.automatization` (`band_path.py`/`cp2k_convergence.py`/`equation_of_state.py`/
      `spin_contamination.py`/`benchmark_mpi_config.py` w źródle `punktukas-tools`) już częściowo
      pokrywa ten temat po stronie Python — sprawdzić przy starcie tego zadania zanim cokolwiek się
      projektuje od zera, ten sam duch co "nie reimplementować OUTCAR parsing" (zob.
      `docs/work/project/plans/2026-08-23-python-scripting-console.md` sekcja 2.5).
- [ ] Defect thermodynamics
- [ ] **Python scripting panel — wprost zażądane 2026-08-21, priorytet w górę z Backlogu:** REPL w
      UI, styl ipython (podpowiedzi składni + ścieżek), hot-reload przez file watcher np. `efsw`,
      debugger VSCode/debugpy — `ds` module z submodułami `ds.scene`/`ds.commands`/`ds.events`/`ds.app`;
      `T05` daje już fundament embeddingu przez nanobind. **MVP vs stretch:** samo wykonanie
      snippetu Pythona przeciw `ds` + output/error pane to małe (reuse `PythonInterpreter`
      RAII/GIL z T05); pełne autocomplete-jak-w-ipython dla składni i ścieżek to osobny, znacznie
      większy kawałek (introspekcja obiektów `ds`, filesystem completion) — nie robić na starcie
      w jednym kroku z MVP. **Siostrzane zadanie, zażądane 2026-08-22:** integrated PowerShell
      terminal panel (T08 replanning wyżej) — ogólny shell, nie REPL ze scenom; oba mogą dzielić
      grupę doków "Integrated tools", ale to osobne zadania implementacyjne. **Spriorytetyzowane
      razem z resztą jako #4 w T08 "Priorytetyzacja i kolejność implementacji" wyżej** (PowerShell
      terminal tam jako #16 — Python console wyżej w kolejności, PowerShell niżej).
  - [ ] **Rozszerzenie zakresu — zażądane 2026-08-23, użytkownik sam ocenia jako duże i trudne.**
        Sam REPL/hot-reload wyżej to za mało — realny cel to **scriptable object model całego
        projektu**, dostępny identycznie z (a) hot-reloadowanych skryptów `.py` wpiętych w scenę,
        (b) interaktywnej konsoli w UI, (c) **Jupyter/`.ipynb`** (ten sam kernel/`ds` module, nie
        osobna integracja). Przykład z rozmowy (składnia orientacyjna, nie kontrakt):
        ```python
        6_7_defect = project.okeanos["6-7"]
        exc_singlet = 6_7_defect["exc_ms"].OUTCAR['free energy']
        gs_singlet = 6_7_defect["ground_state"].OUTCAR['free energy']
        exc_triplet = 6_7_defect["exc_ms_triplet"].OUTCAR['free energy']
        6_7_ZPL = 2 * exc_singlet - exc_triplet - gs_singlet
        ```
        tzn. `project[...]`/`defect[...]` adresuje encje modelu domenowego (`DefectConcept`/
        `CalculationRecord` z T11) po nazwie/tagu, a pola typu `.OUTCAR['free energy']` czytają
        sparsowane wyniki (`VaspOutputBridge`/T08.6) leniwie. **To NIE jest gotowe do
        implementacji wprost — wymaga osobnego projektu/designu przed kodem** (użytkownik: "Należy
        zaprojektować cały system"), bo dotyka na raz kilku nierozwiązanych decyzji:
        - **Model adresowania** — jak `project.okeanos["6-7"]["exc_ms"]` mapuje się na
          `DefectConcept`/`CalculationRecord`/`StructureRegistry` (T11); czy `okeanos` to nazwa
          projektu, serwera, czy grupy defektów — do ustalenia, nie zakładać.
        - **Persystencja** — czy wyniki/nazwy-referencje z sesji Python (`6_7_ZPL` wyżej) mają
          przeżywać restart appki (i jako co: cache, czy re-liczone leniwie za każdym razem).
        - **Bezpieczny zapis** — skrypt z hot-reloadem mutujący scenę (dodaje/usuwa atomy, zmienia
          projekt) współbieżnie z UI-em edytującym to samo — potrzebna jasna zasada kto ma prawo
          pisać kiedy (main-thread-only commit już obowiązuje ogólnie, zob. "Granice
          architektoniczne" na górze pliku — ale trzeba to jawnie rozciągnąć na skrypty).
        - **Undo/Redo** — mutacje sceny ze skryptu Python muszą przechodzić przez ten sam
          `Core/Undo`/`UndoStack` co mutacje z UI (T08), nie osobną, niewidoczną dla usera ścieżkę.
        - **Wielu serwerów** — adresowanie encji, których dane obliczeniowe leżą na różnych
          zamontowanych serwerach naraz (zob. T07.5.2 Server Profiles, dziś `[ ]` nieistniejące) —
          `project.okeanos[...]` musi wiedzieć/nie musieć wiedzieć na którym mouncie faktycznie
          leżą pliki tego konkretnego defektu.
        - **`.ipynb` wsparcie** — czy przez faktyczny Jupyter kernel (ipykernel) osadzony/
          zewnętrzny wołający ten sam `ds`/`PythonInterpreter`, czy appka renderuje/edytuje
          notebooki własnym UI (`nbformat` roundtrip) — różne koszty, do rozstrzygnięcia na starcie.
- [ ] **Energetyka i DFT (`old-ds-functionality.md` §16.4):** uruchamianie VASP/Quantum ESPRESSO
      ze sceny (przez `PythonScriptJob`), monitorowanie konwergencji SCF na żywo (parsing OUTCAR/log),
      import sił na atomy z OUTCAR + wizualizacja wektorów, formation energy calculator
      (defekt vs pristine), charge density difference (CHG defekt − CHG pristine)
- [ ] **Eksport i integracja (§16.6):** CIF, XYZ/extended XYZ (ASE-kompatybilny), VESTA `.vesta`,
      siatka k-punktów (KPOINTS, Monkhorst-Pack/Gamma), eksport POTCAR (wybór pseudopotencjałów)
- [ ] **Kolaboracja i historia (§16.8):** git-like snapshot timeline projektu (osobne od undo stack),
      eksport diff struktury (dodane/usunięte/przesunięte atomy), adnotacje przypisane do atomów/pozycji
- [ ] **Rendering (§16.9, poza tym co już w T09):** ray-traced render (opcjonalnie Blender via
      subprocess), animacja: sekwencja klatek z AIMD/NEB (interpolacja ścieżek atomów)
- [ ] **Moduł generacji komórek startowych — zażądane 2026-08-21, niski priorytet:** budowanie
      struktury początkowej (bulk/primitive → supercell, ewentualnie od zera z grupy przestrzennej)
      jako punkt wejścia przed defektowaniem/testami zbieżności powyżej
- [ ] **Materials Collection + panel generacji materiałów/superkomórek/właściwości — zażądane
      2026-08-23.** Persystentna biblioteka definicji materiałów (nazwa, sieć, atomy bazowe,
      grupa przestrzenna, metadane) do przeglądania i wybierania jako punkt startowy — **to NIE
      jest odrzucone 2026-08-22 Collections/Groups** (te miały grupować obiekty/podzbiory
      WEWNĄTRZ jednej sceny/okna, czego appka nie potrzebuje przy modelu "jedna struktura = jedno
      okno"; to jest biblioteka materiałów NA POZIOMIE APLIKACJI, osobna koncepcyjnie od
      renderer windows i od ECS-owego `CollectionComponent`). Osobna, dedykowana część UI (panel
      lub zakładka) do: (1) wyboru/wyszukania materiału z kolekcji, (2) generacji z niego
      superkomórki (rozmiar, ewentualnie orientacja) i defektów, (3) wyliczenia/przypisania
      podstawowych właściwości materiału. **Pokrywa się częściowo z już śledzonymi zadaniami
      wyżej w Backlogu — to ma je spiąć w jeden spójny workflow/UI, nie zastępować:**
      "Moduł generacji komórek startowych" (bulk/primitive → supercell), "Structure Authoring
      wizard" (supercell builder, cell definition), "Moduł generacji testów zbieżności i
      defektów" (masowa generacja wariantów), "Energetyka i DFT" (formation energy i inne
      właściwości liczone z obliczeń). Design (skąd dane materiałów — plik/pymatgen/ręczny
      wpis, schema biblioteki, czy to nowy Domain layer koncept) do doprecyzowania przy starcie
      tego zadania, nie tutaj.
- [ ] **Edytor własnych etykiet (LaTeX) — zażądane 2026-08-22, niski priorytet, "kiedyś":**
      użytkownik definiuje dowolny tekstowy label (nie tylko auto bond-length/angle z T09) i może użyć
      składni LaTeX do zapisu matematycznego (np. wzory, indeksy, symbole greckie) — przy założeniu że
      LaTeX jest dostępny globalnie na komputerze (`latex`/`pdflatex` na PATH, nie vendorować TeX-a).
      Prawdopodobny kształt: label → skompilowany przez zewnętrzny `pdflatex`/`dvisvgm` do SVG/PNG →
      wrzucony jako teksturowany quad (osobna ścieżka od MSDF-owych auto-etykiet z T09, bo MSDF to
      font-atlas ASCII, nie renderer TeX-a) — **niezweryfikowane, wymaga rozpoznania na starcie tego
      zadania**: czy dostępny jest `Core/Platform/ProcessRunner` do wywołania `pdflatex` jako
      subprocess (ten sam wzorzec co Python bridge), koszt/cache kompilacji per-label (nie kompilować
      od nowa co klatkę), fallback gdy LaTeX niedostępny (graceful `StructuredError`, appka nie ma
      wymagać LaTeX-a do działania). Reużyje ECS `LabelComponent`/`TransformComponent` z T09 (zob.
      wyżej) jako ten sam mechanizm co pinned measurement labels, inny tylko backend renderowania
      glifów.

---

## Panele UI — docelowa lista (z `old-ds-functionality.md` §13, do rozbicia na taski powyżej)

| Panel             | Opis                                                              | Task          |
| ----------------- | ----------------------------------------------------------------- | ------------- |
| Scene Outliner    | Drzewo sceny: atomy, kolekcje, puste, grupy                       | T08           |
| Properties        | Właściwości zaznaczonego obiektu (pozycja, pierwiastek, flagi SD) | T08           |
| Actions           | Szybkie akcje sceny                                               | T08           |
| Appearance        | Kolory, materiały atomów i wiązań, globalne opcje wyglądu         | T12           |
| Viewport Settings | Tło, siatka, oświetlenie, tryb projekcji                          | T12           |
| Volumetrics       | Sterowanie danymi wolumetrycznymi i izopowierzchniami             | T14           |
| Element Catalog   | Edycja globalnych danych wizualnych pierwiastków                  | T07/T12       |
| Periodic Table    | Picker pierwiastków                                               | T07/T12       |
| Log / Errors      | Panel logów z poziomami i metadanymi źródła                       | ✅ T04         |
| Stats             | Liczba atomów, wiązań, wydajność                                  | T08/T09       |
| Viewport Info     | Informacje diagnostyczne viewportu                                | T09           |
| Shortcuts         | Podgląd wszystkich skrótów klawiszowych                           | T12           |
| Settings          | UI, styl, skróty klawiszowe konfiguralne                          | ⚠️ PARTIAL T12 |
| Render Preview    | Podgląd renderowanego obrazu                                      | T15           |

---

## Skróty klawiszowe — docelowa lista (z `old-ds-functionality.md` §14)

| Skrót                    | Akcja                      | Task |
| ------------------------ | -------------------------- | ---- |
| Ctrl+O                   | Open POSCAR                | T07  |
| Ctrl+S                   | Export POSCAR              | T07  |
| Ctrl+Z / Ctrl+Y          | Undo / Redo                | T08  |
| Ctrl+A                   | Select All                 | T08  |
| Ctrl+C / Ctrl+V / Ctrl+D | Copy / Paste / Duplicate   | T08  |
| G / R / S                | Translate / Rotate / Scale | T08  |
| G+X/Y/Z                  | Translate wzdłuż osi       | T08  |
| B                        | Box Select                 | T08  |
| H / Alt+H                | Hide / Unhide All          | T08  |
| Delete                   | Usuń zaznaczenie           | T08  |
| Shift+A                  | Dodaj atom (menu)          | T08  |
| F12                      | Render Image               | T15  |
| N                        | Toggle boczny panel        | T08  |
| Tab                      | Wyjście z trybu transform  | T08  |
| PgUp/PgDn/Home/End       | Obrót o 90° wokół osi      | T07  |

---

## Infrastruktura i narzędzia (aktualny stan, zweryfikowane w kodzie)

- **Wielowątkowość**: `Core/JobSystem` (BS\_thread\_pool) — działa, oddzielony od `ScientificRuntime`
- **Profiling**: Tracy (CPU + GPU zones) — działa
- **Logowanie**: `Core/Logging`, per-level z metadanymi — działa
- **Build system**: Premake5, wildcard include — działa (Debug/Release/Dist)
- **Skrypty setup**: PowerShell/`.sh` + uv (Python) — `Tooling.bat`/`Tooling.sh` + `setup.py`
- **Konfiguracja**: YAML (yaml-cpp) przez `App/Managers/ConfigManager` + `App/Serialization/YamlConfigSerializer`
  (uwaga: serializer ~1700 linii, zob. T06.5 P2)
- **Knowledge graph**: `graphify-out/` — używać do nawigacji (`graphify query/path/explain`),
  nie edytować ręcznie; może być nieaktualny względem HEAD (sprawdzać commit w `GRAPH_REPORT.md`)
- **`punktukas-tools`** (2026-08-13): prywatne narzędzie (Bitbucket, autor Lukas Razinkovas, brak
  licencji) — **nie vendorować, nie commitować**. Traktowane jako opcjonalna, globalnie
  zainstalowana zależność Python; przyszłe mosty (`ScientificRuntime`) mają robić
  `import puntukas` w `try/except ImportError` → `StructuredError` z komunikatem instalacyjnym,
  analogicznie do detekcji narzędzi w `scripts/python/common/tooling.py`. Pokrywa POSCAR/POTCAR/
  KPOINTS/WAVECAR/CHGCAR/symmetry po stronie Python — nasza appka odpowiada za UI (wolumetryka,
  gizmos, selection), nie za re-implementację tego parsowania w C++.

---

*Zunifikowano z `TODO.md` + `old-ds-functionality.md` na podstawie przeglądu kodu repo
`Defect-Studio-mgr`, branch `main`, commit `e85be0c` (2026-08-13). Weryfikacja testów/builda
pominięta na prośbę użytkownika — statusy `[x]` oznaczone jako "zweryfikowane w kodzie" opierają
się wyłącznie na przeglądzie źródeł (grep + lektura), nie na uruchomieniu `DefectStudioTests`.*
