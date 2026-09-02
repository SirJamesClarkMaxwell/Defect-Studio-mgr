# Audyt Architektoniczny — `refactor/full-plan`
> Repozytorium: `Defect-Studio-mgr` | Gałąź: `refactor/full-plan`  
> Data audytu: czerwiec 2025  
> Cel: przygotowanie codebase pod projekt produkcyjny bazujący na tej architekturze

---

## Spis treści

1. [Mapa systemów projektu](#1-mapa-systemów-projektu)
2. [Przyjęte założenia architektoniczne](#2-przyjęte-założenia-architektoniczne)
3. [Kategoria A — Naruszenia warstw (Layer Violations)](#3-kategoria-a--naruszenia-warstw)
4. [Kategoria B — Reimplementacje istniejących systemów](#4-kategoria-b--reimplementacje-istniejących-systemów)
5. [Kategoria C — Serializacja i IO poza warstwą IO](#5-kategoria-c--serializacja-i-io-poza-warstwą-io)
6. [Kategoria D — Systemy zdefiniowane, ale nieużywane lub używane poniżej możliwości](#6-kategoria-d--systemy-zdefiniowane-ale-nieużywane-lub-używane-poniżej-możliwości)
7. [Plan naprawczy — priorytety i kolejność](#7-plan-naprawczy--priorytety-i-kolejność)
8. [Szczegółowe zadania naprawcze](#8-szczegółowe-zadania-naprawcze)

---

## 1. Mapa systemów projektu

Przed opisem naruszeń konieczne jest zdefiniowanie co i gdzie powinno żyć. Poniżej lista wszystkich systemów wraz z ich "właścicielem" w drzewie źródeł.

| System                                   | Lokalizacja                                                  | Odpowiedzialność                                                    |
| ---------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------------- |
| `EventBus`                               | `src/Core/EventSystem/BusEventSystem/`                       | Broker zdarzeń asynchronicznych / synchronicznych między warstwami  |
| `KeymapResolver`                         | `src/Core/Input/`                                            | Rozwiązywanie `KeyChord` → `CommandID` przez pryzmat kontekstu      |
| `KeyInputProcessor`                      | `src/Core/Input/`                                            | Wejście do systemu skrótów — pobiera chord, zwraca wynik            |
| `ContextManager`                         | `src/Core/Input/`                                            | Zarządza aktywnym kontekstem (np. `renderer.viewport`, `global`)    |
| `KeyCode` / `KeyChord`                   | `src/Core/Utils/KeyCodes.hpp`, `src/Core/Input/KeyChord.hpp` | Platforma-neutralna reprezentacja klawiszy i akordów                |
| `CommandRegistry` / `CommandService`     | `src/Core/Commands/`                                         | Rejestracja i wykonywanie komend domenowych                         |
| `UndoStack`                              | `src/Core/Undo/`                                             | Globalny stos undo/redo oparty na `ICommand`                        |
| `JobSystem`                              | `src/Core/JobSystem/`                                        | Wielowątkowa kolejka zadań z priorytetami i śledzeniem stanu        |
| `LogRegistry`                            | `src/Core/Logging/`                                          | Centralne składowanie wpisów logu                                   |
| `NotificationCenter`                     | `src/Core/Notifications/`                                    | System powiadomień dla użytkownika                                  |
| `ProgressTracker`                        | `src/Core/ProgressTrackingSystem/`                           | Śledzenie postępu zadań                                             |
| `ConfigManager` / `YamlConfigSerializer` | `src/App/Serialization/`                                     | Serializacja/deserializacja konfiguracji aplikacji                  |
| `IOLayer` / `TextFileIO`                 | `src/IO/`                                                    | Czytanie i zapis plików — jedyne miejsce uprawnione do dotyku dysku |
| `RendererLayer`                          | `src/Renderer/`                                              | Rendering 3D: zarządzanie oknami, kamerami, sceną                   |
| `OpenGlRendererBackend`                  | `src/Renderer/OpenGl/`                                       | Backend GPU — wyłącznie draw calls i zarządzanie zasobami GPU       |
| `PresentationLayer` / panele             | `src/Presentation/`                                          | Warstwa UI: ImGui, panele, input z myszy/klawiatury                 |
| `ScientificRuntime`                      | `src/ScientificRuntime/`                                     | Python, pymatgen, ASE, uruchamianie skryptów                        |
| `Domain`                                 | `src/Domain/`                                                | Modele domenowe (Crystal, Defect, etc.) niezależne od GPU i UI      |

### Oczekiwana hierarchia zależności (od dołu do góry)

```
Core (bez zależności zewnętrznych poza std / glm / spdlog)
  ↑
Domain (zależy od Core)
  ↑
IO (zależy od Core, Domain)
  ↑
ScientificRuntime (zależy od Core, Domain, IO)
  ↑
Renderer (zależy od Core, Domain, IO — NIE od Presentation, NIE od App, NIE od ImGui poza backendem UI)
  ↑
Presentation (zależy od Core, Domain, Renderer — posiada ImGui, obsługuje input)
  ↑
App (orkiestruje wszystkie warstwy, jedyne miejsce gdzie można mieć zależności krzyżowe)
```

**Kluczowa zasada:** Żadna warstwa nie może zależeć od warstwy znajdującej się nad nią. `Core` nie może zależeć od `ScientificRuntime`. `Renderer` nie może zależeć od `Presentation` (ImGui). `Events` nie mogą zależeć od `Presentation`.

---

## 2. Przyjęte założenia architektoniczne

Na podstawie istniejącego kodu i komentarzy ustalono następujące założenia, które zostały naruszone:

1. **Skróty klawiaturowe przechodzą przez `KeymapResolver`** — nie przez inline polling `ImGui::IsKeyPressed()`.
2. **Undo/redo przechodzi przez `UndoStack` + `ICommand`** — nie przez ręczne wektory historii.
3. **Interakcje z widokiem renderera przechodzą przez `EventBus`** — `RendererPanelInput` publikuje zdarzenia, `RendererLayer` je konsumuje.
4. **Pliki czyta i zapisuje wyłącznie `IOLayer`** — żaden panel, żaden renderer, żaden runtime nie otwiera `std::ifstream`/`std::ofstream` bezpośrednio.
5. **`Renderer` nie zna ImGui** — kształt i pozycja viewportu to dane z Presentation przekazywane przez event/parametr, nie `ImVec2`.
6. **`Core` nie zna `ScientificRuntime`** — GIL i Python to szczegóły implementacyjne ScientificRuntime, niewidoczne dla Core.

---

## 3. Kategoria A — Naruszenia warstw

### A-1 `RendererSettings.hpp` importuje `<imgui.h>` (KRYTYCZNE)

**Plik:** `src/Renderer/RendererSettings.hpp:7`

```cpp
// PROBLEM — Renderer zależy od ImGui
#include <imgui.h>

struct RendererKeyboardShortcutSettings
{
    ImGuiKey alignAxisA = ImGuiKey_A;
    ImGuiKey alignAxisB = ImGuiKey_B;
    // ...
};
```

`RendererSettings.hpp` jest nagłówkiem używanym przez cały moduł Renderer, w tym przez `OpenGlRendererBackend`. W rezultacie **backend GPU wymaga nagłówka ImGui tylko dlatego, że typy skrótów klawiaturowych są wyrażone jako `ImGuiKey`**. To fundamentalna infekcja warstwy.

`ImGuiKey` to typ UI-backendowy — należy do `Presentation`. Renderer powinien znać tylko własne klucze domenowe (`KeyCode` z `Core/Utils/KeyCodes.hpp`).

**Zakres infekcji przez include chain:**
- `RendererSettings.hpp` → `imgui.h`
- `RendererWindowState.hpp` → `imgui.h` (niezależnie, przez `ImVec2`)
- `OpenGlRendererBackend.hpp` → `RendererWindowState.hpp` → `imgui.h`
- Każdy plik `.cpp` w Renderer, który includuje którykolwiek z powyższych

---

### A-2 `RendererWindowState.hpp` importuje `<imgui.h>` (KRYTYCZNE)

**Plik:** `src/Renderer/RendererWindowState.hpp:11`

```cpp
// PROBLEM
#include <imgui.h>

struct RendererWindowState
{
    ImVec2 viewportSize = ImVec2(640.0f, 480.0f);   // ← typ ImGui
    ImVec2 lastMousePosition = ImVec2(0.0f, 0.0f);  // ← typ ImGui
    // ...
};
```

`ImVec2` to typ matematyczny z biblioteki ImGui. Renderer posiada już `glm::vec2` (przez zależność od `glm`). Użycie `ImVec2` zamiast `glm::vec2` zmusza każdy komponent korzystający z `RendererWindowState` do wiedzy o ImGui.

`lastMousePosition` jest szczególnie znamienne: pozycja myszy to dane wejściowe z warstwy `Presentation`. `RendererWindowState` nie powinien w ogóle przechowywać pozycji myszy — to stan interakcji, który należy do `RendererPanelInput` (Presentation).

---

### A-3 `OpenGlRendererBackend.hpp` importuje `<imgui.h>` (KRYTYCZNE)

**Plik:** `src/Renderer/OpenGl/OpenGlRendererBackend.hpp:7`

```cpp
// PROBLEM — backend GPU zna ImGui
#include <imgui.h>
```

Backend renderera nie używa **żadnego** typu ImGui bezpośrednio w swoim interfejsie publicznym — `ImGui` pojawia się tutaj wyłącznie jako zarażenie przez `RendererWindowState.hpp` i `RendererSettings.hpp`. Usunięcie `ImGuiKey` i `ImVec2` z tych plików automatycznie usunie tę zależność.

---

### A-4 `RendererLayer.cpp` zawiera `ParseRendererShortcutKey()` — funkcję UI (KRYTYCZNE)

**Plik:** `src/Renderer/RendererLayer.cpp:113-158`

```cpp
// PROBLEM — Renderer parsuje stringi na klucze ImGui
[[nodiscard]] ImGuiKey ParseRendererShortcutKey(std::string_view token, ImGuiKey fallback)
{
    // ... 25 linii if/else zamieniające "A" → ImGuiKey_A, "Left" → ImGuiKey_LeftArrow ...
}
```

Ta funkcja:
1. Przyjmuje `std::string_view` (string z configu)
2. Zwraca `ImGuiKey` (typ ImGui-specyficzny)
3. Żyje w `RendererLayer` (warstwa Renderer)
4. Duplikuje logikę istniejącą w `KeyChord.cpp` (parsowanie nazw klawiszy)

Jest to dokładnie ten przypadek, który opisałeś: Renderer robi coś, co powinno iść przez system skrótów klawiaturowych. Funkcja ta istnieje tylko dlatego, że `RendererKeyboardShortcutSettings` przechowuje `ImGuiKey` zamiast `KeyCode`.

---

### A-5 `Events/EditorUiEvents.hpp` importuje `Presentation/UiConfig.hpp` (WYSOKIE)

**Plik:** `src/Events/EditorUiEvents.hpp:10`

```cpp
// PROBLEM — Events zna Presentation
#include "Presentation/UiConfig.hpp"   // ← UIConfig, AppearanceConfig to typy UI

struct ConfigPreviewRequested final : public BusEvent
{
    UIConfig ui;          // ← typ z Presentation
};

struct RuntimeConfigApplied final : public BusEvent
{
    UIConfig ui;
    AppearanceConfig appearance;   // ← typ z Presentation
};
```

`Events` to warstwa niższa od `Presentation`. Zdarzenia powinny być wyrażone w typach domenowych lub co najwyżej w typach z `App` (jak `ApplicationConfig`). `UIConfig` i `AppearanceConfig` są typami konfiguracyjnymi — powinny żyć w `App/` (lub w dedykowanym `Core/Configuration/`), nie w `Presentation/`.

**Efekt:** każdy subskrybent tych zdarzeń, włącznie z tymi z `Core` lub `Domain`, musiałby zależeć od `Presentation`.

---

### A-6 `Core/JobSystem/JobSystem.cpp` importuje `ScientificRuntime/Python/PythonGilScope.hpp` (KRYTYCZNE)

**Plik:** `src/Core/JobSystem/JobSystem.cpp:11`

```cpp
// PROBLEM — Core zależy od ScientificRuntime
#include "ScientificRuntime/Python/PythonGilScope.hpp"
```

Użycie w `runJob()`:

```cpp
Ref<PythonGilAcquireScope> pythonGilScope;
if (job->UsesPythonRuntime())
    pythonGilScope = CreateRef<PythonGilAcquireScope>();
```

GIL (Global Interpreter Lock) to koncepcja specyficzna dla CPython. `JobSystem` jest częścią `Core` i ma być wielokrotnego użytku niezależnie od tego czy aplikacja używa Pythona. Umieszczenie obsługi GIL-a w `Core` oznacza, że **każda aplikacja zbudowana na tym Core musi linkować CPython**, nawet jeśli w ogóle nie używa Pythona.

Dodatkowo `IJob::UsesPythonRuntime()` w `Core/JobSystem/JobSystemTypes.hpp:113` to wirtualna metoda w klasie bazowej — co oznacza że **każde zadanie (IJob) w całym systemie ma interfejs "czy używasz Pythona?"**, co jest absurdem architektonicznym.

---

### A-7 `Renderer/RendererPythonLoader.cpp` importuje `ScientificRuntime/PymatgenBridge.hpp` (WYSOKIE)

**Plik:** `src/Renderer/RendererPythonLoader.cpp:16`

```cpp
// PROBLEM — Renderer zna pymatgen bezpośrednio
#include "ScientificRuntime/Python/PymatgenBridge.hpp"
```

`RendererPythonLoader` buduje `RendererStructureData` z `PymatgenStructureData`. Oznacza to, że Renderer zna szczegóły implementacyjne ScientificRuntime (strukturę danych `PymatgenStructureSite`). Poprawna ścieżka: `ScientificRuntime` produkuje `Domain::Crystal::Structure`, `IO` lub `App` konwertuje to do `RendererStructureData` i wysyła przez event.

Komentarz w kodzie (`// TODO(T07): Konwersja Domain::Structure -> RendererStructureData`) potwierdza, że to tymczasowe rozwiązanie — które jest nadal na swoim miejscu.

---

### A-8 `App/ApplicationState.hpp` importuje `Presentation/UiConfig.hpp` (ŚREDNIE)

**Plik:** `src/App/ApplicationState.hpp:15`

```cpp
#include "Presentation/UiConfig.hpp"   // UIConfig, AppearanceConfig, ApplicationPaths
```

`ApplicationConfig` (struct w `App/`) zawiera `UIConfig ui` i `AppearanceConfig appearance`, których definicje są w `Presentation/`. `App` leży nad `Presentation` w hierarchii zależności — zależność w drugą stronę jest akceptowalna — ale problem polega na tym, że typy `UIConfig`/`AppearanceConfig` są "logicznie" typami konfiguracyjnymi (nie mają metod ImGui, nie są to widgety) a fizycznie żyją w folderze `Presentation`. Powinny zostać przeniesione do `App/` lub `Core/Configuration/` tak aby `Events` i `App` mogły je includować bez zależności od `Presentation`.

---

## 4. Kategoria B — Reimplementacje istniejących systemów

### B-1 `ParseRendererShortcutKey()` jest ręczną reimplementacją `KeyCode` + `KeymapResolver` (KRYTYCZNE)

**Pliki:**
- `src/Renderer/RendererLayer.cpp:113` — reimplementacja parsowania
- `src/Core/Utils/KeyCodes.hpp` — istniejący `enum class KeyCode`
- `src/Core/Input/KeyChord.cpp` — istniejący `std::string ToString(KeyCode)`
- `src/Core/Input/KeymapResolver.hpp` — istniejące rozwiązywanie klawiszy → komend

Funkcja `ParseRendererShortcutKey` implementuje mapowanie `"A"→ImGuiKey_A`, `"Left"→ImGuiKey_LeftArrow` itd. Ten sam problem jest rozwiązany przez `KeyCode` (platforma-neutralny enum) i `KeyChord::ToString()`.

Poprawna architektura:
1. Konfiguracja przechowuje `"A"`, `"Left"` (stringi — już tak jest w `RendererShortcutConfig`)
2. Przy starcie/zmianie configu: stringi → `KeyCode` przez istniejący parser w `Core/Input/`
3. `KeymapResolver` rejestruje binding: `KeyChord{KeyCode::A}` → `CommandID("renderer.align_axis_a")`
4. `RendererPanelInput` odpytuje `KeyInputProcessor` — który zwraca `CommandID`
5. `RendererPanel` emituje odpowiednie zdarzenie na EventBus

Funkcja `ParseRendererShortcutKey` + cała struktura `RendererKeyboardShortcutSettings` z `ImGuiKey` **znika całkowicie**.

---

### B-2 `RendererKeyboardShortcutSettings` i `RendererShortcutConfig` to podwójna reprezentacja skrótów (KRYTYCZNE)

**Pliki:**
- `src/App/ApplicationState.hpp:113` — `RendererShortcutConfig` (stringi, dla IO)
- `src/Renderer/RendererSettings.hpp:61` — `RendererKeyboardShortcutSettings` (ImGuiKey, dla polling)

Każdy skrót klawiaturowy renderera istnieje **w dwóch miejscach jednocześnie**: jako string w `RendererShortcutConfig` (serializowany do YAML) i jako `ImGuiKey` w `RendererKeyboardShortcutSettings` (używany w polling). `ParseRendererShortcutKey()` to klej między nimi.

Poprawnie: jeden punkt prawdy to `KeymapResolver` — skróty rejestruje się raz przy inicjalizacji/zmianie configu, a Presentation pyta resolver o aktywne komendy. Nie potrzeba żadnej z tych dwóch struktur.

---

### B-3 Lokalny undo/redo widoku renderera duplikuje `UndoStack` (KRYTYCZNE)

**Pliki:**
- `src/Renderer/RendererWindowState.hpp:59-60` — `viewUndoHistory`, `viewRedoHistory`
- `src/Renderer/RendererLayer.cpp` — `BeginViewInteraction`, `CommitViewInteraction`, `UndoViewChange`, `RedoViewChange`, `pushViewChange`
- `src/Renderer/Commands/SetCameraViewCommand.hpp` — **istniejąca, kompletna implementacja ICommand dla zmiany widoku** — NIGDY NIEUŻYWANA
- `src/Core/Undo/UndoStack.hpp` — globalny `UndoStack` z `BeginGroup`/`EndGroup`, `CanMerge` — NIEUŻYWANY dla widoku

`SetCameraViewCommand` implementuje `ICommand` z pełnym `Execute/Undo/Redo` i **`CanMerge`** (który scala kolejne małe zmiany w jeden rekord — idealne dla ciągłego orbit). Klasa ta czeka na użycie, podczas gdy `RendererLayer` reinplementuje całe undo/redo ręcznie jako dwa wektory `RendererViewStateChange`.

**Skala duplikacji:** ~120 linii w `RendererLayer.cpp` + pola w każdym `RendererWindowState` implementuje to, co `UndoStack` + `SetCameraViewCommand` już daje za darmo.

---

### B-4 `ComputeCameraTransitionDurationSeconds()` istnieje w 3 miejscach (ŚREDNIE)

**Pliki:**

```cpp
// src/Renderer/RendererLayer.cpp:95 — kopia 1
float ComputeCameraTransitionDurationSeconds(float rotationSpeed) { ... }

// src/Presentation/Panels/RendererPanelInput.cpp:41 — kopia 2
float ComputeCameraTransitionDurationSeconds(float rotationSpeed) { ... }

// src/Presentation/Panels/RendererPanelToolbar.cpp:73 — kopia 3
float ComputeCameraTransitionDurationSeconds(float rotationSpeed) { ... }
```

Identyczna funkcja (te same stałe `0.14f`, `0.02f`, `0.50f`) w trzech plikach. Należy do `RendererViewCamera` lub do dedykowanego `RendererUtils.hpp`.

---

### B-5 `BuildCellEdges()` i `BuildBonds()` zduplikowane w loaderach POSCAR i Python (ŚREDNIE)

**Pliki:**
- `src/Renderer/RendererPoscarLoader.cpp` — `BuildCellEdges`, `BuildBonds`, `GridCellKey`, `GridCellKeyHasher`
- `src/Renderer/RendererPythonLoader.cpp` — `BuildCellEdges`, `BuildBonds`, `RendererPythonGridCellKey`, `RendererPythonGridCellKeyHasher`

Algorytm budowania krawędzi komórki (`BuildCellEdges`) i algorytm siatki przestrzennej do bondów (`BuildBonds` ze spatial hashing) to **identyczne implementacje** z różnymi nazwami typów pomocniczych. Obie klasy grid-cell-key mają identyczną implementację `operator==` i hashera.

Ta duplikacja jest konsekwencją faktu, że obydwa loadery żyją w `Renderer` zamiast przejść przez wspólną konwersję w dedykowanym miejscu (np. `IO/StructureToRenderer.cpp` lub jako metoda na `Domain::Structure`).

---

### B-6 `RendererPanelInput.cpp` zawiera częściową logikę orkiestracji interakcji (WYSOKIE)

Funkcja `applyViewportKeyboardNavigation()` w `RendererPanelInput.cpp` nie tylko czyta input (co byłoby poprawne dla Presentation), ale też:

1. Bezpośrednio woła `m_Layer.BeginViewInteraction()`, `m_Layer.CommitViewInteraction()`, `m_Layer.UndoViewChange()`, `m_Layer.RedoViewChange()` — **12+ bezpośrednich wywołań** zamiast zdarzeń
2. Czyta i modyfikuje `windowState.transitionDuration`, `windowState.transitionActive` itd. — **zarządza stanem wewnętrznym Renderera**
3. Woła `startCameraTransition()` (metoda RendererPanel) która **kopiuje całą logikę transition** istniejącą w `RendererLayer::restoreViewSnapshot()`

Mouse input (orbit/pan/zoom przez wheel) jest poprawnie wysyłany przez `EventBus` (`OrbitDelta`, `PanDelta`, `ZoomDelta`). Keyboard input całkowicie to omija i woła metodę warstwy bezpośrednio. Ta niespójność jest szczególnie myląca.

---

## 5. Kategoria C — Serializacja i IO poza warstwą IO

### C-1 Parser POSCAR w `Renderer/RendererPoscarLoader.cpp` (KRYTYCZNE)

**Plik:** `src/Renderer/RendererPoscarLoader.cpp`

```cpp
// PROBLEM — Renderer czyta pliki i parsuje format domenowy
[[nodiscard]] static Result<std::string> LoadUtf8Text(const Path &filePath)
{
    std::ifstream stream(filePath.Native());   // ← bezpośredni dostęp do dysku
    // ...
}

Result<RendererStructureData> LoadRendererStructureFromPoscar(...)
{
    // ... pełny parser POSCAR: lattice, elements, counts, coordinates ...
}
```

Komentarz w kodzie to przyznaje: `"Renderer quick-test POSCAR could not be opened"` — to był "quick test", który stał się trwałym rozwiązaniem.

**Problemy:**
1. `Renderer` otwiera pliki — to zadanie `IOLayer`
2. `Renderer` parsuje format kryształu (POSCAR) — to zadanie `IO/` (domeny)
3. Parser POSCAR to duży, samodzielny moduł (227 linii) — nie powinien być ukryty w Renderer

**Poprawna ścieżka:** `IO/PoscarIO.cpp` wczytuje plik → zwraca `Domain::Crystal::Structure` → konwerter (`IO/` lub `App/`) buduje `RendererStructureData` → event na `EventBus` dociera do `RendererLayer`.

---

### C-2 Log CSV zapisywany bezpośrednio w `LoggingPanel::exportEntriesToCsv()` (WYSOKIE)

**Plik:** `src/Presentation/Panels/LoggingPanel.cpp:231`

```cpp
// PROBLEM — Panel UI otwiera plik bezpośrednio
bool LoggingPanel::exportEntriesToCsv(const std::string &path) const
{
    std::ofstream out(path, std::ios::out | std::ios::trunc);  // ← bezpośredni zapis
    // ...
}
```

Wywołanie:
```cpp
// LoggingPanel.cpp ~135 - hardkodowana ścieżka
const bool ok = exportEntriesToCsv("logs/event-log-export.csv");
```

**Problemy:**
1. Panel UI otwiera `std::ofstream` — to zadanie IOLayer
2. Ścieżka `"logs/event-log-export.csv"` jest hardkodowana w panelu — brak konfigurowalności
3. Brak obsługi błędów poza `bool` — IOLayer zwróciłby `Result<void>`
4. Brak integracji z `ApplicationPaths` — nie wiadomo gdzie faktycznie ląduje plik

**Poprawnie:** Panel emituje `ExportLogsRequested{entries, targetPath}` na EventBus. `IOLayer` subskrybuje, zapisuje plik, emituje `LogsExported` lub `LogsExportFailed`.

---

### C-3 Shader files czytane bezpośrednio w `OpenGlShaderLibrary` (NISKIE / borderline)

**Plik:** `src/Renderer/OpenGl/OpenGlShaderLibrary.cpp:235`

```cpp
std::ifstream stream(path.Native());  // ← bezpośredni odczyt shaderów
```

To borderline case — shadery są wewnętrznym zasobem renderera (nie danymi domenowymi). Jednakże dla spójności z resztą architektury i testowalności (mock IOLayer w testach), powinno używać `TextFileIO`. Niski priorytet, ale wart odnotowania.

---

### C-4 `ScriptRunner` używa `std::system()` i ręcznego IO (WYSOKIE)

**Plik:** `src/ScientificRuntime/Python/ScriptRunner.cpp`

```cpp
// Bezpośredni odczyt pliku
static std::string readTextFile(const Path &filePath) {
    std::ifstream stream(filePath.Native(), std::ios::binary);  // ← bezpośrednie IO
    // ...
}

// Wywołanie systemu — blokujące, bez obsługi błędów subprocess
result.exitCode = std::system(shellCommand.c_str());  // ← std::system!
```

`std::system()` to szczególnie poważny problem:
- **Blokuje wątek wywołujący** — przy wywołaniu z UI oznacza zawieszenie interfejsu
- **Brak kontroli nad procesem potomnym** — nie można go zatrzymać, nie ma PID, brak strumieni
- **Brak obsługi błędów** — zwraca tylko exit code
- **Zależność od powłoki systemowej** — zachowanie różne na Windows/Linux/macOS
- **Ryzyko bezpieczeństwa** — przy niedostatecznej sanityzacji argumentów

`ScriptRunner` jest wywoływany z `JobSystem` (asynchronicznie), więc blokowanie jest mniejszym problemem niż gdyby było w UI — ale `std::system` nadal jest nieakceptowalne w kodzie produkcyjnym.

---

## 6. Kategoria D — Systemy zdefiniowane, ale nieużywane lub używane poniżej możliwości

### D-1 `SetCameraViewCommand` — kompletna implementacja ICommand, nigdy nieużyta (KRYTYCZNE)

**Plik:** `src/Renderer/Commands/SetCameraViewCommand.hpp/cpp`

`SetCameraViewCommand` implementuje pełny `ICommand` z:
- `Execute()` — przywraca `newView`
- `Undo()` — przywraca `oldView`
- `Redo()` — redone przez delegate do `Execute`
- `CanMerge()` — scala kolejne małe zmiany (ciągły orbit → jeden rekord undo)
- `Merge()` — implementacja scalania

Jest to **dokładnie** to, czego potrzebuje system undo widoku renderera. Jednak:

```cpp
// src/Renderer/RendererWindowState.hpp — RÓWNOLEGŁY, RĘCZNY system undo
std::vector<RendererViewStateChange> viewUndoHistory;  // ← zamiast UndoStack
std::vector<RendererViewStateChange> viewRedoHistory;  // ← zamiast UndoStack
```

`SetCameraViewCommand` nie jest tworzony **nigdzie** w kodzie aplikacji. `UndoStack` nie jest używany **nigdzie** dla operacji renderera. Cały system komend i globalny undo stack są kompletnie ominięte dla widoku.

---

### D-2 `KeymapResolver` / `KeyInputProcessor` / `ContextManager` — pominięte dla skrótów renderera (KRYTYCZNE)

**Pliki:**
- `src/Core/Input/KeymapResolver.hpp` — kompletny resolver bindingów
- `src/Core/Input/KeyInputProcessor.hpp` — kompletny procesor inputu
- `src/Core/Input/ContextManager.hpp` — kompletny manager kontekstu

Skróty klawiaturowe renderera całkowicie omijają te systemy:

```cpp
// src/Presentation/Panels/RendererPanelInput.cpp — bezpośredni polling ImGui
const auto shortcutPressed = [](ImGuiKey key) -> bool
{
    return key != ImGuiKey_None && ImGui::IsKeyPressed(key);  // ← bezpośredni polling
};

if (shortcutPressed(shortcuts.alignAxisA))    // ← ImGuiKey zamiast CommandID
    alignToAxis(lattice[0]);
```

Poprawnie `RendererPanelInput` powinien:
1. W każdej klatce dostarczyć aktywny `KeyChord` do `KeyInputProcessor`
2. Otrzymać `KeyInputResult` z `CommandID`
3. Na podstawie `CommandID` podjąć akcję (lub wyemitować zdarzenie)

Ominięcie `KeymapResolver` sprawia, że skróty renderera nie są widoczne w panelu `KeyBindings` w `SettingsPanel`, nie mogą być rebind-owane przez użytkownika runtime, i nie respektują kontekstu (np. `ContextManager` nie wie, że viewport jest aktywny).

---

### D-3 `SettingsPanel` — lokalny undo z `ApplicationConfig` (OK z zastrzeżeniem, wymaga dokumentacji)

**Plik:** `src/Presentation/Panels/SettingsPanel.hpp`

```cpp
std::vector<ApplicationConfig> m_UndoHistory;
std::vector<ApplicationConfig> m_RedoHistory;
std::optional<ApplicationConfig> m_PendingUndoSnapshot;
```

To **lokalny draft undo** dla panelu ustawień — przechowuje pełne snapshoty configu przed każdą zmianą w edytorze. Jest to inna semantyka niż `UndoStack` (który operuje na `ICommand` i obsługuje undo na poziomie całej aplikacji). Lokalny undo w Settings Panel jest uzasadniony (użytkownik może "cofnąć" zmiany w panelu przed ich zatwierdzeniem).

**Zastrzeżenie:** To lokalne undo nie jest podpięte pod `UndoStack` — co oznacza, że Ctrl+Z w głównym oknie nie cofa zmian w ustawieniach. Wymaga jawnej dokumentacji jako "local draft undo, not wired to global UndoStack" aby przyszli programiści nie próbowali go "naprawić".

---

## 7. Plan naprawczy — priorytety i kolejność

Każda naprawa otrzymuje etykietę priorytetu i zestaw plików do zmiany.

| #   | ID        | Opis                                                                 | Priorytet    | Zależności |
| --- | --------- | -------------------------------------------------------------------- | ------------ | ---------- |
| 1   | FIX-A3    | Usuń `PythonGilScope` z `Core/JobSystem`                             | P0-BLOKUJĄCE | —          |
| 2   | FIX-A1/A2 | Usuń `ImVec2` i `ImGuiKey` z `Renderer/`                             | P0-BLOKUJĄCE | —          |
| 3   | FIX-B1/B2 | Usuń `ParseRendererShortcutKey` + `RendererKeyboardShortcutSettings` | P0-BLOKUJĄCE | FIX-A1     |
| 4   | FIX-D2    | Podłącz skróty renderera pod `KeymapResolver`                        | P0-BLOKUJĄCE | FIX-B1/B2  |
| 5   | FIX-A5/A8 | Przenieś `UIConfig`/`AppearanceConfig` z `Presentation/` do `App/`   | P1-WYSOKIE   | —          |
| 6   | FIX-D1/B3 | Usuń lokalny undo widoku, użyj `SetCameraViewCommand` + `UndoStack`  | P1-WYSOKIE   | FIX-D2     |
| 7   | FIX-C1    | Przenieś `RendererPoscarLoader` do `IO/PoscarIO`                     | P1-WYSOKIE   | FIX-A7     |
| 8   | FIX-A7    | Odetnij `Renderer` od `ScientificRuntime/PymatgenBridge`             | P1-WYSOKIE   | FIX-C1     |
| 9   | FIX-B6    | Zmień keyboard path w `RendererPanelInput` na eventy                 | P1-WYSOKIE   | FIX-D2     |
| 10  | FIX-C2    | Przepnij `LoggingPanel::exportEntriesToCsv` przez `IOLayer`          | P2-ŚREDNIE   | —          |
| 11  | FIX-B4    | Deduplikacja `ComputeCameraTransitionDurationSeconds`                | P2-ŚREDNIE   | —          |
| 12  | FIX-B5    | Deduplikacja `BuildCellEdges` + `BuildBonds`                         | P2-ŚREDNIE   | FIX-C1     |
| 13  | FIX-C4    | Zastąp `std::system()` w ScriptRunner właściwym subprocess API       | P2-ŚREDNIE   | —          |
| 14  | FIX-C3    | Użyj `TextFileIO` w `OpenGlShaderLibrary`                            | P3-NISKIE    | —          |
| 15  | DOC-B6    | Udokumentuj lokalny undo Settings Panel jako celowy                  | P3-NISKIE    | —          |

---

## 8. Szczegółowe zadania naprawcze

---

### FIX-A3 — Usuń `PythonGilScope` z `Core/JobSystem`

**Problem:** `Core/JobSystem/JobSystem.cpp` importuje `ScientificRuntime/Python/PythonGilScope.hpp` i wywołuje `job->UsesPythonRuntime()` z `IJob`.

**Rozwiązanie w dwóch krokach:**

**Krok 1 — Odwróć zależność przez politykę (policy/callback):**

W `Core/JobSystem/JobSystemTypes.hpp` zastąp `UsesPythonRuntime()`:
```cpp
// PRZED
class IJob {
    [[nodiscard]] virtual bool UsesPythonRuntime() const { return false; }
    // ...
};

// PO — brak jakiejkolwiek wzmianki o Pythonie w Core
class IJob {
    // UsesPythonRuntime() USUNIĘTE
    // ...
};
```

**Krok 2 — Przenieś akwizycję GIL do `ScientificRuntime`:**

Stwórz `JobPreExecuteHook` (lub użyj `std::function`) który `ScientificRuntime` może zarejestrować w `JobSystem`:

```cpp
// Core/JobSystem/JobSystem.hpp
class JobSystem {
public:
    // Rejestracja hooka uruchamianego przed Execute() każdego joba
    using PreExecuteHook = std::function<std::unique_ptr<IJobExecutionGuard>(const IJob &job)>;
    void RegisterPreExecuteHook(PreExecuteHook hook);
    // ...
};

// ScientificRuntime rejestruje GIL hook przy inicjalizacji
jobSystem->RegisterPreExecuteHook([](const IJob &job) -> std::unique_ptr<IJobExecutionGuard>
{
    if (job.UsesPythonRuntime())           // UsesPythonRuntime żyje TUTAJ, w ScientificRuntime
        return std::make_unique<PythonGilAcquireScope>();
    return nullptr;
});
```

Alternatywnie prostsze: `JobSystem::Submit` przyjmuje opcjonalny `JobExecutionPolicy` który ScientificRuntime może uzupełnić:

```cpp
jobSystem->Submit(pythonJob, priority, PythonJobPolicy{});  // policy akwiruje GIL
```

**Pliki do zmiany:**
- `src/Core/JobSystem/JobSystem.hpp` — dodaj `RegisterPreExecuteHook` lub `JobExecutionPolicy`
- `src/Core/JobSystem/JobSystem.cpp` — usuń `#include "ScientificRuntime/Python/PythonGilScope.hpp"`, użyj hooka
- `src/Core/JobSystem/JobSystemTypes.hpp` — usuń `virtual bool UsesPythonRuntime()`
- `src/ScientificRuntime/Python/PythonScriptJob.hpp/.cpp` — zachowaj `UsesPythonRuntime()` lokalnie, zarejestruj hook
- `src/ScientificRuntime/ScientificRuntimeLayer.cpp` — rejestruj hook przy `OnAttach`

---

### FIX-A1/A2 — Usuń `ImVec2` i `ImGuiKey` z `Renderer/`

**Problem:** `RendererSettings.hpp` i `RendererWindowState.hpp` importują `<imgui.h>`.

**Krok 1 — Zastąp `ImGuiKey` przez `KeyCode` w `RendererKeyboardShortcutSettings`:**

```cpp
// src/Renderer/RendererSettings.hpp — PRZED
#include <imgui.h>

struct RendererKeyboardShortcutSettings {
    ImGuiKey alignAxisA = ImGuiKey_A;
    // ...
};
```

Po FIX-B1/B2 ta struktura znika całkowicie. Skróty żyją w `KeymapResolver`.

**Krok 2 — Zastąp `ImVec2` przez `glm::vec2` w `RendererWindowState`:**

```cpp
// src/Renderer/RendererWindowState.hpp — PRZED
#include <imgui.h>
struct RendererWindowState {
    ImVec2 viewportSize = ImVec2(640.0f, 480.0f);
    ImVec2 lastMousePosition = ImVec2(0.0f, 0.0f);
};

// PO
#include <glm/glm.hpp>
struct RendererWindowState {
    glm::vec2 viewportSize = glm::vec2(640.0f, 480.0f);
    // lastMousePosition USUNIĘTE — przeniesione do RendererPanelInput (Presentation)
};
```

`lastMousePosition` jest stanem interakcji z myszą — należy do `RendererPanelInput` w `Presentation`, a nie do `RendererWindowState` w `Renderer`. Przenieś go jako lokalny member `RendererPanel` lub wewnątrz `applyViewportInputNavigation`.

**Krok 3 — Zaktualizuj wszystkie użycia `ImVec2` w `RendererPanel*.cpp`:**

W `Presentation/Panels/RendererPanel.cpp` / `RendererPanelInput.cpp` konwertuj między `ImVec2` a `glm::vec2` na granicy Presentation↔Renderer:

```cpp
// Presentation/Panels/RendererPanel.cpp — lokalna konwersja
ImVec2 imguiSize = ImGui::GetContentRegionAvail();
windowState.viewportSize = glm::vec2(imguiSize.x, imguiSize.y);  // konwersja na granicy
```

**Pliki do zmiany:**
- `src/Renderer/RendererSettings.hpp` — usuń `#include <imgui.h>`, usuń `ImGuiKey` (po FIX-B1)
- `src/Renderer/RendererWindowState.hpp` — zamień `ImVec2` → `glm::vec2`, usuń `lastMousePosition`
- `src/Renderer/OpenGl/OpenGlRendererBackend.hpp` — usunięcie `imgui.h` nastąpi automatycznie
- `src/Presentation/Panels/RendererPanel.cpp` / `RendererPanelInput.cpp` / `RendererPanelToolbar.cpp` — zaktualizuj typy + konwersje na granicy

---

### FIX-B1/B2 + FIX-D2 — Usuń `ParseRendererShortcutKey`, podłącz `KeymapResolver`

To największa zmiana, ale kluczowa dla spójności systemu. Poniżej pełna sekwencja:

**Krok 1 — Usuń `RendererKeyboardShortcutSettings` z `RendererSettings.hpp`:**

```cpp
// USUŃ całą tę strukturę
struct RendererKeyboardShortcutSettings { /* ... ImGuiKey fields ... */ };

// USUŃ z RendererGlobalRenderSettings
struct RendererGlobalRenderSettings {
    // shortcuts USUNIĘTE
};
```

**Krok 2 — Usuń `ParseRendererShortcutKey` z `RendererLayer.cpp`:**

```cpp
// USUŃ całą tę funkcję z RendererLayer.cpp:113
[[nodiscard]] ImGuiKey ParseRendererShortcutKey(...) { /* ... */ }
```

Usuń też wszystkie wywołania w `ApplyConfig()`:
```cpp
// USUŃ ~14 wywołań ParseRendererShortcutKey w ApplyConfig()
m_GlobalRenderSettings.shortcuts.alignAxisA = ParseRendererShortcutKey(...);
// itd.
```

**Krok 3 — Stwórz zdarzenia domenowe dla akcji renderera:**

```cpp
// src/Events/RendererEvents.hpp — dodaj nowe zdarzenia
namespace RendererEvents::Viewport
{
    struct AlignToAxisRequested : BusEvent { std::string windowId; int axis; }; // 0=A,1=B,2=C
    struct OrbitStepRequested   : BusEvent { std::string windowId; float dx; float dy; };
    struct RollStepRequested    : BusEvent { std::string windowId; float delta; };
    struct ZoomStepRequested    : BusEvent { std::string windowId; float amount; };
    struct FocusSelectedAtomRequested : BusEvent { std::string windowId; };
    struct UndoViewRequested    : BusEvent { std::string windowId; };
    struct RedoViewRequested    : BusEvent { std::string windowId; };
}
```

**Krok 4 — Zarejestruj bindingi w `KeymapResolver` przy starcie (np. w `RendererLayer::OnAttach` lub `ApplicationBootstrap`):**

```cpp
// App/ApplicationBootstrap.cpp lub RendererLayer::OnAttach — po otrzymaniu configu
void registerRendererKeyBindings(
    KeymapResolver &resolver,
    const RendererShortcutConfig &shortcuts)  // stringi z configu
{
    auto parseKey = [](const std::string &name) -> KeyCode {
        return KeyBinding::ParseKeyCode(name);  // istniejący parser w Core/Input/KeyBinding.cpp
    };

    resolver.RegisterBinding({
        .id = "renderer.align_axis_a",
        .chord = KeyChord{ parseKey(shortcuts.alignAxisA) },
        .commandId = CommandID("renderer.align_axis_a"),
        .when = ContextExpr("renderer.viewport.focused"),
        .layer = KeymapLayer::WindowLocal,
    });
    // ... pozostałe bindingi ...
}
```

**Krok 5 — `RendererPanelInput` odpytuje `KeyInputProcessor`, emituje zdarzenia:**

```cpp
// src/Presentation/Panels/RendererPanelInput.cpp — NOWA logika
void RendererPanel::applyViewportKeyboardNavigation(RendererWindowState &windowState)
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;

    // Poinformuj ContextManager że ten viewport jest aktywny
    m_ContextManager->SetContext("renderer.viewport.focused", true);
    m_ContextManager->SetContext("renderer.viewport.window_id", windowState.windowId);

    // Zbierz aktywne klawisze → KeyChord
    // (np. przez dedykowaną funkcję ImGuiKeyChordPoller w Presentation)
    auto chord = pollActiveKeyChord();  // zwraca std::optional<KeyChord>
    if (!chord.has_value()) return;

    // Zapytaj resolver
    auto result = m_KeyInputProcessor->HandleKeyPressed(*chord);
    if (!result.HasValue() || !result.Value().handled) return;

    // Na podstawie CommandID emituj zdarzenie
    const CommandID cmd = *result.Value().commandId;
    if (cmd == "renderer.align_axis_a")
        m_EventBus->Publish(RendererEvents::Viewport::AlignToAxisRequested{windowState.windowId, 0});
    else if (cmd == "renderer.orbit_left")
        m_EventBus->Publish(RendererEvents::Viewport::OrbitStepRequested{windowState.windowId, +delta, 0});
    // ... itd. ...
}
```

**Krok 6 — `RendererLayer` subskrybuje nowe zdarzenia:**

```cpp
// src/Renderer/RendererLayer.cpp — w bindConfigEvents() lub OnAttach()
AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::AlignToAxisRequested>(
    std::bind_front(&RendererLayer::onAlignToAxisRequested, this)));
// ... pozostałe ...
```

**Pliki do zmiany:**
- `src/Renderer/RendererSettings.hpp` — usuń `RendererKeyboardShortcutSettings` + `shortcuts` member
- `src/Renderer/RendererLayer.cpp` — usuń `ParseRendererShortcutKey`, usuń blok shortcuts w `ApplyConfig`
- `src/Renderer/RendererLayer.hpp` — dodaj subskrypcje nowych zdarzeń
- `src/Events/RendererEvents.hpp` — dodaj nowe event types
- `src/Presentation/Panels/RendererPanelInput.cpp` — przepisz `applyViewportKeyboardNavigation`
- `src/App/ApplicationBootstrap.cpp` — rejestracja bindingów przy starcie i przy zmianie configu
- `src/Core/Input/KeyBinding.cpp` — upewnij się że `ParseKeyCode(string)` jest publiczne i pokrywa wszystkie klawisze

---

### FIX-D1/B3 — Zastąp lokalny undo widoku `SetCameraViewCommand` + `UndoStack`

**Krok 1 — Usuń lokalny undo z `RendererWindowState`:**

```cpp
// src/Renderer/RendererWindowState.hpp — USUŃ
std::vector<RendererViewStateChange> viewUndoHistory;   // ← USUŃ
std::vector<RendererViewStateChange> viewRedoHistory;   // ← USUŃ
bool viewInteractionActive = false;                      // ← USUŃ
std::string viewInteractionSource;                       // ← USUŃ
RendererViewSnapshot viewInteractionStart;               // ← USUŃ
```

**Krok 2 — Usuń z `RendererLayer.hpp` i `.cpp`:**

Usuń metody:
- `BeginViewInteraction` / `CommitViewInteraction` / `CancelViewInteraction`
- `UndoViewChange` / `RedoViewChange`
- `captureViewSnapshot` / `restoreViewSnapshot` / `pushViewChange`

**Krok 3 — Przekaż `UndoStack` do `RendererLayer` lub używaj przez eventy:**

Opcja A — `RendererLayer` dostaje `Ref<UndoStack>`:
```cpp
// src/Renderer/RendererLayer.hpp
class RendererLayer : public Layer {
public:
    void BindUndoStack(Ref<UndoStack> undoStack);
private:
    Ref<UndoStack> m_UndoStack;
};
```

Opcja B — przez eventy (preferowane dla luźniejszego sprzężenia):
```cpp
// RendererPanelInput emituje UndoViewRequested / RedoViewRequested
// App/ApplicationBootstrap subskrybuje i woła m_UndoStack->Undo()/Redo()
```

**Krok 4 — Użyj `SetCameraViewCommand` przy każdej zmianie widoku:**

```cpp
// src/Renderer/RendererLayer.cpp — np. w onOrbitDelta()
void RendererLayer::onOrbitDelta(const RendererEvents::Viewport::OrbitDelta &event)
{
    RendererWindowState *windowState = findWindowById(event.windowId);
    if (!windowState || !windowState->camera) return;

    const RendererViewSnapshot before = captureViewSnapshot(*windowState);
    windowState->camera->Orbit(event.dx, event.dy);
    const RendererViewSnapshot after = captureViewSnapshot(*windowState);

    // NOWE: push przez UndoStack + SetCameraViewCommand
    auto command = std::make_unique<SetCameraViewCommand>(before, after, "orbit", event.windowId);
    m_UndoStack->PushExecuted(std::move(command));
    // SetCameraViewCommand::CanMerge() automatycznie scala kolejne orbit w jeden rekord
}
```

**Pliki do zmiany:**
- `src/Renderer/RendererWindowState.hpp` — usuń undo fields
- `src/Renderer/RendererLayer.hpp/.cpp` — usuń undo metody, dodaj `UndoStack`, zaktualizuj event handlers
- `src/Renderer/Commands/SetCameraViewCommand.cpp` — upewnij się że `Execute` i `Undo` faktycznie zmieniają stan kamery (wymaga dostępu do `RendererLayer` lub przez CommandContext)
- `src/Presentation/Panels/RendererPanelInput.cpp` — usuń wywołania `m_Layer.BeginViewInteraction` etc., zastąp zdarzeniami `UndoViewRequested`/`RedoViewRequested`
- `src/App/ApplicationBootstrap.cpp` — podłącz `UndoStack` do `RendererLayer`

---

### FIX-A5/A8 — Przenieś `UIConfig`/`AppearanceConfig` z `Presentation/` do `App/`

**Problem:** `Events/EditorUiEvents.hpp` i `App/ApplicationState.hpp` includują `Presentation/UiConfig.hpp`.

**Rozwiązanie:**

```bash
# Przenieś fizycznie plik
mv src/Presentation/UiConfig.hpp src/App/UiConfig.hpp
```

Zaktualizuj wszystkie include paths:
```cpp
// WSZĘDZIE PRZED:
#include "Presentation/UiConfig.hpp"

// PO:
#include "App/UiConfig.hpp"
```

`UIConfig` i `AppearanceConfig` to POD structs bez żadnych zależności od ImGui — przeniesienie ich do `App/` jest trywialne i usuwa odwróconą zależność `Events → Presentation`.

**Pliki do zmiany:**
- `src/Presentation/UiConfig.hpp` → `src/App/UiConfig.hpp` (przeniesienie)
- `src/Events/EditorUiEvents.hpp`
- `src/App/ApplicationState.hpp`
- `src/Presentation/EditorUiState.hpp`
- `src/Presentation/ImGuiLayer.hpp`
- `premake5.lua` (jeśli ręcznie lista plików)

---

### FIX-C1 — Przenieś `RendererPoscarLoader` do `IO/PoscarIO`

**Krok 1 — Stwórz `IO/PoscarIO.hpp/.cpp`:**

```cpp
// src/IO/PoscarIO.hpp
#pragma once
#include "Domain/Crystal/Structure.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::IO
{
    [[nodiscard]] Result<Domain::Crystal::Structure> LoadPoscar(const Path &filePath);
}
```

**Krok 2 — Przenieś logikę parsowania z `RendererPoscarLoader.cpp` do `PoscarIO.cpp`:**

Parser tokenizacji (`TokenizeLines`, `ParseFloatToken`, `ParseIntToken`, `ExpandElementList`) przenosi się bez zmian. Wynikiem jest `Domain::Crystal::Structure` zamiast `RendererStructureData`.

**Krok 3 — Stwórz konwerter `IO/StructureToRenderer.hpp/.cpp`:**

```cpp
// src/IO/StructureToRenderer.hpp
#include "Domain/Crystal/Structure.hpp"
#include "Renderer/RendererTypes.hpp"  // RendererStructureData
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio::IO
{
    [[nodiscard]] RendererStructureData ConvertToRenderer(
        const Domain::Crystal::Structure &structure,
        const AtomStyleTable &atomStyleTable,
        const ElementPropertiesTable &elementPropertiesTable);
}
```

Tutaj też `BuildCellEdges` i `BuildBonds` żyją — jeden raz (FIX-B5).

**Krok 4 — Usuń `src/Renderer/RendererPoscarLoader.cpp/.hpp`**

**Krok 5 — `RendererLayer` nie ładuje plików przy starcie — zamiast tego:**

```cpp
// src/Renderer/RendererStartupBootstrap.cpp — USUŃ ładowanie POSCAR
// Startup windows ładuje App/ApplicationBootstrap.cpp przez IOLayer + event
```

**Pliki do zmiany:**
- Nowe: `src/IO/PoscarIO.hpp/.cpp`
- Nowe: `src/IO/StructureToRenderer.hpp/.cpp`
- Usuń: `src/Renderer/RendererPoscarLoader.hpp/.cpp`
- `src/Renderer/RendererStartupBootstrap.cpp` — usuń dependency od loadera
- `src/Renderer/RendererLayer.cpp` — usuń `loadDefaultWindows` lub zastąp przez event
- `src/App/ApplicationBootstrap.cpp` — wczytaj struktury startowe przez `IOLayer`

---

### FIX-A7 — Odetnij `Renderer` od `ScientificRuntime/PymatgenBridge`

**Problem:** `RendererPythonLoader.cpp` importuje `PymatgenBridge.hpp` i operuje na `PymatgenStructureData`.

**Rozwiązanie:** Po FIX-C1 pojawi się `IO/StructureToRenderer::ConvertToRenderer`. `RendererPythonLoader` powinien:

1. Przyjmować `Domain::Crystal::Structure` (nie `PymatgenStructureData`)
2. Używać `IO::StructureToRenderer::ConvertToRenderer`
3. Konwersja `PymatgenStructureData` → `Domain::Crystal::Structure` odbywa się w `ScientificRuntime`

```cpp
// PRZED (Renderer zna pymatgen)
// src/Renderer/RendererPythonLoader.cpp
#include "ScientificRuntime/Python/PymatgenBridge.hpp"
RendererStructureData BuildRendererStructureFromPythonData(const PymatgenStructureData &...)

// PO (Renderer zna tylko Domain)
// src/Renderer/ — ta funkcja przenosi się do IO/
// ScientificRuntime/Python/PymatgenConversion.cpp
Domain::Crystal::Structure ConvertFromPymatgen(const PymatgenStructureData &data);
// następnie App lub IOLayer woła ConvertToRenderer()
```

**Pliki do zmiany:**
- `src/Renderer/RendererPythonLoader.cpp/.hpp` — usuń `PymatgenBridge.hpp`, przebuduj
- Nowe: `src/ScientificRuntime/Python/PymatgenConversion.hpp/.cpp` — `PymatgenStructureData` → `Domain::Structure`

---

### FIX-C2 — Przepnij `LoggingPanel::exportEntriesToCsv` przez `IOLayer`

**Krok 1 — Dodaj event:**

```cpp
// src/Events/ lub src/App/Events/ — nowy event
namespace AppEvents::Logs
{
    struct ExportRequested final : public BusEvent {
        std::vector<LogEntry> entries;
        Path targetPath;
    };
    struct ExportCompleted final : public BusEvent { Path targetPath; std::size_t bytes; };
    struct ExportFailed final : public BusEvent { Path targetPath; std::string error; };
}
```

**Krok 2 — `LoggingPanel` emituje event zamiast pisać plik:**

```cpp
// src/Presentation/Panels/LoggingPanel.cpp — PRZED
const bool ok = exportEntriesToCsv("logs/event-log-export.csv");

// PO
m_EventBus->Publish(AppEvents::Logs::ExportRequested{
    .entries = m_Entries,
    .targetPath = Path(m_ExportPath)  // z ustawień lub FileDialog
});
```

**Krok 3 — `IOLayer` subskrybuje i zapisuje:**

```cpp
// src/IO/IOLayer.cpp — nowa obsługa
AddSubscription(m_EventBus->Subscribe<AppEvents::Logs::ExportRequested>(
    [this](const auto &event) { handleLogExport(event); }));
```

---

### FIX-B4 — Deduplikacja `ComputeCameraTransitionDurationSeconds`

**Rozwiązanie:** Przenieść do `RendererViewCamera` jako `static` metoda lub do `src/Renderer/RendererUtils.hpp`:

```cpp
// src/Renderer/RendererViewCamera.hpp — dodaj
static float ComputeTransitionDurationSeconds(float rotationSpeed) noexcept;
```

Usuń trzy kopie z:
- `src/Renderer/RendererLayer.cpp` (anonymous namespace)
- `src/Presentation/Panels/RendererPanelInput.cpp` (anonymous namespace)
- `src/Presentation/Panels/RendererPanelToolbar.cpp` (anonymous namespace)

---

### FIX-C4 — Zastąp `std::system()` w `ScriptRunner`

`std::system()` należy zastąpić właściwym zarządzaniem procesem potomnym:

- **Linux/macOS:** `fork()` + `execvp()` lub `posix_spawn()` z przekierowaniem stdout/stderr przez `pipe()`
- **Windows:** `CreateProcess()` z `STARTUPINFO`
- **Wieloplatformowo:** rozważyć Boost.Process lub lekką własną abstrakcję w `Core/Platform/`

Minimalny refactor bez bibliotek zewnętrznych:

```cpp
// src/Core/Platform/ProcessRunner.hpp — nowa abstrakcja w Core/Platform
struct ProcessResult {
    int exitCode = -1;
    std::string stdout_output;
    std::string stderr_output;
};

[[nodiscard]] Result<ProcessResult> RunProcess(
    const Path &executable,
    const std::vector<std::string> &args,
    const Path &workingDirectory);
```

`ScriptRunner` używa `Platform::RunProcess` zamiast `std::system`.

---

## Podsumowanie

Poniżej skrócona lista kontrolna — co musi być prawdą po zakończeniu wszystkich napraw:

- [ ] `src/Renderer/` **nie includuje** `<imgui.h>` (żaden plik `.hpp` ani `.cpp`)
- [ ] `src/Core/` **nie includuje** żadnego nagłówka z `src/ScientificRuntime/`
- [ ] `src/Events/` **nie includuje** żadnego nagłówka z `src/Presentation/`
- [ ] `ParseRendererShortcutKey` **nie istnieje**
- [ ] `RendererKeyboardShortcutSettings` **nie istnieje**
- [ ] `RendererWindowState::viewUndoHistory` i `viewRedoHistory` **nie istnieją**
- [ ] `RendererWindowState::lastMousePosition` **nie istnieje** (przeniesione do Presentation)
- [ ] `SetCameraViewCommand` **jest używany** przez system undo widoku
- [ ] Skróty klawiaturowe renderera **są zarejestrowane w `KeymapResolver`**
- [ ] `RendererPanelInput` **nie woła** `m_Layer.BeginViewInteraction` etc. bezpośrednio
- [ ] `RendererPoscarLoader` **nie istnieje** — zastąpiony przez `IO/PoscarIO`
- [ ] `LoggingPanel` **nie otwiera** `std::ofstream`
- [ ] `ComputeCameraTransitionDurationSeconds` istnieje **w jednym miejscu**
- [ ] `BuildCellEdges` i `BuildBonds` istnieją **w jednym miejscu**
- [ ] `IJob::UsesPythonRuntime()` **nie istnieje** w `Core/JobSystem/JobSystemTypes.hpp`
- [ ] `std::system()` **nie istnieje** w `ScriptRunner`