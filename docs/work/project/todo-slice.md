# Etap 1 — T04 Slice E–J: Domknięcie Core
## Szczegółowy plan implementacji

> **Branch:** `task/04-core` (kontynuacja)
> **Wejście:** Slice D (ProgressTracker) jest gotowy i przetestowany.
> **Wyjście:** Kompletny T04 — pełny szkielet aplikacyjny bez renderera.

---

## Slice E — ConfigManager (layered YAML)

### 1. Contract sketch

```cpp
// App/ConfigManager.hpp — rozszerzenie istniejącej klasy

class ConfigManager {
    // Cztery warstwy (od najwyższego priorytetu):
    //  [0] project  — project/.defects-studio/settings.yaml
    //  [1] user     — %APPDATA%/DefectsStudio/user.yaml
    //  [2] platform — resources/defaults/{platform}.yaml
    //  [3] app      — resources/defaults/default.yaml

public:
    // Inicjalizacja — ładuje app + platform + user przy starcie
    void Init(std::filesystem::path appResourcesDir,
              std::filesystem::path userConfigDir);

    // Ładowanie / odładowanie konfiguracji projektu
    void LoadProjectConfig(std::filesystem::path projectDir);
    void UnloadProjectConfig();

    // Odczyt — szuka od najwyższej warstwy w dół, fallback do T
    template<typename T>
    T Get(std::string_view keyPath, T defaultValue = {}) const;

    // Zapis — zawsze do warstwy user (NIGDY nie piszemy do defaults)
    template<typename T>
    void SetUser(std::string_view keyPath, T value);

    // Zapis do warstwy projektu (wymaga otwartego projektu)
    template<typename T>
    void SetProject(std::string_view keyPath, T value);

    void SaveUser();      // flush user.yaml na dysk
    void SaveProject();   // flush project settings na dysk

    // Reset klucza — usuń z user.yaml (wróci do defaults)
    void ResetToDefault(std::string_view keyPath);
    void ResetAllUserSettings();

    // Hot-reload — wywołać po zewnętrznej zmianie pliku
    void ReloadUserConfig();
    void ReloadProjectConfig();

    [[nodiscard]] bool HasProjectConfig() const;
    [[nodiscard]] std::filesystem::path GetUserConfigPath() const;
    [[nodiscard]] std::filesystem::path GetProjectConfigPath() const;
};
```

### 2. Testy (pisz PRZED implementacją)

Plik: `tests/Core/ConfigManagerTests.cpp` (rozszerzyć istniejący)

```cpp
// --- Slice E tests ---

TEST(ConfigManagerTest, DefaultDocumentCreatedWhenFileMissing)
// Init z nieistniejącym user.yaml → plik zostaje utworzony z wartościami defaults

TEST(ConfigManagerTest, LayeredGetReadsHighestPriorityFirst)
// Ustaw ten sam klucz w app i user → Get zwraca wartość user

TEST(ConfigManagerTest, SetUserWritesToUserLayerOnly)
// SetUser("viewport.fov", 60.0f) → Get zwraca 60, app layer niezmieniony

TEST(ConfigManagerTest, ResetToDefaultRemovesUserOverride)
// SetUser + ResetToDefault → Get zwraca wartość z app defaults

TEST(ConfigManagerTest, YamlSaveLoadRoundtrip)
// SetUser kilku kluczy, SaveUser(), nowa instancja Init() → te same wartości

TEST(ConfigManagerTest, ProjectConfigOverridesUser)
// LoadProjectConfig → klucz z projektu wygrywa nad user

TEST(ConfigManagerTest, UnloadProjectConfigRestoresUserLayer)
// LoadProjectConfig + UnloadProjectConfig → Get wraca do user layer

TEST(ConfigManagerTest, InvalidYamlFallsBackToDefaults)
// Uszkodzony YAML → Init nie crashuje, Get zwraca defaults

TEST(ConfigManagerTest, UISettingsRoundtrip)
// Zapis + odczyt ui.theme, ui.font_size przez warstwę user
```

### 3. Implementacja

**Struktura pliku default.yaml** (utwórz `resources/defaults/default.yaml`):

```yaml
ui:
  theme: "dark"
  font: "Segoe UI"
  font_size: 14
  scale: 1.0
  language: "en"
  show_fps: false

viewport:
  background: [0.12, 0.12, 0.16]
  atom_radius: 0.4
  bond_thickness: 0.08
  show_axes: true
  fov: 45.0
  near_clip: 0.01
  far_clip: 1000.0

autosave:
  enabled: true
  interval_s: 300
  max_backups: 5

undo:
  max_steps: 200

performance:
  max_fps: 60
  vsync: true

recent_projects:
  max_count: 10
  paths: []
```

**Implementacja warstw:** użyj `std::vector<YAML::Node>` (od najwyższego priorytetu). Odczyt: iteruj od 0 do N, zwróć pierwszy znaleziony klucz. Zapis: zawsze do indexu 0 (project) lub 1 (user).

**Key path:** użyj notacji `.` jako separatora, np. `viewport.fov` → `node["viewport"]["fov"]`. Parser: split po `.`, traverse przez YAML::Node.

**WAŻNE:** `ConfigManager` jest jedynym właścicielem IO plików konfiguracyjnych. Żaden inny kod nie dotyka tych plików bezpośrednio.

### 4. Iteracja

Po przejściu testów:
- [ ] Zweryfikuj że istniejące testy `ConfigManagerTests.cpp` nadal przechodzą
- [ ] Sprawdź edge cases: pusta wartość YAML, nested keys, tablice YAML
- [ ] Dodaj `GetAllKeys()` dla DebugLayer

### 5. Doc-comments + mdBook stub

Dodaj do `docs/src/core/config.md`:
```markdown
# ConfigManager

ConfigManager is the sole owner of config file I/O.
Four-layer hierarchy (highest to lowest priority):
project → user → platform → app defaults.
```

---

## Slice F — Notification System

### 1. Contract sketch

Nowe pliki: `Core/Notifications/`

```cpp
// Notification.hpp
enum class NotificationSeverity { Debug, Info, Success, Warning, Error };
enum class NotificationCategory  { App, Job, Config, IO, Python, Validation, Internal };
enum class NotificationDisplayPolicy { Toast, Silent, Blocking };

struct Notification {
    uint64_t         id;
    NotificationSeverity  severity;
    NotificationCategory  category;
    NotificationDisplayPolicy displayPolicy;
    std::string      title;
    std::string      message;
    std::string      source;       // "JobSystem", "ConfigManager", etc.
    Time::TimePoint  timestamp;
};

// Notifier.hpp
class Notifier {
public:
    explicit Notifier(WeakRef<EventBus> eventBus);

    void Info   (NotificationCategory, std::string title, std::string message, std::string source = {});
    void Success(NotificationCategory, std::string title, std::string message, std::string source = {});
    void Warning(NotificationCategory, std::string title, std::string message, std::string source = {});
    void Error  (NotificationCategory, std::string title, std::string message, std::string source = {});
    void Debug  (NotificationCategory, std::string title, std::string message, std::string source = {});

    // Historia — append-only, tylko w pamięci
    [[nodiscard]] std::vector<Notification> GetHistory() const;
    [[nodiscard]] size_t GetCount() const;
    void ClearHistory();  // tylko dla testów / DebugLayer

private:
    void emit(Notification n);
    uint64_t nextId();

    mutable std::mutex           m_Mutex;
    std::vector<Notification>    m_History;
    std::atomic<uint64_t>        m_NextId{1};
    WeakRef<EventBus>            m_EventBus;
};

// NotificationCenter.hpp
// Live UI hub — subskrybuje NotificationEvent z EventBus,
// fan-out do zarejestrowanych UI listenerów
class NotificationCenter {
public:
    using Listener = std::function<void(const Notification&)>;

    explicit NotificationCenter(WeakRef<EventBus> eventBus);

    SubscriptionHandle RegisterListener(Listener listener);

private:
    void onNotificationEvent(const NotificationEvent& event);

    WeakRef<EventBus>              m_EventBus;
    SubscriptionHandle             m_Subscription;
    std::vector<Listener>          m_Listeners;
    mutable std::mutex             m_Mutex;
};

// NotificationHistory.hpp
// Read model z filtrowaniem
class NotificationHistory {
public:
    struct Filter {
        std::optional<NotificationSeverity>  severity;
        std::optional<NotificationCategory>  category;
        std::optional<std::string>           source;
        std::optional<Time::TimePoint>       from;
        std::optional<Time::TimePoint>       to;
    };

    explicit NotificationHistory(const Notifier& notifier);

    [[nodiscard]] std::vector<Notification> Query(const Filter& filter) const;
    [[nodiscard]] std::vector<Notification> GetAll() const;
    [[nodiscard]] std::vector<Notification> GetByCategory(NotificationCategory) const;
    [[nodiscard]] std::vector<Notification> GetBySeverity(NotificationSeverity) const;
};
```

### 2. Testy

Plik: `tests/Core/NotificationTests.cpp` (nowy)

```cpp
TEST(NotifierTest, InfoAppendsToHistory)
TEST(NotifierTest, ErrorUsesCorrectSeverity)
TEST(NotifierTest, SuccessUsesCorrectSeverity)
TEST(NotifierTest, DebugUsesCorrectSeverity)
TEST(NotifierTest, EmitPublishesNotificationEventOnBus)
TEST(NotifierTest, HistoryIsAppendOnly_ClearIsExplicit)

TEST(NotificationHistoryTest, FilterBySeverity)
TEST(NotificationHistoryTest, FilterByCategory)
TEST(NotificationHistoryTest, FilterBySource)
TEST(NotificationHistoryTest, FilterByTimeRange)
TEST(NotificationHistoryTest, NoFilesystemInteraction)

TEST(NotificationCenterTest, RegisteredListenerReceivesEvent)
TEST(NotificationCenterTest, MultipleListenersAllReceive)
TEST(NotificationCenterTest, UnregisterStopsDelivery)
```

### 3. Implementacja

**NotificationEvent** (w `App/Events/`):
```cpp
struct NotificationEvent {
    Notification notification;
};
```

**Ownership:**
- `Notifier` → owned by `Application` (równy lifetime z aplikacją)
- `NotificationCenter` → owned by `DebugLayer` lub `EditorLayer` (UI lifetime)
- `NotificationHistory` → owned przez UI panel

**Threading:** `Notifier::emit()` może być wołany z worker threadów przez `EventBus::Queue()`, nie `Publish()`.

### 4. Iteracja

- [ ] Sprawdź że `Notifier` działa z `EventBus::Queue()` z non-main thread
- [ ] Dodaj `NotificationHistoryPanel` w `Debug/Panels/`
- [ ] Podłącz `JobSystem` do `Notifier` (job failures → `Notifier.Error(...)`)

---

## Slice G — Thread Affinity

### 1. Contract sketch

Nowy plik: `Core/Threading/ThreadAffinity.hpp`

```cpp
namespace DefectStudio::Threading {

    // Zapis main thread ID przy starcie aplikacji
    void RegisterMainThread();

    // Sprawdzenie — true jeśli wywołany z main thread
    [[nodiscard]] bool IsMainThread();

    // Assert — tylko w Debug. W Release: no-op lub log.
    #define DS_ASSERT_MAIN_THREAD() \
        DS_ASSERT(DefectStudio::Threading::IsMainThread(), \
            "This function must be called from the main thread")

} // namespace DefectStudio::Threading
```

### 2. Testy

```cpp
TEST(ThreadAffinityTest, IsMainThreadReturnsTrueOnMainThread)
TEST(ThreadAffinityTest, IsMainThreadReturnsFalseOnWorkerThread)
TEST(ThreadAffinityTest, AssertMainThreadTriggersOnWorkerThread) // death test
```

### 3. Implementacja

```cpp
// ThreadAffinity.cpp
namespace {
    std::thread::id g_MainThreadId;
}

void RegisterMainThread() {
    g_MainThreadId = std::this_thread::get_id();
}

bool IsMainThread() {
    return std::this_thread::get_id() == g_MainThreadId;
}
```

`RegisterMainThread()` wołany w `Application::Init()` jako pierwsza rzecz.

### 4. Gdzie dodać DS_ASSERT_MAIN_THREAD

Po implementacji, dodaj guard do:
- `EventBus::ProcessQueue()`
- `ProgressTracker` — ścieżki snapshot write (opcjonalnie)
- `NotificationCenter::onNotificationEvent()` — fan-out do UI listenerów
- Każda metoda `Application` modyfikująca UI-visible state

---

## Slice H — UI Style Presets

### 1. Contract sketch

```cpp
// App/UIStyleManager.hpp  (nowy plik lub część ConfigManager)

enum class UIStylePreset { Dark, Light, Classic, Custom };

class UIStyleManager {
public:
    explicit UIStyleManager(ConfigManager& config);

    void ApplyPreset(UIStylePreset preset);
    void ApplyHazelDark();
    void ApplyHazelLight();
    void ApplyHazelClassic();

    // Persist/restore
    void SaveCurrentPreset();          // zapisuje do ConfigManager (user layer)
    void LoadAndApplyFromConfig();     // przy starcie, przed pierwszym renderem

    [[nodiscard]] UIStylePreset GetCurrentPreset() const;

private:
    ConfigManager& m_Config;
    UIStylePreset  m_CurrentPreset = UIStylePreset::Dark;
};
```

### 2. Testy

```cpp
TEST(UIStyleManagerTest, LoadPresetFromConfigAppliesCorrectPreset)
TEST(UIStyleManagerTest, SavePresetPersistsToConfigManager)
TEST(UIStyleManagerTest, RoundtripSaveLoad)
TEST(UIStyleManagerTest, UnknownPresetNameFallsBackToDefault)
```

Uwaga: testy nie mogą wymagać aktywnego kontekstu ImGui. Testuj tylko logikę Config IO, nie styl ImGui.

### 3. Implementacja

Hazel-inspired color scheme:
```cpp
void UIStyleManager::ApplyHazelDark() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding  = 4.0f;
    // ... kolory Hazel dark
    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.105f, 0.11f, 1.0f);
    colors[ImGuiCol_Header]   = ImVec4(0.2f, 0.205f, 0.21f, 1.0f);
    // etc.
}
```

Persystencja: `ConfigManager::SetUser("ui.theme", "dark")`.

---

## Slice I — CapabilityRegistry + CapabilityService

### 1. Contract sketch

Nowe pliki: `Core/Capabilities/`

```cpp
// Capability.hpp
enum class CapabilitySource { BuildTime, RuntimeDetected, Policy };

struct CapabilityInfo {
    std::string     id;        // "PythonBridge", "TracyProfiling", "AdvancedAnalysis"
    bool            available;
    CapabilitySource source;
    std::string     description;
};

// CapabilityRegistry.hpp
class CapabilityRegistry {
public:
    void Register(CapabilityInfo info);  // tylko przed LockAfterStartup()
    void LockAfterStartup();             // po tym: Register() throws

    [[nodiscard]] bool             IsRegistered(std::string_view id) const;
    [[nodiscard]] CapabilityInfo   GetInfo(std::string_view id) const;
    [[nodiscard]] std::vector<CapabilityInfo> GetAll() const;
    [[nodiscard]] bool             IsLocked() const;

private:
    std::unordered_map<std::string, CapabilityInfo> m_Capabilities;
    bool                                             m_Locked = false;
};

// CapabilityService.hpp
class CapabilityService {
public:
    explicit CapabilityService(CapabilityRegistry& registry);

    [[nodiscard]] bool IsAvailable(std::string_view id) const;

    // Throws CapabilityNotAvailableException jeśli unavailable
    void Require(std::string_view id) const;

    // Convenience — nie throwuje, zwraca optional<CapabilityInfo>
    [[nodiscard]] std::optional<CapabilityInfo> TryGet(std::string_view id) const;

private:
    CapabilityRegistry& m_Registry;
};

struct CapabilityNotAvailableException : std::runtime_error {
    std::string capabilityId;
    explicit CapabilityNotAvailableException(std::string id);
};
```

### 2. Testy

```cpp
TEST(CapabilityRegistryTest, BuildTimeCapabilityIsQueryable)
TEST(CapabilityRegistryTest, RuntimeDetectedCapabilityIsQueryable)
TEST(CapabilityRegistryTest, PolicyCapabilityWithFalseReturnsUnavailable)
TEST(CapabilityRegistryTest, LockAfterStartupPreventsLateRegistration)
TEST(CapabilityRegistryTest, UnknownCapabilityReturnsFalse)
TEST(CapabilityRegistryTest, IsLockedReflectsState)

TEST(CapabilityServiceTest, IsAvailableReflectsRegistryState)
TEST(CapabilityServiceTest, RequireThrowsForUnavailableCapability)
TEST(CapabilityServiceTest, RequireDoesNotThrowForAvailableCapability)
TEST(CapabilityServiceTest, TryGetReturnsNulloptForUnknown)
```

### 3. Implementacja

**Inicjalizacja w Application** (po skończeniu Slice I):
```cpp
// Application::Init() — kolejność jest ważna:
// 1. ThreadAffinity::RegisterMainThread()
// 2. CapabilityRegistry::Register(...) — wszystkie capabilities
// 3. EventBus::Init()
// 4. Notifier::Init()
// 5. CoreLayer::OnAttach()  ← JobSystem, ProgressTracker
// 6. CapabilityRegistry::LockAfterStartup()
// 7. Main loop
```

**Capabilities do zarejestrowania na start:**
```cpp
registry.Register({"PythonBridge",    false, CapabilitySource::BuildTime, "Python scripting bridge"});
registry.Register({"TracyProfiling",  TRACY_ENABLE, CapabilitySource::BuildTime, "Tracy profiler"});
registry.Register({"AdvancedAnalysis",false, CapabilitySource::Policy, "Advanced analysis modules"});
```

**Ownership:** `CapabilityRegistry` i `CapabilityService` — owned by `Application`.

### 4. Iteracja

- [ ] Dodaj `CapabilitySnapshotPanel` w `Debug/Panels/` (read-only view)
- [ ] Sprawdź że `LockAfterStartup()` jest wołane we właściwym miejscu
- [ ] Dodaj runtime detection dla `PythonBridge` (czy venv istnieje)

---

## Slice J — StructuredError + Blocking Popup Policy

### 1. Contract sketch

Nowy plik: `Core/Diagnostics/StructuredError.hpp`

```cpp
enum class ErrorCategory {
    Config, Job, IO, Python, Validation, Internal
};

struct StructuredError {
    ErrorCategory category;
    std::string   code;             // "config.missing_file"
    std::string   technicalContext; // dla logów i developerów
    std::string   userMessage;      // dla UI display
    std::string   source;           // "ConfigManager", "JobSystem"

    // Konwersja do Notification
    [[nodiscard]] Notification ToNotification() const;

    // Helper factory
    static StructuredError FromException(const std::exception& e,
                                          ErrorCategory category,
                                          std::string source);
};

// W Application:
// ShowBlockingError — TYLKO dla pre-execution validation failures
// Runtime job failures → Notifier.Error(), NIE ShowBlockingError
void ShowBlockingError(const StructuredError& error);
```

### 2. Testy

```cpp
TEST(StructuredErrorTest, ConstructionWithAllFieldsHoldsValues)
TEST(StructuredErrorTest, ToNotificationProducesCorrectSeverity)
TEST(StructuredErrorTest, ToNotificationPreservesUserMessage)

TEST(ErrorRoutingTest, JobFailurePathUsesNotifier_NotBlockingPopup)
TEST(ErrorRoutingTest, ConfigLoadFailureProducesCorrectCategory)
TEST(ErrorRoutingTest, PreExecutionValidationRoutesToBlockingPopup)
TEST(ErrorRoutingTest, RuntimeFailureRoutesToNotifier)
```

### 3. Implementacja

**Zasada routing'u — zapisz jako komentarz w kodzie:**
```cpp
// RULE: Two distinct error paths exist in DefectsStudio:
//
// PATH A (runtime failures): worker job throws/fails
//   → catch → StructuredError{ErrorCategory::Job}
//   → Notifier.Error() → NotificationEvent → non-blocking toast
//
// PATH B (pre-execution validation): validation before job submission
//   → StructuredError{ErrorCategory::Validation}
//   → Application::ShowBlockingError() → modal popup (blocks UI)
//
// NEVER conflate these two paths. Blocking popup = pre-execution only.
```

**Integracja z JobSystem** — w `JobSystem` exception boundary:
```cpp
try {
    job->Execute(context);
} catch (const std::exception& e) {
    auto error = StructuredError::FromException(e, ErrorCategory::Job, job->GetName());
    // Queue na main thread → Notifier.Error()
}
```

### 4. Iteracja

- [ ] Przejrzyj istniejący kod — zastąp każdy raw `std::string error` przez `StructuredError`
- [ ] Dodaj `StructuredError` do `ConfigLoadResult`
- [ ] Sprawdź że `ShowBlockingError` jest wołany TYLKO z walidacji pre-execution
- [ ] Dodaj helper `DS_TRY(expr)` dla propagacji errorów (opcjonalnie `std::expected`)

---

## Integration Tests — po Slice J

### Contract

Plik: `tests/Core/IntegrationTests.cpp`

```cpp
TEST(IntegrationTest, ApplicationBootsWithAllSlicesIntegrated)
// Application::Init() → Run(1 frame) → Shutdown() — bez assertów, bez crashu

TEST(IntegrationTest, SleepJobSubmitProgressCompleteFlow)
// SleepJob → JobSystem → ProgressTracker snapshot → NotificationCenter receives JobCompletedEvent

TEST(IntegrationTest, FailingJobProducesNotificationViaNotifier)
// FailingJob → snapshot.status == Failed → Notifier history zawiera error

TEST(IntegrationTest, CapabilityRequireUnavailableTriggersShowBlockingError)
// CapabilityService.Require("UnavailableCap") → ShowBlockingError wołany z main thread

TEST(IntegrationTest, ConfigLoadSaveRoundtripWithActiveApplication)
// Init → SetUser(key, value) → SaveUser() → ReloadUserConfig() → Get() zwraca value
```

---

## Acceptance Criteria T04

T04 jest done gdy:

- [ ] `Application` bootuje i shutdownuje bezpiecznie
- [ ] Platform events i subsystem EventBus są osobnymi hierarchiami
- [ ] `Application` owns: `EventBus`, `Notifier`, `CapabilityService`
- [ ] `CoreLayer` owns: `JobSystem`, `ProgressTracker`
- [ ] `CapabilityService` inicjalizowany przed innymi subsystemami
- [ ] `CapabilityRegistry` locked przed główną pętlą
- [ ] `StructuredError` jest jedynym typem błędu w subsystemach
- [ ] Job failures → `Notifier.Error()` (non-blocking)
- [ ] Pre-execution failures → `ShowBlockingError()` (blocking modal)
- [ ] `EventBus` RAII subscriptions, priority, deferred removal, thread-safe queue
- [ ] `ProcessQueue()` wywoływany dokładnie raz na frame na main thread
- [ ] `ProgressTracker` działa jako read model dla UI
- [ ] `ConfigManager` jest jedynym właścicielem config file IO
- [ ] Hazel-inspired presets (Dark, Light, Classic) działają i persystują
- [ ] `Notifier` emituje `NotificationEvent` i prowadzi historię
- [ ] Worker threads nie mutują UI state bezpośrednio
- [ ] `DS_ASSERT_MAIN_THREAD` chroni `ProcessQueue()` i snapshot paths
- [ ] Diagnostics skeleton działa: log panel, job monitor, notification history, capability snapshot
- [ ] Wszystkie testy Slice A–J przechodzą w Debug + Release

---

## Dokumentacja (faza końcowa T04)

Po przejściu wszystkich testów — DOPIERO TERAZ:

1. Pełna dokumentacja referencyjna w `docs/src/core/`
2. ADR dla każdej locked decision (0.1–0.9 z `todo-detailed-copilot-implementation-plan.md`)
3. Diagram systemu w mdBook (ASCII lub Mermaid)
4. Developer guide: "How to add a new capability"
5. Developer guide: "How to add a new notification source"