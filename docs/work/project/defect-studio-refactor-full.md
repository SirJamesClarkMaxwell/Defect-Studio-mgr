# Defect Studio – Pełny plan refaktoryzacji

> **Zasady dla Codex/Copilot:**
> - Nie opisuj w czacie co robisz. Nie wyjaśniaj zmian. Wykonuj je bezpośrednio.
> - Każda SESJA to osobny commit. Nie łącz sesji.
> - Przed każdą zmianą przeczytaj wskazane pliki. Stosuj styl kodu z projektu.
> - Konwencje: `PascalCase` typy i publiczne metody, `camelCase` prywatne metody,
>   `m_PascalCase` pola prywatne, `Ref<T>`/`Unique<T>`/`WeakRef<T>` zamiast raw smart pointerów,
>   `[[nodiscard]]` na każdej funkcji zwracającej wynik, `Result<T>` jako model błędów.
> - Anonimowe namespace wewnątrz namespace projektu są dopuszczalne dla file-local helperów.
>   Nie używaj ich dla stanu wymagającego stabilnej zewnętrznej nazwy.
> - Pliki `.cpp` max ~500 linii – dziel na mniejsze jeśli przekraczają.

---

## STATUS SESJI (nie implementuj – już gotowe)

Poniższe sesje zostały zaimplementowane i NIE wymagają działania:

- **Sesja 1** – DemoLayer split (DemoCommands, DemoBackendRuntime, DemoNotifications)
- **Sesja 3** – ICommand::Redo virtual method
- **Sesja 4** – Notifier: Publish → Queue
- **Sesja 6** – JobEvents.hpp przeniesione do Core/JobSystem
- **Sesja 7** – CapabilityService::Require → Result<void>
- **Sesja 9** – ImGuiLayer obsługuje tosty przez EventBus
- **Sesja 10** – CommandRegistry::notifyObservers (rename z Notify)
- **Sesja 12** – CommandRegistry: raw ptr → WeakRef
- **Sesja 13** – CommandFactory używa Unique<ICommand>
- **Sesja 14** – Rejestracja komend przez std::bind_front
- **Sesja 15** – KeyBinding.hpp rozbity na osobne pliki
- **Sesja 16** – SystemCommands + registerSystemCommands w CoreLayer
- **Sesja 17** – CapabilityService ukrywa CapabilityRegistry
- **Sesja A** – CommandOutcome::FromCommand factory
- **Sesja B** – CommandService
- **Sesja E** – CancelGroup fix w UndoStack

---

# CZĘŚĆ I – RENDERER

> Etapy R0→R3. Implementuj w tej kolejności. Każdy etap to osobna seria commitów.

---

## ETAP R0 – Quick wins

### R0.1 – Usuń ElementDataTable.hpp

**Przeczytaj przed rozpoczęciem:** `src/Renderer/ElementDataTable.hpp`

Uruchom: `grep -rn "ElementDataTable" src/ tests/`

Jeśli grep zwraca trafienia wyłącznie wewnątrz `ElementDataTable.hpp` – usuń plik:
```
git rm src/Renderer/ElementDataTable.hpp
```
Nie twórz zamiennika. Klasa zostanie właściwie zaimplementowana w T07.

---

### R0.2 – Usuń drawMainPanel i MainLoopState z Application

**Przeczytaj przed rozpoczęciem:** `src/App/Application.hpp`, `src/App/Application.cpp`

W `src/App/Application.hpp`:
- Usuń deklarację: `void drawMainPanel(bool &showDemoWindow, ImVec4 &clearColor, float frameRate);`

W `src/App/Application.cpp`:
- Usuń struct `MainLoopState` (linie zawierające `showDemoWindow`, `showSettingsWindow`, `clearColor`)
- Usuń zmienną `MainLoopState state;` w mainLoop (lub analogicznym miejscu)
- Usuń zakomentowane wywołanie `// drawMainPanel(...)`
- Usuń całą implementację metody `Application::drawMainPanel(...)`

---

### R0.3 – Zakomentuj ProcessQueuedEvents z opisem

**Przeczytaj przed rozpoczęciem:** `src/App/Application.hpp`, `src/App/Application.cpp`

W `src/App/Application.hpp`, znajdź:
```cpp
static void ProcessQueuedEvents();
```
Zastąp:
```cpp
// Extension point: external event pump for test harness or offline tools.
// Not currently called from production code. Do not remove.
// static void ProcessQueuedEvents();
```

W `src/App/Application.cpp`, znajdź implementację `Application::ProcessQueuedEvents()`.
Zakomentuj całą implementację i poprzedź:
```cpp
// Extension point – see declaration in Application.hpp.
// void Application::ProcessQueuedEvents()
// {
//     DS_ASSERT(s_Instance.has_value(), "Application not created");
//     s_Instance->get().processPendingEvents();
// }
```

---
----
### R0.4 – Fix: niekompletne FBO nie czyści zasobów

**Przeczytaj przed rozpoczęciem:** `src/Renderer/OpenGl/OpenGlFrameBuffer.cpp`

W metodzie `OpenGlFrameBuffer::Resize` (lub `Invalidate` – znajdź metodę która wywołuje `glCheckFramebufferStatus`).

Znajdź blok:
```cpp
if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    DS_LOG_ERROR("Renderer framebuffer is incomplete for size {}x{}", m_Width, m_Height);
glBindFramebuffer(GL_FRAMEBUFFER, 0);
```

Zastąp:
```cpp
if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
{
    DS_LOG_ERROR(
        "Renderer: framebuffer incomplete for size {}x{} — releasing resources",
        m_Width, m_Height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    release();
    return;
}
glBindFramebuffer(GL_FRAMEBUFFER, 0);
```

Upewnij się że metoda `release()` (lub odpowiednik cleanup) resetuje `m_Width` i `m_Height` do 0.
Jeśli nie ma metody `release()` – dodaj prywatną:
```cpp
void OpenGlFrameBuffer::release()
{
    if (m_FrameBuffer)  { glDeleteFramebuffers(1, &m_FrameBuffer);  m_FrameBuffer = 0; }
    if (m_ColorTexture) { glDeleteTextures(1, &m_ColorTexture);      m_ColorTexture = 0; }
    if (m_DepthStencil) { glDeleteRenderbuffers(1, &m_DepthStencil); m_DepthStencil = 0; }
    m_Width  = 0;
    m_Height = 0;
}
```

---

### R0.5 – Oznacz dispatchBondCompute jako extension point

**Przeczytaj przed rozpoczęciem:** `src/Renderer/OpenGl/OpenGlRendererBackend.hpp`, `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`

W `OpenGlRendererBackend.hpp`, przed deklaracją `dispatchBondCompute`, dodaj:
```cpp
// T09 extension point: GPU-side bond transform via compute shader.
// SSBO i shader są inicjalizowane, ale dispatch nie jest wywoływany.
// Aktywować gdy T09 wprowadzi automatyczną regenerację bondów przy przesuwaniu atomów.
```

W `OpenGlRendererBackend.cpp`, przed implementacją `dispatchBondCompute`, dodaj ten sam komentarz.

---

### R0.6 – Skopiuj shadery do install/app/assets przy buildzie

**Przeczytaj przed rozpoczęciem:** `premake5.lua`, `src/Renderer/RendererLayer.cpp`

#### R0.6a – Dodaj postbuild w premake5.lua

W `premake5.lua`, w projekcie głównej aplikacji (`DefectStudio`, nie testy), znajdź sekcję `postbuildcommands` (lub dodaj ją). Dodaj komendy kopiowania shaderów:

Dla Windows:
```lua
filter { "system:windows" }
    postbuildcommands {
        'if not exist "%{cfg.targetdir}\\shaders" mkdir "%{cfg.targetdir}\\shaders"',
        'xcopy /E /Y /I "src\\Renderer\\OpenGl\\Shaders\\*" "%{cfg.targetdir}\\shaders\\" >NUL'
    }
filter {}
```

Dla Linux:
```lua
filter { "system:linux" }
    postbuildcommands {
        'mkdir -p "%{cfg.targetdir}/shaders"',
        'cp -r src/Renderer/OpenGl/Shaders/. "%{cfg.targetdir}/shaders/"'
    }
filter {}
```

#### R0.6b – Zaktualizuj resolveShaderDirectory w RendererLayer.cpp

**Przeczytaj:** `src/Renderer/RendererLayer.cpp` – funkcja `resolveShaderDirectory` lub `BuildShaderDirectoryFromAssetsRoot`.

Znajdź blok który używa `parent_path().parent_path().parent_path()` do znalezienia katalogu `src/Renderer/OpenGl/Shaders`. Dodaj jako **pierwsze sprawdzenie** (przed heurystyką repo):

```cpp
// Najpierw szukaj shaderów obok binarki (deploy path)
const Path deployShaders = Path::FromResolved(
    m_StartupConfig.assetsDirectory.Native().parent_path() / "shaders");
if (FileSystem::Exists(deployShaders.Native()))
    return deployShaders;

// Fallback: ścieżka deweloperska w repozytorium
// TODO: usunąć gdy pipeline budowania zawsze kopiuje shadery do deploy dir
```

Heurystykę `parent_path().parent_path().parent_path()` zostaw jako fallback z komentarzem `// Dev fallback`.

---

## ETAP R1 – Rozbicie RendererLayer.hpp (god-header)

> **Ważne:** Nie zmieniaj logiki. Tylko przenoś typy do nowych nagłówków i aktualizuj includy.
> Po każdym podpunkcie projekt musi się kompilować.

### R1.1 – Utwórz src/Renderer/RendererTypes.hpp

Utwórz nowy plik `src/Renderer/RendererTypes.hpp`.

Przenieś do niego z `src/Renderer/RendererLayer.hpp` następujące typy (tylko deklaracje/definicje typów, bez klas z metodami):

- `struct RendererAtomData`
- `struct RendererColorGradient`
- `struct RendererBondData`
- `struct RendererCellEdge`
- `struct RendererStructureData`
- `struct RendererViewSnapshot`
- `struct RendererViewStateChange`

Nagłówek `RendererTypes.hpp` zaczyna się od:
```cpp
#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
```

Dodaj tylko te includes których struktury faktycznie potrzebują (sprawdź typy pól).

W `RendererLayer.hpp` zastąp definicje tych typów pojedynczym:
```cpp
#include "Renderer/RendererTypes.hpp"
```

---

### R1.2 – Utwórz src/Renderer/RendererSettings.hpp

Utwórz `src/Renderer/RendererSettings.hpp`.

Przenieś z `src/Renderer/RendererLayer.hpp`:

- `enum class CameraProjection`
- `struct RendererLightingSettings`
- `struct RendererGridSettings`
- `struct RendererToolbarWheelSettings`
- `struct RendererKeyboardShortcutSettings`
- `struct RendererViewportSettings`
- `struct RendererGlobalRenderSettings`

Nagłówek zaczyna się od:
```cpp
#pragma once

#include <cstdint>
#include <string>
// imgui key type – tylko jeśli RendererKeyboardShortcutSettings używa ImGuiKey
// (tymczasowo; zastąpione w R3.5)
#include <imgui.h>
```

W `RendererLayer.hpp` zastąp definicje:
```cpp
#include "Renderer/RendererSettings.hpp"
```

---

### R1.3 – Utwórz src/Renderer/RendererWindowState.hpp

Utwórz `src/Renderer/RendererWindowState.hpp`.

Przenieś z `src/Renderer/RendererLayer.hpp`:

- `struct RendererToolbarIconTexture`
- `struct RendererWindowState` (cała struktura z wszystkimi polami)

Nagłówek zaczyna się od:
```cpp
#pragma once

#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include <string>
#include <vector>
```

Dodaj tylko niezbędne includes (sprawdź typy pól `RendererWindowState`).

W `RendererLayer.hpp` zastąp definicje:
```cpp
#include "Renderer/RendererWindowState.hpp"
```

---

### R1.4 – Zaktualizuj includy w plikach korzystających z typów

**Przeczytaj:** `src/Renderer/OpenGl/OpenGlRendererBackend.hpp`, `src/Renderer/RendererPoscarLoader.hpp`, `src/Renderer/RendererStartupBootstrap.hpp`

W `src/Renderer/OpenGl/OpenGlRendererBackend.hpp`:
- Zastąp `#include "Renderer/RendererLayer.hpp"` na:
```cpp
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/RendererViewCamera.hpp"
```

W `src/Renderer/RendererPoscarLoader.hpp`:
- Zastąp `#include "Renderer/RendererLayer.hpp"` na:
```cpp
#include "Renderer/RendererTypes.hpp"
```

W `src/Renderer/RendererStartupBootstrap.hpp` – sprawdź których typów potrzebuje i dodaj minimal includes. Jeśli potrzebuje `RendererStartupConfig` – zostaw include `RendererLayer.hpp` (bo `RendererStartupConfig` zostaje tam).

---

### R1.5 – Rozdziel RendererStartupConfig od Ref<EventBus>

**Przeczytaj:** `src/Renderer/RendererLayer.hpp`, `src/Renderer/RendererLayer.cpp`, `src/App/Application.cpp`

W `src/Renderer/RendererLayer.hpp`:

W struct `RendererStartupConfig` usuń pole:
```cpp
Ref<EventBus> eventBus;
```

Dodaj publiczną metodę do klasy `RendererLayer`:
```cpp
void BindEventBus(Ref<EventBus> eventBus);
```

W `src/Renderer/RendererLayer.cpp`, dodaj implementację:
```cpp
void RendererLayer::BindEventBus(Ref<EventBus> eventBus)
{
    DS_ASSERT(!m_Attached, "BindEventBus must be called before OnAttach");
    m_EventBus = std::move(eventBus);
}
```

Upewnij się, że `m_EventBus` jest używane w `OnAttach()` tak samo jak wcześniej `m_StartupConfig.eventBus`.

W `src/App/Application.cpp`, w miejscu gdzie tworzony jest `RendererStartupConfig`:
- Usuń przypisanie `startupConfig.eventBus = m_EventBus;`
- Po stworzeniu `RendererLayer`, wywołaj: `rendererLayer->BindEventBus(m_EventBus);`

---

### R1.6 – Weryfikacja kompilacji po R1

Po wykonaniu R1.1–R1.5:

```bash
# Linux
./scripts/Tooling.sh generate && cd build && make -j$(nproc) 2>&1 | grep -E "error:|warning:" | head -40

# Windows: uruchom skrypt generate i zbuilduj w VS lub przez msbuild
```

Napraw każdy błąd kompilacji. Typowe problemy:
- Brakujące include w nowych headerach – dodaj konkretne `#include`
- Forward declaration zamiast pełnego include – zamień na `#include` jeśli typ jest używany nie przez wskaźnik

---

## ETAP R2 – Publiczne API, usunięcie friend, cache, Python bridge

### R2.1 – Przenieś logikę kamery z RendererPanel do RendererViewCamera

**Przeczytaj:** `src/Renderer/RendererViewCamera.hpp`, `src/Renderer/RendererViewCamera.cpp`, `src/Presentation/Panels/RendererPanel.cpp`

W `src/Presentation/Panels/RendererPanel.cpp` znajdź (w anonimowym namespace lub jako static):
- Funkcję `NormalizeAngleRadians`
- Funkcję `BuildCameraAxesFromEuler` (lub równoważną)
- Funkcję `CameraOrientationQuatFromEuler`
- Funkcję `CameraEulerFromOrientationQuat`

Przenieś każdą z tych funkcji do `src/Renderer/RendererViewCamera.cpp` jako `static` funkcje file-local lub jako publiczne metody klasy `RendererViewCamera` jeśli kamery je wywołują bezpośrednio.

Dodaj odpowiednie deklaracje w `RendererViewCamera.hpp` jeśli mają być publiczne.

W `RendererPanel.cpp` usuń zduplikowane definicje. W miejscach wywołań użyj wersji z `RendererViewCamera`.

---

### R2.2 – Zdefiniuj publiczne API RendererLayer, usuń friend class

**Przeczytaj:** `src/Renderer/RendererLayer.hpp`, `src/Renderer/RendererLayer.cpp`, `src/Presentation/Panels/RendererPanel.cpp`, `src/Presentation/Panels/RendererPanel.hpp`

#### Krok 1: Dodaj publiczne metody do RendererLayer

W `src/Renderer/RendererLayer.hpp`, w sekcji `public`, dodaj:

```cpp
// API dla RendererPanel – zastępuje dostęp przez friend
[[nodiscard]] std::vector<RendererWindowState> &GetWindows();
[[nodiscard]] const std::vector<RendererWindowState> &GetWindows() const;
[[nodiscard]] RendererGlobalRenderSettings &GetGlobalSettings();
[[nodiscard]] const RendererGlobalRenderSettings &GetGlobalSettings() const;
[[nodiscard]] bool IsAttached() const noexcept;
[[nodiscard]] const RendererToolbarIconTexture *GetToolbarIcon(const std::string &fileName) const;

// Proxy do backendu – RendererPanel nie zna konkretnego typu backendu
void RenderToFbo(
    const std::string &windowKey,
    const RendererStructureData &structure,
    const RendererWindowState &windowState,
    const RendererGlobalRenderSettings &settings);
void CollectProfilingData();
```

#### Krok 2: Implementuj w RendererLayer.cpp

Dodaj implementacje (proste delegacje do istniejących pól):
```cpp
std::vector<RendererWindowState> &RendererLayer::GetWindows()        { return m_Windows; }
const std::vector<RendererWindowState> &RendererLayer::GetWindows() const { return m_Windows; }
RendererGlobalRenderSettings &RendererLayer::GetGlobalSettings()      { return m_GlobalRenderSettings; }
const RendererGlobalRenderSettings &RendererLayer::GetGlobalSettings() const { return m_GlobalRenderSettings; }
bool RendererLayer::IsAttached() const noexcept { return m_Attached; }

void RendererLayer::CollectProfilingData()
{
    if (m_RendererBackend)
        m_RendererBackend->CollectProfilingData();
}
```

Dla `GetToolbarIcon` i `RenderToFbo` – deleguj do odpowiednich prywatnych pól/metod.

#### Krok 3: Usuń friend class

W `src/Renderer/RendererLayer.hpp` usuń linię:
```cpp
friend class RendererPanel;
```

#### Krok 4: Zaktualizuj RendererPanel.cpp

Zastąp każde bezpośrednie odwołanie do prywatnych pól `m_Layer.m_X` wywołaniem nowego publicznego API:
- `m_Layer.m_Windows` → `m_Layer.GetWindows()`
- `m_Layer.m_GlobalRenderSettings` → `m_Layer.GetGlobalSettings()`
- `m_Layer.m_Attached` → `m_Layer.IsAttached()`
- `m_Layer.m_RendererBackend->CollectProfilingData()` → `m_Layer.CollectProfilingData()`
- `m_Layer.m_ToolbarIcons` (lookup ikony) → `m_Layer.GetToolbarIcon(fileName)`

Dla `m_Layer.m_ShowPeriodicTableWindow` i `m_Layer.m_SelectedPeriodicElement`:
Dodaj getter/setter do `RendererLayer`:
```cpp
bool &GetShowPeriodicTableWindow();
std::string &GetSelectedPeriodicElement();
```

---

### R2.3 – Cache wierzchołków cell box

**Przeczytaj:** `src/Renderer/OpenGl/OpenGlRendererBackend.hpp`, `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`

#### Krok 1: Dodaj pola cache do OpenGlViewportResources

Znajdź struct `OpenGlViewportResources` (lub odpowiednik przechowujący per-viewport state). Dodaj:
```cpp
bool cellEdgesDirty = true;
std::vector<glm::vec3> cachedCellEdgeVertices;
```

#### Krok 2: Ustaw dirty przy zmianie struktury

W metodzie `RenderWindow` (lub miejscu które ustawia `atomsDirty = true` przy zmianie źródła), dodaj analogicznie:
```cpp
resources.cellEdgesDirty = true;
```

#### Krok 3: Zmień renderCellBox

Zmień sygnaturę:
```cpp
void renderCellBox(
    const RendererStructureData &structure,
    const RendererViewCamera &camera,
    OpenGlViewportResources &resources);   // dodaj resources
```

W ciele metody, przed budowaniem wektora `vertices`:
```cpp
if (resources.cellEdgesDirty)
{
    resources.cachedCellEdgeVertices.clear();
    resources.cachedCellEdgeVertices.reserve(structure.cellEdges.size() * 2);
    for (const RendererCellEdge &edge : structure.cellEdges)
    {
        resources.cachedCellEdgeVertices.push_back(edge.start);
        resources.cachedCellEdgeVertices.push_back(edge.finish);
    }
    resources.cellEdgesDirty = false;
}

if (resources.cachedCellEdgeVertices.empty())
    return;

const auto &vertices = resources.cachedCellEdgeVertices;
```

Usuń lokalny `std::vector<glm::vec3> vertices` i budowanie pętli – zastąpione przez cache.

Zaktualizuj wywołanie `renderCellBox` w metodzie wywoływanej co klatkę – przekaż `resources`.

---

### R2.4 – Zdekomponuj RendererPanel.cpp

**Przeczytaj:** `src/Presentation/Panels/RendererPanel.hpp`, `src/Presentation/Panels/RendererPanel.cpp`

Plik ma 1276 linii – podziel na maksimum 3 pliki, bez zmiany logiki:

#### Krok 1: Utwórz src/Presentation/Panels/RendererPanelToolbar.cpp

Przenieś do niego prywatne metody odpowiedzialne za rysowanie toolbara (szukaj metod w stylu `drawToolbar`, `drawRotationStep`, `drawZoomStep`, `drawIconButton` itp.). Zostają jako metody klasy `RendererPanel`, przenosisz tylko implementacje do nowego `.cpp`.

Pierwsze linie nowego pliku:
```cpp
#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
```

#### Krok 2: Utwórz src/Presentation/Panels/RendererPanelInput.cpp

Przenieś implementacje:
- `applyViewportKeyboardNavigation`
- `applyViewportInputNavigation`
- `beginViewInteraction`
- `commitViewInteraction`
- `cancelViewInteraction`
- `pushViewChange`
- `undoViewChange`
- `redoViewChange`
- `startCameraTransition`
- `updateCameraTransition`

Pierwsze linie:
```cpp
#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
```

#### Krok 3: Sprawdź rozmiary

Po podziale każdy plik `.cpp` powinien mieć ≤500 linii. Jeśli `RendererPanel.cpp` nadal przekracza 500 linii – wyodrębnij kolejną logiczną grupę.

---

### R2.5 – Lekki Python bridge dla ładowania struktur

**Przeczytaj:** `src/Renderer/RendererPoscarLoader.hpp`, `src/Renderer/RendererPoscarLoader.cpp`, `src/ScientificRuntime/PymatgenBridge.hpp`

#### Krok 1: Utwórz src/Renderer/RendererPythonLoader.hpp

```cpp
#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
    class PymatgenBridge;

    // Tymczasowy most do Pythona dla ładowania struktur krystalicznych.
    // T07: zastąpić przez pełny Domain model + IOLayer pipeline.
    [[nodiscard]] Result<RendererStructureData> LoadRendererStructureViaPython(
        const Path &filePath,
        std::string name,
        PymatgenBridge &bridge);
}
```

#### Krok 2: Utwórz src/Renderer/RendererPythonLoader.cpp

```cpp
#include "Core/dspch.hpp"
#include "Renderer/RendererPythonLoader.hpp"
#include "ScientificRuntime/PymatgenBridge.hpp"
#include "Core/Logging/Logger.hpp"

namespace DefectStudio
{
    Result<RendererStructureData> LoadRendererStructureViaPython(
        const Path &filePath,
        std::string name,
        PymatgenBridge &bridge)
    {
        // Wywołaj pymatgen Structure.from_file(path)
        auto structureResult = bridge.LoadStructure(filePath);
        if (!structureResult)
        {
            DS_LOG_ERROR("RendererPythonLoader: pymatgen failed to load '{}': {}",
                filePath.String(), structureResult.Error().technicalDetails);
            return structureResult.Error();
        }

        // TODO(T07): Konwersja Domain::Structure → RendererStructureData
        // Na razie konwertuj bezpośrednio z danych pymatgen
        RendererStructureData data;
        data.name = std::move(name);
        data.sourcePath = filePath.String();

        // Wypełnij data.atoms, data.lattice, data.cellEdges z structureResult
        // Szczegóły zależą od API PymatgenBridge – sprawdź PymatgenBridge.hpp
        // i wypełnij analogicznie do RendererPoscarLoader.cpp

        DS_LOG_INFO("RendererPythonLoader: loaded '{}' via Python ({} atoms)",
            data.name, data.atoms.size());
        return data;
    }
}
```

#### Krok 3: Zaktualizuj wywołania ładowania struktur

W miejscach gdzie wywoływany jest `LoadRendererStructureFromPoscar`:
- Jeśli `PymatgenBridge` jest dostępny (Python capability aktywna) → użyj `LoadRendererStructureViaPython`
- Fallback: `LoadRendererStructureFromPoscar` (C++ parser)

Wzorzec:
```cpp
// W RendererLayer lub IOLayer handler:
if (m_PythonAvailable && m_PymatgenBridge)
{
    auto result = LoadRendererStructureViaPython(path, name, *m_PymatgenBridge);
    if (result)
        return result;
    DS_LOG_WARN("Python loader failed, falling back to C++ parser");
}
return LoadRendererStructureFromPoscar(path, name, atomStyleTable, elementPropertiesTable);
```

Dodaj komentarz przy `RendererPoscarLoader`:
```cpp
// TODO(T07): Ten parser zostanie zastąpiony przez Python bridge (PymatgenBridge).
// Zostaw jako fallback gdy Python jest niedostępny.
```

---

## ETAP R3 – Input przez EventBus, lokalne undo widoku, układ okresowy

> **Ważne:** Ten etap wymaga Etapu R1 i R2 jako prerekvizytów.
>
> **Aktualizacja architektoniczna po analizie R3:**
> - Każde okno/viewport renderera ma mieć stabilny techniczny identyfikator `windowId` w formie UUID/string.
>   `title` pozostaje nazwą UI i nie może być używany jako klucz logiki.
> - `RendererPanel` nie manipuluje kamerą bezpośrednio dla inputu viewportu. Panel emituje semantic events
>   z `windowId`, a `RendererLayer` jako właściciel stanu znajduje okno i zmienia kamerę.
> - Globalny `UndoStack` jest dla zmian projektu/danych domenowych. Zmiany widoku kamery są lokalnym stanem
>   viewportu i trafiają do lokalnej historii widoku per `windowId`, bez rozszerzania Core `UndoStack`
>   o domeny undo w tym etapie.
> - Skróty: `Ctrl+Z` = global undo, `Ctrl+Y` / `Ctrl+Shift+Z` = global redo,
>   `Ctrl+Alt+Z` = lokalne undo aktywnego viewportu, `Ctrl+Alt+Y` / `Ctrl+Alt+Shift+Z` = lokalne redo viewportu.
> - Kolejność commitów: R3.1, R3.2, R3.3 + `windowId`, R3.5, R3.4 + `lastFocusedState`, R3.6 atomowo
>   z cleanupem `RendererPanelInput.cpp` i `RendererPanelToolbar.cpp`.

### R3.1 – Utwórz src/Events/RendererEvents.hpp

Utwórz nowy plik `src/Events/RendererEvents.hpp`:

```cpp
#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include <string>

namespace DefectStudio::RendererEvents::Viewport
{
    // Emitowane przez RendererPanel gdy viewport jest aktywny.
    // RendererLayer subskrybuje i deleguje do kamery.

    struct OrbitDelta final : public BusEvent
    {
        std::string windowId;
        float dx = 0.0f;
        float dy = 0.0f;
    };

    struct PanDelta final : public BusEvent
    {
        std::string windowId;
        float dx = 0.0f;
        float dy = 0.0f;
    };

    struct ZoomDelta final : public BusEvent
    {
        std::string windowId;
        float amount = 0.0f;
    };

    struct FocusChanged final : public BusEvent
    {
        std::string windowId;
        bool focused = false;
    };
} // namespace DefectStudio::RendererEvents::Viewport
```

---

### R3.2 – Utwórz lokalną komendę widoku SetCameraViewCommand

Utwórz katalog `src/Renderer/Commands/`.

> **Zakres po aktualizacji:** `SetCameraViewCommand` jest lokalnym modelem zmiany widoku kamery.
> Nie rejestruj go w globalnym `CommandRegistry` i nie pushuj do globalnego `UndoStack`.
> W R3.6 może być użyty przez lokalną historię viewportu albo zastąpiony prostym
> `RendererViewStateChange`, jeśli implementacja lokalnej historii będzie prostsza.

Plik `src/Renderer/Commands/SetCameraViewCommand.hpp`:

```cpp
#pragma once

#include "Core/Commands/Command.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
    // Undoable: zmiana widoku kamery (orbit, pan, zoom, transition).
    // Execute: przechodzi do newView. Undo: wraca do oldView.
    // CanMerge: scala kolejne małe zmiany (np. ciągły orbit myszką) w jeden rekord.
    class SetCameraViewCommand final : public ICommand
    {
    public:
        SetCameraViewCommand(
            RendererViewSnapshot oldView,
            RendererViewSnapshot newView,
            std::string description,
            std::string windowId);

        [[nodiscard]] Result<void> Execute(CommandContext &context) override;
        [[nodiscard]] Result<void> Undo(CommandContext &context)   override;
        [[nodiscard]] Result<void> Redo(CommandContext &context)   override;
        [[nodiscard]] std::string  Description()    const override;
        [[nodiscard]] bool         IsUndoable()     const noexcept override;
        [[nodiscard]] bool         CanMerge(const ICommand &next) const noexcept override;
        [[nodiscard]] Result<void> Merge(std::unique_ptr<ICommand> next) override;

        [[nodiscard]] const std::string &WindowId() const noexcept;

    private:
        RendererViewSnapshot m_OldView;
        RendererViewSnapshot m_NewView;
        std::string          m_Description;
        std::string          m_WindowId;
    };
} // namespace DefectStudio
```

> **Uwaga architektoniczna:** ta komenda nie jest podpinana do globalnego `UndoStack` dla zmian kamery.
> Jeśli zostaje użyta w R3.6, działa jako element lokalnej historii widoku viewportu. Globalny `UndoStack`
> pozostaje dla zmian projektu/danych domenowych.

Plik `src/Renderer/Commands/SetCameraViewCommand.cpp`:

```cpp
#include "Core/dspch.hpp"
#include "Renderer/Commands/SetCameraViewCommand.hpp"

namespace DefectStudio
{
    SetCameraViewCommand::SetCameraViewCommand(
        RendererViewSnapshot oldView,
        RendererViewSnapshot newView,
        std::string description,
        std::string windowId)
        : m_OldView(std::move(oldView))
        , m_NewView(std::move(newView))
        , m_Description(std::move(description))
        , m_WindowId(std::move(windowId))
    {
    }

    // Execute i Redo aplikują m_NewView do kamery windowId.
    // Implementacja: lokalna historia widoku musi mieć callback/API do RendererLayer.
    // Tymczasowo – TODO(R3.3): podpięcie przez CommandContext lub callback.
    Result<void> SetCameraViewCommand::Execute(CommandContext &)
    {
        // TODO(R3.6): ApplyViewToWindow(m_WindowId, m_NewView);
        return {};
    }

    Result<void> SetCameraViewCommand::Undo(CommandContext &)
    {
        // TODO(R3.6): ApplyViewToWindow(m_WindowId, m_OldView);
        return {};
    }

    Result<void> SetCameraViewCommand::Redo(CommandContext &context)
    {
        return Execute(context);
    }

    std::string SetCameraViewCommand::Description() const
    {
        return m_Description;
    }

    bool SetCameraViewCommand::IsUndoable() const noexcept
    {
        return true;
    }

    bool SetCameraViewCommand::CanMerge(const ICommand &next) const noexcept
    {
        const auto *other = dynamic_cast<const SetCameraViewCommand *>(&next);
        return other != nullptr && other->m_WindowId == m_WindowId;
    }

    Result<void> SetCameraViewCommand::Merge(std::unique_ptr<ICommand> next)
    {
        auto *other = dynamic_cast<SetCameraViewCommand *>(next.get());
        if (other == nullptr)
            return MakeError("renderer.camera.merge_type_mismatch",
                "Cannot merge incompatible commands", "", "SetCameraViewCommand");
        // Zachowaj m_OldView (punkt startowy), zaktualizuj m_NewView (punkt końcowy)
        m_NewView       = other->m_NewView;
        m_Description   = other->m_Description;
        return {};
    }

    const std::string &SetCameraViewCommand::WindowId() const noexcept
    {
        return m_WindowId;
    }
}
```

> **Uwaga:** `MakeError` zastąp wywołaniem `StructuredError{...}` zgodnym z istniejącym kodem projektu.

---

### R3.3 – RendererLayer subskrybuje eventy viewportu i OnEvent klawiaturowy

**Przeczytaj:** `src/Renderer/RendererLayer.hpp`, `src/Renderer/RendererLayer.cpp`, `src/Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp`, `src/Events/RendererEvents.hpp` (nowy z R3.1)

#### Krok 1: Dodaj subskrypcje w RendererLayer::OnAttach

W `RendererLayer::OnAttach()` lub `RendererLayer::BindEventBus()`, dodaj subskrypcje przez EventBus:

```cpp
// Subskrypcje semantic events z RendererPanel
AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::OrbitDelta>(
    std::bind_front(&RendererLayer::onOrbitDelta, this)));
AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::PanDelta>(
    std::bind_front(&RendererLayer::onPanDelta, this)));
AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ZoomDelta>(
    std::bind_front(&RendererLayer::onZoomDelta, this)));
AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::FocusChanged>(
    std::bind_front(&RendererLayer::onViewportFocusChanged, this)));
```

#### Krok 2: Dodaj prywatne metody do RendererLayer

W `src/Renderer/RendererLayer.hpp`, sekcja private:
```cpp
void onOrbitDelta(const RendererEvents::Viewport::OrbitDelta &event);
void onPanDelta(const RendererEvents::Viewport::PanDelta &event);
void onZoomDelta(const RendererEvents::Viewport::ZoomDelta &event);
void onViewportFocusChanged(const RendererEvents::Viewport::FocusChanged &event);
[[nodiscard]] RendererWindowState *findWindowById(const std::string &windowId);
```

#### Krok 3: Implementuj w RendererLayer.cpp

Dodaj `std::string windowId;` do `RendererWindowState` i ustawiaj je przy tworzeniu okna.
Wartość ma być stabilnym technicznym identyfikatorem, najlepiej UUID. Nie używaj `title`
jako źródła prawdy dla logiki; `title` jest tylko etykietą UI.

```cpp
RendererWindowState *RendererLayer::findWindowById(const std::string &windowId)
{
    for (auto &ws : m_Windows)
        if (ws.windowId == windowId)
            return &ws;
    return nullptr;
}

void RendererLayer::onOrbitDelta(const RendererEvents::Viewport::OrbitDelta &event)
{
    auto *ws = findWindowById(event.windowId);
    if (!ws || !ws->camera) return;
    ws->camera->Orbit(event.dx, event.dy);
    // TODO(R3.6): zapisz zmianę w lokalnej historii widoku viewportu
}

void RendererLayer::onPanDelta(const RendererEvents::Viewport::PanDelta &event)
{
    auto *ws = findWindowById(event.windowId);
    if (!ws || !ws->camera) return;
    ws->camera->Pan(event.dx, event.dy);
}

void RendererLayer::onZoomDelta(const RendererEvents::Viewport::ZoomDelta &event)
{
    auto *ws = findWindowById(event.windowId);
    if (!ws || !ws->camera) return;
    ws->camera->Zoom(event.amount);
}

void RendererLayer::onViewportFocusChanged(const RendererEvents::Viewport::FocusChanged &event)
{
    // TODO(R3.5): aktywuj/deaktywuj kontekst "renderer.viewport" w ContextManager
    DS_LOG_TRACE("Renderer viewport '{}' focus: {}", event.windowId, event.focused);
}
```

---

### R3.4 – RendererPanel emituje semantic events zamiast manipulować kamerą

**Przeczytaj:** `src/Presentation/Panels/RendererPanel.cpp`, `src/Presentation/Panels/RendererPanel.hpp`, `src/Events/RendererEvents.hpp`

#### Krok 1: Dodaj EventBus do RendererPanel

RendererPanel ma dostęp do `m_Layer`. Dodaj publiczny getter w `RendererLayer`:
```cpp
[[nodiscard]] Ref<EventBus> GetEventBus() const;
```

Dodaj helper:
```cpp
// W RendererPanel.cpp, na początku applyViewportInputNavigation:
Ref<EventBus> eventBus = m_Layer.GetEventBus();
```

#### Krok 2: Zamień bezpośrednie wywołania kamery na emit

W `applyViewportInputNavigation` (RendererPanelInput.cpp po R2.4), zamień:

```cpp
// BYŁO:
windowState.camera->Orbit(delta.x * ..., delta.y * ...);

// JEST:
if (eventBus)
{
    RendererEvents::Viewport::OrbitDelta orbitEvent;
    orbitEvent.windowId = windowState.windowId;
    orbitEvent.dx = delta.x * m_Layer.GetGlobalSettings().orbitSensitivity;
    orbitEvent.dy = delta.y * m_Layer.GetGlobalSettings().orbitSensitivity;
    eventBus->Publish(orbitEvent);
}
```

Analogicznie dla Pan i Zoom.

#### Krok 3: FocusChanged przy zmianie focused state

W miejscu gdzie sprawdzasz `ImGui::IsWindowFocused()`, dodaj emit gdy stan się zmienia (przy zmianie z true→false lub false→true):
```cpp
const bool nowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
if (nowFocused != windowState.lastFocusedState)
{
    windowState.lastFocusedState = nowFocused;
    if (eventBus)
    {
        RendererEvents::Viewport::FocusChanged focusEvent;
        focusEvent.windowId = windowState.windowId;
        focusEvent.focused   = nowFocused;
        eventBus->Publish(focusEvent);
    }
}
```

Dodaj `bool lastFocusedState = false;` do `RendererWindowState` (w RendererWindowState.hpp po R1.3).

#### Krok 4: Zostaw IsItemHovered jako gate

`ImGui::IsItemHovered()` pozostaje w RendererPanel jako sprawdzenie czy emitować event. Jest to poprawny wyjątek – wymaga aktywnego ImGui kontekstu.

---

### R3.5 – Przenieś układ okresowy do pliku konfiguracyjnego

**Przeczytaj:** `src/Presentation/Panels/RendererPanel.cpp` – funkcja `drawPeriodicTableWindow`

#### Krok 1: Utwórz plik konfiguracyjny

Utwórz `install/app/assets/config/periodic_table.yaml`:

```yaml
# Pełna tabela układu okresowego
# Format: symbol: numer atomowy
elements:
  - symbol: H
  - symbol: He
  - symbol: Li
  # ... itd. – wszystkie 118 pierwiastków w kolejności
lanthanides:
  - La
  - Ce
  - Pr
  # ... 15 lantanowców
actinides:
  - Ac
  - Th
  - Pa
  # ... 15 aktynowców
```

Wypełnij wszystkie symbole – przepisz je z hardcoded tablic w `RendererPanel.cpp`.

#### Krok 2: Załaduj w RendererLayer::OnAttach

W `RendererLayer::OnAttach()`, po załadowaniu innych assetów:
```cpp
// Załaduj układ okresowy
const Path periodicTablePath = m_StartupConfig.assetsDirectory.Native()
    / "config" / "periodic_table.yaml";
// Parsuj YAML i wypełnij m_PeriodicTableSymbols, m_LanthanideSymbols, m_ActinideSymbols
```

Dodaj do `RendererLayer` pola:
```cpp
std::vector<std::string> m_PeriodicTableSymbols;
std::vector<std::string> m_LanthanideSymbols;
std::vector<std::string> m_ActinideSymbols;
```

Dodaj publiczne gettery:
```cpp
[[nodiscard]] const std::vector<std::string> &GetPeriodicTableSymbols() const;
[[nodiscard]] const std::vector<std::string> &GetLanthanideSymbols()    const;
[[nodiscard]] const std::vector<std::string> &GetActinideSymbols()      const;
```

#### Krok 3: Zaktualizuj drawPeriodicTableWindow

W `RendererPanel.cpp`, w `drawPeriodicTableWindow()`, zastąp hardcoded tablice wywołaniami:
```cpp
const auto &symbols     = m_Layer.GetPeriodicTableSymbols();
const auto &lanthanides = m_Layer.GetLanthanideSymbols();
const auto &actinides   = m_Layer.GetActinideSymbols();
```

---

### R3.6 – Lokalna historia widoku viewportu zamiast globalnego UndoStack

> **Uwaga:** Kamera i ustawienia widoku są lokalnym stanem viewportu. Nie podpinaj ich do globalnego
> `UndoStack`, bo globalne `Ctrl+Z` ma cofać zmiany projektu/danych domenowych, a nie orbitowanie kamery.

**Przeczytaj:** `src/Renderer/RendererWindowState.hpp` (po R1.3), `src/Presentation/Panels/RendererPanelInput.cpp` (po R2.4),
`src/Presentation/Panels/RendererPanelToolbar.cpp`, `src/Renderer/RendererLayer.hpp`, `src/Renderer/RendererLayer.cpp`

#### Krok 1: Przenieś historię widoku do RendererLayer

Historia widoku może pozostać per `RendererWindowState`, ale jej właścicielem i jedynym modyfikatorem ma być
`RendererLayer`, nie `RendererPanel`.

W `RendererWindowState` zostaw lub przenieś do wewnętrznej mapy `RendererLayer`:
```cpp
std::vector<RendererViewStateChange> viewUndoHistory;
std::vector<RendererViewStateChange> viewRedoHistory;
bool viewInteractionActive = false;
std::string viewInteractionSource;
RendererViewSnapshot viewInteractionStart;
```

Jeśli historia zostaje w `RendererWindowState`, `RendererPanel` nie może jej bezpośrednio modyfikować.
Jeśli historia trafia do mapy w `RendererLayer`, użyj klucza `windowId`:
```cpp
std::unordered_map<std::string, RendererViewHistory> m_ViewHistories;
```

#### Krok 2: Dodaj API lokalnej historii do RendererLayer

W `RendererLayer.hpp` dodaj publiczne metody dla lokalnego undo/redo aktywnego viewportu:
```cpp
void BeginViewInteraction(const std::string &windowId, std::string sourceAction);
void CommitViewInteraction(const std::string &windowId);
void CancelViewInteraction(const std::string &windowId);
void UndoViewChange(const std::string &windowId);
void RedoViewChange(const std::string &windowId);
```

Dodaj prywatne helpery:
```cpp
[[nodiscard]] RendererViewSnapshot captureViewSnapshot(const RendererWindowState &windowState) const;
void restoreViewSnapshot(RendererWindowState &windowState, const RendererViewSnapshot &snapshot, const char *sourceAction);
void pushViewChange(
    RendererWindowState &windowState,
    const RendererViewSnapshot &before,
    const RendererViewSnapshot &after,
    const char *sourceAction);
```

#### Krok 3: Rejestruj zmiany kamery w RendererLayer

W `RendererLayer::onOrbitDelta`, `onPanDelta`, `onZoomDelta`:
- znajdź okno po `event.windowId`,
- zrób snapshot przed zmianą,
- wykonaj `Orbit` / `Pan` / `Zoom`,
- zrób snapshot po zmianie,
- zapisz zmianę do lokalnej historii widoku dla `windowId`.

Nie twórz wpisów w globalnym `UndoStack`.

#### Krok 4: RendererPanel nie zarządza historią widoku

W `src/Presentation/Panels/RendererPanelInput.cpp` i `src/Presentation/Panels/RendererPanelToolbar.cpp` usuń bezpośrednie
zarządzanie historią:
- `pushViewChange`
- `undoViewChange`
- `redoViewChange`
- bezpośrednie modyfikowanie `viewUndoHistory`
- bezpośrednie modyfikowanie `viewRedoHistory`

Jeśli panel potrzebuje rozpocząć/zakończyć interakcję, woła API `RendererLayer`:
```cpp
m_Layer.BeginViewInteraction(windowState.windowId, "mouse.orbit");
m_Layer.CommitViewInteraction(windowState.windowId);
```

#### Krok 5: Skróty lokalnego undo/redo

`Ctrl+Z` i globalne redo pozostają w Core przez `KeyInputProcessor` / `CommandRegistry`.

Lokalne skróty viewportu obsłuż w ścieżce renderera, tylko gdy viewport jest aktywny/focused:
```cpp
Ctrl+Alt+Z              -> m_Layer.UndoViewChange(activeWindowId);
Ctrl+Alt+Y              -> m_Layer.RedoViewChange(activeWindowId);
Ctrl+Alt+Shift+Z        -> m_Layer.RedoViewChange(activeWindowId);
```

Nie przechwytuj zwykłego `Ctrl+Z`, żeby nie blokować globalnego undo projektu.

#### Krok 6: Commit R3.6 musi być atomowy

Ten commit musi objąć jednocześnie:
- API lokalnej historii w `RendererLayer`
- cleanup `RendererPanelInput.cpp`
- cleanup `RendererPanelToolbar.cpp`
- aktualizację deklaracji w `RendererPanel.hpp`
- lokalne skróty `Ctrl+Alt+Z` / `Ctrl+Alt+Y`

Nie dziel R3.6 na mniejsze commity, bo stan pośredni może się nie kompilować.

---

# CZĘŚĆ II – CORE / APP / PRESENTATION

> Sesje kontynuowane z istniejącego planu. Stosuj tę samą kolejność commit/sesja.

---

## SESJA 2 – Komentarze known limitations w UndoStack

**Przeczytaj przed rozpoczęciem:** `src/Core/Undo/UndoStack.cpp`

### 2.1 Dokumentacja partial rollback

W `UndoStack::applyUndoRecord`, bezpośrednio przed pętlą `for`, dodaj:
```cpp
// Known limitation: if Undo fails mid-group, commands processed before the
// failure are already reverted while remaining commands are not. The stack
// index is not decremented on failure, leaving domain state partially reverted.
// Mitigation: implement Undo as an infallible operation in all ICommand subclasses.
```

### 2.2 Dokumentacja CancelGroup semantyki

W `UndoStack::CancelGroup`, bezpośrednio przed `--m_GroupDepth`, dodaj:
```cpp
// CancelGroup semantics:
// - Decrements m_GroupDepth (symmetric with EndGroup)
// - Sets m_GroupCancelled = true so outer EndGroup knows to rollback
// - When depth reaches 0: immediately applies Undo to all accumulated commands
// - When depth > 0: defers rollback to the outermost EndGroup/CancelGroup
```

---

## SESJA 3 – Usunięcie Application::Get() z Settings

**Przeczytaj przed rozpoczęciem:**
`src/Presentation/Panels/Settings.cpp`, `src/Presentation/ImGuiLayer.hpp`, `src/App/Application.hpp`

### 3.1 Rozszerz ImGuiLayerRuntime o JobSystem

W `src/Presentation/ImGuiLayer.hpp`, w struct `ImGuiLayerRuntime`, dodaj:
```cpp
WeakRef<JobSystem> jobSystem;
```

### 3.2 Uzupełnij runtime w Application.cpp

W miejscu gdzie tworzony jest `ImGuiLayerRuntime`, dodaj:
```cpp
runtime.jobSystem = m_CoreLayer->GetJobSystemHandle();
```

Dodaj `GetJobSystemHandle()` do `CoreLayer` jeśli brakuje:
```cpp
[[nodiscard]] WeakRef<JobSystem> GetJobSystemHandle() const;
```

### 3.3 Zaktualizuj Settings.cpp

Wykonaj: `grep -n "Application::Get()" src/Presentation/Panels/Settings.cpp`

Dla każdego trafienia zastąp odpowiednim member pobranym przez wstrzyknięte zależności panelu (przez `m_Runtime` lub konstruktor). Nie przywracaj `Application::Get()`.

---

## SESJA 4 – CommandRegistry: logowanie błędów rejestracji

**Przeczytaj przed rozpoczęciem:** `src/Core/Commands/CommandRegistry.cpp`

### 4.1 Dodaj DS_LOG_ERROR do każdego punktu błędu w Register

W funkcji `CommandRegistry::Register`, przed każdym `return` zwracającym błąd:

```cpp
// Przed: return StructuredError{..., "command.register.empty_id", ...};
// Po:
DS_LOG_ERROR("CommandRegistry: registration failed [command.register.empty_id]: id is empty");
return StructuredError{..., "command.register.empty_id", ...};
```

Dla duplikatu:
```cpp
DS_LOG_ERROR("CommandRegistry: registration failed [command.register.duplicate]: id='{}'", meta.id.value);
```

Dla brakującej factory:
```cpp
DS_LOG_ERROR("CommandRegistry: registration failed [command.register.no_factory]: id='{}'", meta.id.value);
```

---

## SESJA 5 – Kolejność inicjalizacji CapabilityService i Notifier

**Przeczytaj przed rozpoczęciem:** `src/App/ApplicationLifecycle.cpp` lub `Application.cpp` – funkcje `initializeEventInfrastructure` i `initializeCoreRuntimeServices`

### 5.1 Przesuń inicjalizację

Upewnij się że `initializeCoreRuntimeServices()` inicjalizuje w tej kolejności:
1. `m_CapabilityService = CreateRef<CapabilityService>();`
2. Rejestracje capabilities: `m_CapabilityService->RegisterCapability(...)`
3. `m_CapabilityService->LockAfterStartup();`
4. `m_Notifier = CreateRef<Notifier>(m_EventBus);`
5. Reszta inicjalizacji

`initializeEventInfrastructure()` powinna zawierać wyłącznie: `m_EventBus`, `m_EventQueue`, `m_EventController`. Przenieś tam co nie pasuje.

---

## SESJA 6 – NotificationCenter: RemoveListener i NotificationHandler

**Przeczytaj przed rozpoczęciem:**
`src/Core/Notifications/NotificationCenter.hpp`,
`src/Core/Notifications/NotificationCenter.cpp`

### 6.1 Zmień API

W `NotificationCenter.hpp`:
```cpp
// Zamień:
using Listener = std::function<void(const Notification &)>;
void RegisterListener(Listener listener);

// Na:
using NotificationHandler = std::function<void(const Notification &)>;
[[nodiscard]] std::size_t RegisterListener(NotificationHandler listener);
void RemoveListener(std::size_t listenerId);
```

Zmień prywatne pole:
```cpp
// Zamień:
std::vector<Listener> m_Listeners;

// Na:
std::unordered_map<std::size_t, NotificationHandler> m_Listeners;
std::size_t m_NextListenerId = 1;
```

### 6.2 Zaktualizuj implementację w NotificationCenter.cpp

```cpp
std::size_t NotificationCenter::RegisterListener(NotificationHandler listener)
{
    if (!listener)
        return 0;
    const std::size_t id = m_NextListenerId++;
    m_Listeners.emplace(id, std::move(listener));
    return id;
}

void NotificationCenter::RemoveListener(std::size_t listenerId)
{
    m_Listeners.erase(listenerId);
}
```

W metodzie dispatch (onNotification lub odpowiedniej):
```cpp
for (const auto &[id, handler] : m_Listeners)
    if (handler)
        handler(notification);
```

### 6.3 Zaktualizuj callsites

Wykonaj: `grep -rn "RegisterListener" src/`

Dla każdego miejsca które nie zapisywało ID – zapisz:
```cpp
m_NotificationCenterListenerId = m_NotificationCenter->RegisterListener(...);
```

Dodaj `std::size_t m_NotificationCenterListenerId = 0;` do klas które rejestrują listener.

---

## SESJA 7 – Naprawa podwójnego ProcessQueue

**Przeczytaj przed rozpoczęciem:** `src/App/Application.cpp` lub `ApplicationLifecycle.cpp` – metoda `persistUiSettings`

### 7.1 Znajdź i napraw

Znajdź w `persistUiSettings()` fragment z wielokrotnymi wywołaniami `m_EventBus->ProcessQueue()`.

Zastąp:
```cpp
m_EventBus->Queue(AppEvents::Config::SaveUserRequested{m_Config});
m_EventBus->ProcessQueue(); // SaveUserRequested → kolejkuje PersistRequested
m_EventBus->ProcessQueue(); // PersistRequested → przetworzony
```

Zostaw dokładnie dwa `ProcessQueue()` jeśli łańcuch zdarzeń tego wymaga (Request → Queue → Process). Usuń nadmiarowe trzecie lub kolejne wywołania.

---

## SESJA 8 – Memory.hpp: usuń dspch.hpp, dodaj <memory>

**Przeczytaj przed rozpoczęciem:** `src/Core/Utils/Memory.hpp`

### 8.1 Zmień include

W `src/Core/Utils/Memory.hpp` usuń:
```cpp
#include "Core/dspch.hpp"
```

Dodaj na początku pliku:
```cpp
#include <memory>
```

### 8.2 Napraw kompilację

Skompiluj projekt. Dla każdego pliku `.cpp` który przestał się kompilować z powodu brakującego include (np. brakujące `<string>`, `<vector>`):
- Dodaj jawny `#include` dla brakującego nagłówka w tym konkretnym `.cpp`
- Nie przywracaj `dspch.hpp` do `Memory.hpp`

---

## SESJA 9 – Przenieś pliki Logger do Core/Logging

**Przeczytaj przed rozpoczęciem:**
`src/Core/Utils/Logger.hpp`, `src/Core/Utils/Logger.cpp`,
`src/Core/Utils/SpdlogEventSink.hpp`, `src/Core/Utils/SpdlogEventSink.cpp`,
`src/Events/LogEvents.hpp`, `src/Events/NotificationEvents.hpp`

### 9.1 Przenieś pliki (git mv)

```bash
git mv src/Core/Utils/Logger.hpp         src/Core/Logging/Logger.hpp
git mv src/Core/Utils/Logger.cpp         src/Core/Logging/Logger.cpp
git mv src/Core/Utils/SpdlogEventSink.hpp src/Core/Logging/SpdlogEventSink.hpp
git mv src/Core/Utils/SpdlogEventSink.cpp src/Core/Logging/SpdlogEventSink.cpp
git mv src/Events/LogEvents.hpp          src/Core/Logging/LogEvents.hpp
git mv src/Events/NotificationEvents.hpp src/Core/Notifications/NotificationEvents.hpp
```

### 9.2 Zaktualizuj wszystkie include paths

Wykonaj globalne zamiany w całym `src/`:
- `"Core/Utils/Logger.hpp"` → `"Core/Logging/Logger.hpp"`
- `"Core/Utils/SpdlogEventSink.hpp"` → `"Core/Logging/SpdlogEventSink.hpp"`
- `"Events/LogEvents.hpp"` → `"Core/Logging/LogEvents.hpp"`
- `"Events/NotificationEvents.hpp"` → `"Core/Notifications/NotificationEvents.hpp"`

Sprawdź: `grep -rn "Core/Utils/Logger\|Events/LogEvents\|Events/NotificationEvents" src/`

### 9.3 Zaktualizuj dspch.hpp

Jeśli `src/Core/dspch.hpp` zawiera `#include "Core/Utils/Logger.hpp"` – zaktualizuj na `"Core/Logging/Logger.hpp"`.

### 9.4 Usuń pusty src/Events/ jeśli pusty

```bash
ls src/Events/
# Jeśli puste:
git rm -r src/Events/
```

Zaktualizuj `premake5.lua` jeśli `src/Events/` był jawnie wymieniony w `files` lub `includedirs`.

---

## SESJA 10 – TestJobs → tests/

**Przeczytaj przed rozpoczęciem:**
`src/Core/JobSystem/TestJobs/TestJobs.hpp`,
`src/Core/JobSystem/TestJobs/TestJobs.cpp`,
`premake5.lua`

### 10.1 Przenieś pliki

```bash
mkdir -p tests/Core/JobSystem/TestJobs
git mv src/Core/JobSystem/TestJobs/TestJobs.hpp tests/Core/JobSystem/TestJobs/TestJobs.hpp
git mv src/Core/JobSystem/TestJobs/TestJobs.cpp tests/Core/JobSystem/TestJobs/TestJobs.cpp
git rm -r src/Core/JobSystem/TestJobs/
```

### 10.2 Zaktualizuj premake5.lua

Usuń `src/Core/JobSystem/TestJobs/` ze źródeł produkcyjnych (`files` dla głównego projektu).

Dodaj `tests/Core/JobSystem/TestJobs/` do projektu testowego (`DefectStudioTests`).

Zaktualizuj `includedirs` projektu testowego jeśli konieczne.

---

## SESJA 11 – Rozbicie Application.cpp

**Przeczytaj przed rozpoczęciem:** `src/App/Application.hpp`, `src/App/Application.cpp`

Sprawdź czy `ApplicationLifecycle.cpp` już istnieje: `ls src/App/Application*.cpp`

Jeśli `ApplicationLifecycle.cpp` już istnieje i zawiera część metod, uzupełnij podział o brakujące:

### 11.1 Utwórz src/App/ApplicationWindow.cpp (jeśli nie istnieje)

Przenieś z `Application.cpp` implementacje:
- `Application::initializeGlfw()`
- `Application::createMainWindow()`
- `Application::initializeGraphics()`
- `Application::shutdownWindow()`
- `Application::shutdownGlfw()`
- `Application::configureInputBackend()`

Pierwsze linie:
```cpp
#include "Core/dspch.hpp"
#include "App/Application.hpp"
// + includes dla GLFW, GLAD, OpenGL które były tylko tu potrzebne
```

Usuń te includes z `Application.cpp` po przeniesieniu.

### 11.2 Uzupełnij ApplicationLifecycle.cpp (lub ApplicationBootstrap.cpp)

Upewnij się że poniższe metody są w pliku `ApplicationLifecycle.cpp` lub `ApplicationBootstrap.cpp`, nie w `Application.cpp`:
- `createFromSpecification`, `beginCreateFromSpecification`
- `bootstrapApplicationConfiguration`, `initializeEventInfrastructure`
- `initializeWindowingAndGraphics`, `initializeApplicationLayers`
- `initializeCoreRuntimeServices`, `finishCreateFromSpecification`
- `shutdownInternal`, `setupDefaultLayers`
- `bootstrapConfiguration`, `applySpecificationFromDefaultConfig`
- `logStartupSpecification`, `initializeLogger`, `shutdownLogger`
- `initializeEventDispatchingSystem`, `initializeCoreLayerSystems`
- `initializeAssetManager`, `persistUiSettings`

### 11.3 Zostaw w Application.cpp

Po podziale `Application.cpp` zawiera wyłącznie:
- `Application::Create()`
- `Application::Application()` (konstruktor)
- `Application::~Application()`
- `Application::Run()`
- `Application::Shutdown()`
- `Application::mainLoop()`
- `Application::runMainLoopFrame()`
- `Application::beginFrame()`
- `Application::renderFrame()`
- `Application::onUpdate()`
- `Application::onRender()`
- `Application::EmitEvent()`
- `Application::Get()`
- `Application::ShowBlockingError()`
- Wszystkie gettery runtime services
- `Application::OnEvent()`
- `Application::dispatchEventToLayers()`
- `Application::queueEvent()`
- `Application::processPendingEvents()`
- Zakomentowane `ProcessQueuedEvents()` (z R0.3)

---

## SESJA 12 – m_BlockingError → ImGuiLayer

**Przeczytaj przed rozpoczęciem:**
`src/App/Application.hpp`, `src/App/Application.cpp`,
`src/Presentation/ImGuiLayer.hpp`, `src/Presentation/ImGuiLayer.cpp`

### 12.1 Dodaj state do ImGuiLayer

W `src/Presentation/ImGuiLayer.hpp` dodaj includes (jeśli brakuje):
```cpp
#include "Core/Diagnostics/StructuredError.hpp"
#include <optional>
```

Dodaj do prywatnych memberów:
```cpp
std::optional<StructuredError> m_PendingBlockingError;
```

Dodaj publiczną metodę:
```cpp
void SetBlockingError(const StructuredError &error);
```

### 12.2 Rendering w OnImGuiRender

W `src/Presentation/ImGuiLayer.cpp`, na początku `OnImGuiRender()`, przed resztą UI:
```cpp
if (m_PendingBlockingError.has_value())
    ImGui::OpenPopup("Fatal Error##blocking");

if (ImGui::BeginPopupModal("Fatal Error##blocking", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
{
    ImGui::TextUnformatted(m_PendingBlockingError->userMessage.c_str());
    if (!m_PendingBlockingError->suggestion.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_PendingBlockingError->suggestion.c_str());
    }
    ImGui::Spacing();
    if (ImGui::Button("Quit Application"))
    {
        m_PendingBlockingError.reset();
        Application::Get().Shutdown();
    }
    ImGui::EndPopup();
}
```

Dodaj implementację `SetBlockingError`:
```cpp
void ImGuiLayer::SetBlockingError(const StructuredError &error)
{
    m_PendingBlockingError = error;
}
```

### 12.3 Zaktualizuj Application

W `src/App/Application.hpp` usuń:
```cpp
std::optional<StructuredError> m_BlockingError;
```

W `src/App/Application.cpp`, zmień `ShowBlockingError`:
```cpp
void Application::ShowBlockingError(const StructuredError &error)
{
    DS_LOG_CRITICAL("Blocking error: {} [{}]", error.userMessage, error.code);
    if (auto *imguiLayer = m_LayerStack.FindLayer<ImGuiLayer>())
        imguiLayer->SetBlockingError(error);
}
```

Jeśli `LayerStack::FindLayer<T>()` nie istnieje, dodaj templated metodę do `LayerStack`:
```cpp
template <typename TLayer>
[[nodiscard]] TLayer *FindLayer()
{
    for (auto &layer : m_Layers)
        if (auto *ptr = dynamic_cast<TLayer *>(layer.get()))
            return ptr;
    return nullptr;
}
```

Usuń wszystkie sprawdzenia i renderowanie `m_BlockingError` z `Application.cpp`.

---

## SESJA 13 – EventBus: usunięcie const_cast

**Przeczytaj przed rozpoczęciem:**
`src/Core/EventSystem/Common/EventControl.hpp`,
`src/Core/EventSystem/BusEventSystem/EventBus.hpp`,
`src/Core/EventSystem/BusEventSystem/EventBus.cpp`

### 13.1 Dodaj mutable do EventControl

W `src/Core/EventSystem/Common/EventControl.hpp`, zmień:
```cpp
bool handled         = false;
bool stopPropagation = false;
```
Na:
```cpp
mutable bool handled         = false;
mutable bool stopPropagation = false;
```

### 13.2 Usuń const_cast z EventBus::Publish

W `src/Core/EventSystem/BusEventSystem/EventBus.hpp` zmień sygnaturę template:
```cpp
// Zamień:
void Publish(const TEvent &event);

// Na:
void Publish(TEvent &event);
```

W `src/Core/EventSystem/BusEventSystem/EventBus.cpp` (lub w template body w `.hpp`):
- Usuń `const_cast<TEvent &>(event)` – przekazuj `event` bezpośrednio

### 13.3 Zaktualizuj callsites

Wykonaj: `grep -rn "->Publish\|\.Publish" src/`

Dla każdego miejsca gdzie zmienna była `const`:
```cpp
// Było:
const SomeEvent event{...};
m_EventBus->Publish(event);

// Jest:
SomeEvent event{...};
m_EventBus->Publish(event);
```

---

## SESJA 14 – LoggingPanel: tablice bool → mapy z enum

**Przeczytaj przed rozpoczęciem:**
`src/Presentation/Panels/LoggingPanel.hpp`,
`src/Presentation/Panels/LoggingPanel.cpp`

### 14.1 Zmień typy pól filtrów

W `LoggingPanel.hpp`, zastąp istniejące tablice bool dla poziomów i kategorii:
```cpp
std::array<bool, static_cast<std::size_t>(LogLevel::Count)> m_ShowLevel;
std::unordered_map<LogCategory, bool>                        m_ShowCategory;
```

Dodaj includes jeśli brakuje: `<array>`, `<unordered_map>`.

### 14.2 Zaktualizuj inicjalizację

W konstruktorze lub `OnAttach`:
```cpp
m_ShowLevel.fill(true);
for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
    m_ShowCategory[static_cast<LogCategory>(i)] = true;
```

### 14.3 Zaktualizuj rendering

Zamień sprawdzenia z magicznymi indeksami na enum:
```cpp
m_ShowLevel[static_cast<std::size_t>(entry.level)]
m_ShowCategory[entry.category]
```

Checkboxy przez iterację:
```cpp
for (std::size_t i = 0; i < m_ShowLevel.size(); ++i)
{
    const auto level = static_cast<LogLevel>(i);
    ImGui::Checkbox(ToString(level), &m_ShowLevel[i]);
    ImGui::SameLine();
}
```

---

## SESJA 15 – Wyłączenie klonowania paneli-singletonów

**Przeczytaj przed rozpoczęciem:**
`src/Presentation/Panels/LoggingPanel.hpp`,
`src/Presentation/Panels/SettingsPanel.hpp` (lub `Settings.hpp`),
`src/Presentation/Panels/ProgressMonitorWindow.hpp`,
`src/Presentation/Panels/TaskMonitorWindow.hpp`

### 15.1 Dodaj = delete do każdego panelu

W każdym z powyższych plików, po deklaracji publicznego konstruktora:
```cpp
// Non-copyable: this panel represents global application state (single session instance).
ClassName(const ClassName &)            = delete;
ClassName &operator=(const ClassName &) = delete;
ClassName(ClassName &&)                 = delete;
ClassName &operator=(ClassName &&)      = delete;
```

Nie dodawaj do: `IPanel`, klas Demo/, klas per-dokument lub per-analiza.

---

## SESJA 16 – Wykrywanie konfliktów KeyBinding

**Przeczytaj przed rozpoczęciem:**
`src/Core/Input/KeymapResolver.hpp`,
`src/Core/Input/KeymapResolver.cpp`

### 16.1 Dodaj struct KeyBindingConflict

W `src/Core/Input/KeymapResolver.hpp`:
```cpp
struct KeyBindingConflict
{
    KeyBinding existingBinding;
    KeyBinding newBinding;
};
```

### 16.2 Zmień RegisterBinding na Result<void>

```cpp
[[nodiscard]] Result<void> RegisterBinding(KeyBinding binding);
[[nodiscard]] const std::vector<KeyBindingConflict> &GetConflicts() const;
```

Dodaj prywatne pole:
```cpp
std::vector<KeyBindingConflict> m_Conflicts;
```

### 16.3 Implementacja w KeymapResolver.cpp

W `RegisterBinding`, przed faktyczną rejestracją, sprawdź konflikt:
```cpp
Result<void> KeymapResolver::RegisterBinding(KeyBinding binding)
{
    for (const auto &existing : m_Bindings)
    {
        if (existing.chord == binding.chord
            && existing.when.GetExpression() == binding.when.GetExpression()
            && existing.layer == binding.layer)
        {
            m_Conflicts.push_back({existing, binding});
            DS_LOG_WARN(
                "KeyBinding conflict: '{}' conflicts with '{}' on chord '{}'",
                binding.id, existing.id, ToString(binding.chord));
        }
    }
    m_Bindings.push_back(std::move(binding));
    return {};
}

const std::vector<KeyBindingConflict> &KeymapResolver::GetConflicts() const
{
    return m_Conflicts;
}
```

### 16.4 Zaktualizuj callsites RegisterBinding

Wykonaj: `grep -rn "RegisterBinding" src/`

Dla każdego wywołania które ignorowało wynik, sprawdź i zaloguj:
```cpp
if (auto result = m_KeymapResolver->RegisterBinding(...); !result)
    DS_LOG_WARN("Keybinding registration failed: {}", result.Error().technicalDetails);
```

---

## SESJA 17 – Zakładka Input w Settings

**Przeczytaj przed rozpoczęciem:**
`src/Presentation/Panels/Settings.hpp` (lub `Settings.cpp`),
`src/Core/Input/KeymapResolver.hpp`,
`src/Core/Commands/CommandRegistry.hpp`

### 17.1 Dodaj zależności do SettingsPanel

W konstruktorze `SettingsPanel` dodaj parametry:
```cpp
WeakRef<KeymapResolver>    keymapResolver,
WeakRef<CommandRegistry>   commandRegistry
```

Dodaj prywatne pola:
```cpp
WeakRef<KeymapResolver>  m_KeymapResolver;
WeakRef<CommandRegistry> m_CommandRegistry;
```

### 17.2 Dodaj enum wartość Tab::Input

Znajdź enum `Tab` w `Settings.hpp/cpp` i dodaj:
```cpp
Input,
```

### 17.3 Zaimplementuj renderInputTab

Dodaj prywatną metodę `renderInputTab()`. Implementacja wyświetla tabelę wszystkich bindingów z pola `m_KeymapResolver->GetAllBindings()` (dodaj `GetAllBindings()` do `KeymapResolver` jeśli brakuje).

Tabela kolumny: Chord | Command | Description | Context

Pokaż też konflikty jeśli `m_KeymapResolver->GetConflicts()` jest niepuste (pomarańczowe ostrzeżenie).

### 17.4 Podepnij w renderingu

W switch/if wybierającym zakładkę do renderowania:
```cpp
case Tab::Input:
    renderInputTab();
    break;
```

### 17.5 Zaktualizuj tworzenie SettingsPanel

W miejscu gdzie `SettingsPanel` jest tworzony (EditorLayer lub Application):
```cpp
CreateRef<SettingsPanel>(
    eventBus,
    jobSystem,
    uiState,
    coreLayer.GetKeymapResolverHandle(),
    coreLayer.GetCommandRegistryHandle(),
    "Settings");
```

Dodaj gettery do CoreLayer jeśli brakują:
```cpp
[[nodiscard]] WeakRef<KeymapResolver>  GetKeymapResolverHandle()  const;
[[nodiscard]] WeakRef<CommandRegistry> GetCommandRegistryHandle() const;
```

---

*Koniec dokumentu. Sesje należy implementować w podanej kolejności.*
*Część I (Renderer): R0 → R1 → R2 → R3.*
*Część II (Core/App): sesje numerowane mogą być implementowane równolegle z R2/R3 jeśli nie ma zależności.*

# Część III

1) sprawdznie czy wszystko znajduje się w opdpowiednich warstwach (bardzo ważne)
2) sprawdzenie czy wszystko jest serializowane tak jak powinno
3) napisanie dokumentacji z diagramami, dla osoby ktora nie wie jak dziala renderowanie
4) napisanie dokumentacji z diagramami, dla osoby która nie wie jak działa RenderingArchitecture
5) sprawdzenie i uzupełnienie testów tak aby pokrywały wszystko 
6) Sprawdzenie czy aby na pewno wszystkie systemy są wykorzystywane tak jak powinny (przed tym należy zrobić listę takowych systemów)
7) Sprawdzić, czy nie ma przypadkiem reimplementacji niektórych systemów 
