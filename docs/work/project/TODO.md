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
- [ ] **Wiązania nie tworzą się po imporcie przez `puntukas`** (ProjectTreePanel "Open Defect" /
      `OpenDefectJob`). Root cause zweryfikowany: `puntukas.atoms.base.AtomsBase.get_positions()`/
      `get_cell()` deklarują `units="angstrom"` i wołają `U.from_angstrom(...)`, ale
      `puntukas.vasp.poscar.poscar.py:90-92` (`atoms_from_data`) przechowuje pozycje/cell już
      przekonwertowane do jednostek atomowych — `from_angstrom` na danych które nie są w
      angstromach to no-op. Efekt: odległości ×1.8897 za duże → `BondGenerator` nie łapie cutoffu
      → zero wiązań. Fix: workaround lokalny w `scripts/python/examples/puntukas_structure_load.py`
      (nie upstream w `punktukas-tools`).
- [ ] Quick PNG export z viewportu (`glReadPixels` + `stbi_write_png`, `stb_image_write.h` już
      zvendorowany — zero nowej zależności).
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
- [ ] Multi-import jako Kolekcje: kolejny POSCAR dołącza atomy jako nowa Kolekcja w istniejącej
      scenie, auto-konwersja układu współrzędnych (Direct ↔ Cartesian), stem ścieżki → nazwa kolekcji
      (zależne od modelu Kolekcji z T08)
- [ ] Multi-structure support (wiele POSCAR w jednym projekcie) — częściowo pokryte przez
      `StructureRegistry` (istnieje i jest wpięte w startup rendererowy, patrz T06.5), ale bez
      pełnego workflow projektu/kolekcji
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
- [ ] Projekt jako katalog z `manifest.yaml`
- [ ] Create / Open / Recent project workflow
- [ ] `ProjectMetadata`: uuid, name, created\_at, last\_modified, format\_version (od pierwszego dnia)
- [ ] Auto-save z konfigurowalnym interwałem
- [ ] Tracking dodanych plików (lazy resource loading)
- [ ] `PathResolver` – normalizacja ścieżek project-relative vs external — **brak w kodzie**
- [ ] Walidacja brakujących plików przy otwieraniu + relink / rebuild flow — **na mounted drive
      to też pokrywa przypadek "mount padł"** (brak sieci/VPN), nie tylko przeniesiony plik
- [ ] Zapis ukrytych atomów, widoków, dodanych bondów, overrides kolorów, stanu kolekcji,
      pozycji kamery (`old-ds-functionality.md` §10)
- [x] **Ponytail placeholder (nie prawdziwe ustawienie projektu):** domyślny root
      `ProjectTreePanel` na sztywno w `EditorLayer.cpp` (`O:\hBN\V2CBCN`, sprawdzany
      `FileSystem::Exists` przy starcie) — `YamlConfigSerializer.cpp` (~1700 linii, zduplikowane
      ścieżki emit/parse) nie jest bezpieczny do rozszerzenia jednym polem bez wcześniejszego
      refactoru. Przenieść do realnej persystencji, gdy powstanie `manifest.yaml`/ustawienia
      projektu wyżej w tej sekcji.
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
- [ ] Zmień typ atomu (zaznaczenia), usuń atom (`Delete`), duplikuj (`Ctrl+D`), kopiuj/wklej (`Ctrl+C/V`)
- [ ] Ukryj (`H`) / odkryj wszystkie (`Alt+H`) — toggle `VisibilityComponent` per kolekcja
<!-- - [ ] Extract to New Collection — przeniesienie zaznaczonych atomów do nowej Kolekcji -->
- [ ] `SceneOutliner` panel – lista struktur/kolekcji z entt view, toggle widoczności, F2 rename
- [ ] `ObjectProperties` panel – właściwości wybranego atomu/struktury (translate przez pola numeryczne,
      uniform XYZ snap)
- [ ] Auto-bond generation z modelem trwałym: global cutoff + **per-para pierwiastków z override**
      (dziś jest tylko global cutoff na poziomie renderera — per-para override z
      `old-ds-functionality.md` §4.2 brak), spatial hash grid, bez wiązań między Kolekcjami
- [ ] Manual bond add/remove z persystencją w projekcie; ukryj pojedyncze wiązanie bez usuwania
      z modelu, auto-ukrycie przy usunięciu atomu; etykiety długości wiązań w 3D (§4.4)
- [ ] ImGuizmo transform (G/R/S + axis/plane constraints: `G`+`X/Y/Z`, `Shift+X/Y/Z`) dla atomów
- [ ] Collections system: widoczność (eye), blokada selekcji (lock), kolor etykiety, rename,
      export aktywnej kolekcji do POSCAR (zależne od T07 multi-import)
- [ ] **Groups** (niezależne od Collections, atom może być w jednej grupie i dowolnej kolekcji):
      create from selection, add/remove selection, select active group, delete group
      (`old-ds-functionality.md` §7 — nie było w oryginalnym `TODO.md`)
- [ ] **Scene Objects – Empty**: punkt pomocniczy z lokalnym układem osi, transformowalny (G/R),
      "align active empty Z to selected atoms", align to world/camera (§8.1)
- [ ] **Scene Objects – Origin i Light**: jednoinstancyjne obiekty specjalne, transformowalny Light (§8.2)
- [ ] **3D Cursor**: ustawiany z menu kontekstowego na płaszczyźnie siatki, pivot do transformacji
      lub punkt wstawienia atomu (§9)
- [ ] Blender-like Shift+A add menu (menu kontekstowe też), Delete, Ctrl+D duplicate
<!-- - [ ] N side-panel (toggle + strip) -->
- [ ] **Pomiary**: odległość między ostatnimi 2 zaznaczonymi atomami, kąt między ostatnimi 3,
      centrum masy zaznaczenia — etykiety w viewporcie (§5; było już w T09 checkliście oryginału,
      przeniesione tu bo logicznie należy do selekcji/sceny)
- [ ] Drag & drop pliku POSCAR/CONTCAR/CHG z eksploratora na viewport
- [ ] CWD fix: ustaw CWD na katalog exe przy starcie (`GetModuleFileNameA`)
- [ ] Touchpad support

**Biblioteki:** entt

---

## T09 – Advanced Renderer (`task/09-advanced-render`)

> Po działającym T08. Poprawiamy jakość renderowania.

- [x] **Compute shader dla bond transform** — **przeniesione z `[ ]` na `[x]`: zweryfikowane,
      już zaimplementowane i dispatchowane w T06**, nie tylko zaplanowane
- [ ] MSDF (Multi-channel SDF) dla 3D labels zamiast billboard quads
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
- [ ] **OpenGL 4.3 Compute Shader – Marching Cubes** (SSBO density field → dispatch → mesh)
- [ ] SSBO z probability density |ψ|²
- [ ] Request/commit model dla compute dispatch (render thread owner, nie bezpośrednia mutacja live-state)
- [ ] Backend abstraction: Compute Shader / CPU fallback (debug/fallback only)
- [ ] Single-iso i dual-iso rendering (positive/negative lobes, VESTA-like)
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
- [ ] Defect thermodynamics
- [ ] Python scripting panel (REPL w UI, hot-reload przez file watcher np. `efsw`,
      debugger VSCode/debugpy — `ds` module z submodułami `ds.scene`/`ds.commands`/`ds.events`/`ds.app`;
      `T05` daje już fundament embeddingu przez nanobind)
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
