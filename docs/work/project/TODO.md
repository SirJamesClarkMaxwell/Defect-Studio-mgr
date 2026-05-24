# DefectStudio – TODO

> **Cel:** VESTA-clone, lepiej napisany. Nowy program pisany od zera.
> Architektura z `sjcmdev` (repo B), funkcjonalność i UI z `SirJamesClarkMaxwell` (repo A).
> Fokus: działający produkt jak najszybciej, bez przedwczesnych abstrakcji — ale z solidnym fundamentem.

---

## Zasady pracy

- Jeden branch per task: `task/NN-short-name`
- Merge do main tylko po pełnym Debug + Release build
- Pliki `.cpp` max ~500 linii — dzielić na moduły
- Brak wyjątków w ścieżkach renderowania — granica udokumentowana
- AI-generated code podlega aktywnemu review przed mergem

---

## Aktualny status i priorytety

```
✅ DONE    : T01 – T03  (platform, docs, cross-platform)
✅ DONE    : T04 partial (Application, LayerStack, EventSystem, EventBus, Window)
🔨 CURRENT : T04 finish + T05 vendor deps
⏭ NEXT    : T06 → T07 → T08 → T09 → T10/T11 → T12 → T13 → T14 → T15
```

**Zasada do T15:** działające minimum. Refactor/polish jako osobne taski po tym jak coś działa end-to-end.

---

## T01 – Platform Setup (`task/01-platform-setup`)

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
- [ ] **Vendor: `glm`** – dodać do `Vendor/`, premake target `glm`
- [ ] **Vendor: `stb_image` + `stb_image_write`** – header-only, dodać do `Vendor/stb/`
- [ ] **Vendor: `nativefiledialog-extended`** – `https://github.com/sjcmdev/nativefiledialog-extended.git`, premake target `nfd`
- [ ] **Vendor: `ImPlot`** – dodać do `Vendor/implot/`, premake target, podlinkować z ImGui
- [ ] **Vendor: `entt`** – header-only ECS, dodać do `Vendor/entt/`

**Biblioteki:** Premake5, spdlog, yaml-cpp, Tracy

---

## T02 – Dokumentacja i testy (`task/02-docs-tests`)

- [x] mdBook: pełna struktura `SUMMARY.md`
- [x] Strony architekturalne: Core, DataModel, IO, Renderer, Layers, UI
- [x] Workflow deweloperski: build, run, debug, konwencje branchy
- [x] `scripts/ci_check.py` finalizacja
- [ ] Testy regresyjne migracji konfiguracji (legacy → YAML)

---

## T03 – Cross-Platform (`task/03-crossplatform`)

- [x] Linux build: Premake5 gmake2, g++ / clang
- [x] Weryfikacja Python tooling
- [x] Parytet `Tooling.bat` vs `Tooling.sh`
- [x] Windows local build matrix: msvc x Debug/Release/Dist

---

## T04 – Core: Application, LayerStack, EventSystem (`task/04-core`)

- [x] `Application` lifecycle (`Create`/`Run`/`Shutdown`), deterministyczna pętla
- [x] `Window` wrapper, `src/Core/Platform/` split
- [x] `DS_ASSERT` w `Core/Assert.hpp`
- [x] Logging z `Core::LogLevel` → spdlog
- [x] `Ref<>` / `WeakRef<>` / `Unique<>` + `CreateRef` / `CreateUnique`
- [x] `EventQueue` jako `std::variant<...>` (memory locality, compile-time-closed event types)
- [x] `Application` singleton – main loop, `OnUpdate` / `OnEvent` / `OnRender`
- [x] `LayerStack` – push/pop/overlay
- [x] `CoreLayer`, `ImGuiLayer`, `EditorLayer` – hierarchia layerów
- [x] `EventSystem` – keyboard, mouse, window events
- [x] `EventBus` (pub/sub) dla komunikacji między panelami
- [x] `ConfigManager` – właściciel plików konfiguracyjnych (`default.yaml`, `ui_settings.yaml`)
- [x] Persist stylu UI (Hazel-like preset) + zapis ustawień użytkownika
- [x] `JobSystem` – thread pool (BS\_thread\_pool pod spodem), kolejki priorytetowe, task cancellation
- [x] `ProgressTracker` – rejestracja postępu dla JobSystem (okno z progress barami)
- [x] `DebugLayer` – szkielet: tylko Debug + opcjonalny internal Release, read-mostly diagnostics
<!-- - [x] Structured Error model – kategoria, kod, kontekst techniczny, wiadomość dla użytkownika -->
- [x] Cooperative cancellation tokens w JobSystem
- [x] `ASSERT_MAIN_THREAD` guard + reguła: tylko main thread commituje stan widoczny w projekcie
- [x] Log panel + job/task monitor w normalnym UI

**Biblioteki:** yaml-cpp, spdlog, BS\_thread\_pool

---

## T05 – Python Bridge (`task/05-python-bridge`)

> Uzależniony od stabilnego T04. Implementować po T06 jeśli renderer blokuje.

- [ ] `PythonInterpreter` RAII wrapper na `py::scoped_interpreter`
- [ ] `ScriptRunner` – wewnętrzne narzędzie deweloperskie (nie główny workflow użytkownika)
- [ ] `PymatgenBridge` – `Structure.from_file()`, superkomórki, symetria
- [ ] `ASEBridge` – Atoms I/O, konwersja formatów
- [ ] GIL management w JobSystem (`gil_scoped_acquire` / `release`)
- [ ] Python venv management – uv + `.venv` z pakietami naukowymi
- [ ] Error propagation: Python exceptions → structured errors → UI policy
- [ ] Testy interoperacyjności: pymatgen POSCAR roundtrip przez bridge

**Biblioteki:** nanobind, pymatgen, ASE, numpy

---

## T06 – OpenGL Renderer (`task/06-renderer`)

> **MVP first:** działający renderer z atomami i bondami.
> Compute shaders jako first-class od początku — nie migracja później.

- [ ] GL context, GLAD loader, debug callback
- [ ] `Renderer` – minimalna abstrakcja backend (forward rendering, jeden pass)
- [ ] `ShaderLibrary` – ładowanie, kompilacja, reload on-the-fly
- [ ] FBO (FrameBuffer Object) dla offscreen render i viewport texture
- [ ] Instanced atom rendering (sphere impostor lub prosta sfera-mesh, konfigurowalny radius)
- [ ] Instanced bond rendering (cylinder 8-stronny, dwie półsfery)
- [ ] Orbit camera (Blender-like: LMB orbit, MMB pan, scroll zoom) + persist
- [ ] **OpenGL 4.3 Compute Shader infrastruktura** (SSBO, dispatch, sync barrier) – fundament dla bonds i volumetrics
- [ ] Tracy GPU profiling integration
- [ ] Grid / unit cell box rendering (linie krawędzi komórki)
- [ ] View gizmo overlay (XYZ osie, kliknięcie ustawia kamerę)
- [ ] Exception-free zone – granica opisana statycznym komentarzem / assertem

**Biblioteki:** GLFW, OpenGL 4.3, GLM, stb\_image, Tracy

---

## T07 – Data Model i POSCAR Import (`task/07-data-model`)

> Port z repo A (`SirJamesClarkMaxwell`). Czyste klasy, zero zależności UI.

- [ ] `Structure` – wektory sieci, atomy, UUID jako stabilna tożsamość
- [ ] `Atom` – pozycja frakcyjna, element, właściwości (label, formalny ładunek, magnetyzacja, occupancy)
- [ ] Derived Cartesian position utilities (przeliczanie Direct ↔ Cartesian)
- [ ] `Bond` – para atomów, długość, typ (auto/manual)
- [ ] `ElementCatalog` – globalny katalog per-element (kolor, promień, masa, Z)
- [ ] POSCAR/CONTCAR parser (VASP5/6, Selective Dynamics, Direct/Cartesian)
- [ ] POSCAR serializer – precyzja i format bez noisy diff; `canonicalizeDirectTranslation = false` domyślnie
- [ ] Sortowanie species w POSCAR malejąco po liczbie atomów
- [ ] `StructureSerializer` – serializacja do YAML na potrzeby projektu
- [ ] Multi-structure support (wiele POSCAR w jednym projekcie, kolekcje)
- [ ] Parser unit tests: POSCAR roundtrip, edge cases (Selective Dynamics, puste struktury)
- [ ] CIF parser – import, konwersja do modelu wewnętrznego (pymatgen backend przez T05)
- [ ] XYZ parser (ASE-compatible)

**Biblioteki:** yaml-cpp

---

## T08 – Scene, ECS, Atom Rendering, Selection (`task/08-scene`)

> ECS (entt) jako fundament sceny od początku — nie refactor później.
> MVP: załaduj POSCAR → Zobacz atomy → kliknij atom.

- [ ] `SceneRegistry` – entt registry jako właściciel wszystkich obiektów sceny
- [ ] Komponenty ECS: `TransformComponent`, `AtomComponent`, `BondComponent`, `VisibilityComponent`, `SelectionComponent`, `CollectionComponent`
- [ ] `SceneSystem` – update loop iterujący po komponentach
- [ ] Załaduj POSCAR → utwórz encje w registry → wyświetl atomy (instanced render z T06)
- [ ] Click-select atomu (ray casting, highlight przez `SelectionComponent`)
- [ ] Box-select (B), circle-select (C), RMB context menu
- [ ] Multi-select (Shift+click)
- [ ] Undo/Redo stack (Ctrl+Z / Ctrl+Y) dla akcji sceny — snapshot-based na start
- [ ] Dodaj atom przez wpisanie współrzędnych (frakcyjne lub kartezjańskie)
- [ ] Zmień typ atomu, usuń atom, duplikuj (operacje na encjach ECS)
- [ ] `SceneOutliner` panel – lista struktur i kolekcji z entt view, toggle widoczności
- [ ] `ObjectProperties` panel – właściwości wybranego atomu/struktury
- [ ] Auto-bond generation (global cutoff + per-element-pair, spatial hash grid)
- [ ] Manual bond add/remove (persystencja w projekcie)
- [ ] H / Alt+H hide/unhide (toggle `VisibilityComponent` per kolekcja)
- [ ] ImGuizmo transform (G/R/S + axis constraints) dla atomów
- [ ] Collections system z toggleami widoczności i selectability
- [ ] Blender-like Shift+A add menu, Delete, Ctrl+D duplicate
- [ ] N side-panel (toggle + strip)
- [ ] Distance/angle measurement tools
- [ ] Drag & drop pliku POSCAR/CONTCAR/CHG z eksploratora na viewport
- [ ] CWD fix: ustaw CWD na katalog exe przy starcie (`GetModuleFileNameA`)
- [ ] Touchpad support

**Biblioteki:** entt

---

## T09 – Advanced Renderer (`task/09-advanced-render`)

> Po działającym T08. Poprawiamy jakość renderowania.

- [ ] **Compute shader dla bond transform** – pre-compute matrix (scale/rotate/translate) na GPU
- [ ] MSDF (Multi-channel SDF) dla 3D labels zamiast billboard quads
- [ ] **Multi-viewport** – wiele viewportów z niezależnymi ustawieniami kamery i renderowania
- [ ] Quick image export (PNG/JPG z aktualnego viewportu)
- [ ] PBR lighting (opcjonalny, dla publication-quality renders)
- [ ] SVG export pipeline

---

## T10 – Project System (`task/10-project`)

> MVP: utwórz / otwórz / zapisz projekt.

- [ ] Projekt jako katalog z `manifest.yaml`
- [ ] Create / Open / Recent project workflow
- [ ] `ProjectMetadata`: uuid, name, created\_at, last\_modified, format\_version
- [ ] `format_version` w plikach projektu od pierwszego dnia
- [ ] Auto-save z konfigurowalnym interwałem
- [ ] Tracking dodanych plików (lazy resource loading)
- [ ] `PathResolver` – normalizacja ścieżek project-relative vs external
- [ ] Walidacja brakujących plików przy otwieraniu + relink / rebuild flow
- [ ] Zapis ukrytych atomów, widoków, dodanych bondów
- [ ] Export kolekcji do POSCAR
- [ ] Tags dla defektów i kolekcji
- [ ] Application startup project (ostatni otwarty)
- [ ] Lekki migration pipeline dla ewolucji formatu pliku projektu
- [ ] Canonical save vs recovery/snapshot save split

---

## T11 – Domain Runtime Model (`task/11-domain-runtime`)

> Uruchamiać razem z T10.

- [ ] `ProjectWorkspace` – runtime container dla otwartego projektu
- [ ] `StructureRegistry` – runtime owner wszystkich załadowanych struktur
- [ ] `DefectConcept` – defekt jako pierwszorzędna encja naukowa (pozycja, typ, tagi, stany ładunku)
- [ ] `DefectConfiguration` – jedna konfiguracja/aranżacja defektu
- [ ] `CalculationRecord` – jeden input structure + opcjonalny output structure
- [ ] Filter view po charge state i spin channel
- [ ] Project-scoped query/index helpers

---

## T12 – Config, Settings, UX Polish (`task/12-ux`)

- [ ] Settings window (dwie kolumny: label – wartość, wyrównane)
- [ ] Appearance panel vs Viewport Settings vs Render Image – czysta taksonomia
- [ ] Per-project element appearance overrides (osobny plik od scene\_state)
- [ ] Import/export/reset workflow dla overrides
- [ ] Persist ImGui dock/panel layout
- [ ] Konfigurowalny skróty klawiaturowe i zachowanie myszy
- [ ] Shortcut conflict detection
- [ ] F2 rename w Scene Outliner
- [ ] Viewport resolution tuning (redukcja GPU load)
- [ ] Range selection z Shift w kolekcjach
- [ ] Hierarchia ustawień: Application / Project / Scene-Workspace
- [ ] Save current layout jako user default
- [ ] Keyboard shortcut reference panel

---

## T13 – Space Group Analyzer (`task/13-spacegroup`)

> Analiza symetrii dla załadowanej struktury. Backend: spglib przez Python bridge (T05).

- [ ] Space group detection z konfigurowalną tolerancją (spglib backend)
- [ ] Space group browser – przeglądanie listy grup 1–230
- [ ] Automatyczna generacja symetrycznie równoważnych pozycji
- [ ] Wyświetlenie operatorów symetrii dla wybranej grupy przestrzennej
- [ ] Toggle: praca bez symetrii / z wymuszoną symetrią
- [ ] Sprawdzenie kompatybilności struktury z daną grupą przestrzenną
- [ ] Standaryzacja komórki, redukcja do komórki prymitywnej, ekspansja do konwencjonalnej
- [ ] Wyświetlenie pozycji Wyckoffa, krotności i symetrii miejsca dla każdego atomu

**Biblioteki:** spglib (przez Python bridge), pymatgen (`SpacegroupAnalyzer`)

---

## T14 – Volumetrics: CHG/PARCHG/CHGCAR (`task/14-volumetrics`)

> Compute shader jako jedyna ścieżka isosurface. Bez CPU Marching Cubes jako main path.

- [ ] Scalar field data model (oddzielony od atom/bond data)
- [ ] CHG/CHGCAR/PARCHG parser (format VASP, multi-block) – port z repo A
- [ ] Wewnętrzna binarna reprezentacja/cache dla szybszego reload
- [ ] Lazy loading bloków (background JobSystem)
- [ ] Block statistics: min, max, mean, memory footprint
- [ ] **OpenGL 4.3 Compute Shader – Marching Cubes** (SSBO z density field → dispatch → index/vertex buffer z mesh)
- [ ] SSBO z probability density |ψ|²
- [ ] Request/commit model dla compute dispatch (render thread owner, nie bezpośrednia mutacja live-state)
- [ ] Backend abstraction: ten sam interfejs dla Compute Shader / CPU fallback (debug/fallback only)
- [ ] Single-iso rendering (VESTA-like)
- [ ] Dual-iso rendering (positive/negative lobes, konfigurowalny kolor i opacity)
- [ ] Per-surface controls: iso value, kolor, opacity, widoczność
- [ ] Async CPU-side preprocessing i immutable compute input preparation
- [ ] VESTA-like workflow panel (side-by-side surfaces, mniej tekstu)
- [ ] Reduced preview resolution (performance guardrail)
- [ ] Polyhedra visualization (coordination polyhedra wokół wybranych atomów)

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
- [ ] Remote SSH / SFTP browser
- [ ] VASP OUTCAR/WAVECAR integration
- [ ] Defect thermodynamics
- [ ] Python scripting panel

---
