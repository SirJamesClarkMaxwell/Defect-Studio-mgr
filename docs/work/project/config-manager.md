# T04 — Szczegółowy przewodnik implementacji: Slice E–J

> **Branch:** `task/04-core`  
> **Aktualny stan:** Slice A–D (EventBus, JobSystem, ProgressTracker) gotowe.  
> **Cel:** Domknąć T04 — pełny szkielet aplikacyjny bez renderera.  
> **Strategia:** Piszesz ręcznie, błędy naprawia Copilot. Każdy slice = osobny commit.

---

## Jak czytać ten dokument

Każdy slice jest podzielony na:
1. **Co tworzyć** — lista plików i ich lokalizacja
2. **Krok po kroku** — kolejność działań z pełnym kodem
3. **Pułapki** — najczęstsze błędy przy tym slice'u
4. **Checklist** — zanim commitujesz

Slices w kolejności implementacji: **G → E → F → H → I → J → Testy integracyjne**

> Slice G (ThreadAffinity) jest pierwszy bo jest zależnością dla F i E.

---

---

# SLICE G — ThreadAffinity

**Priorytet: PIERWSZY** (prosta utility, blokuje Slice F)

## Co tworzyć

```
src/Core/Threading/ThreadAffinity.hpp    ← nowy plik
src/Core/Threading/ThreadAffinity.cpp    ← nowy plik
tests/Core/ThreadAffinityTests.cpp       ← nowy plik
```

## Krok po kroku

### Krok 1 — Nagłówek `ThreadAffinity.hpp`

```cpp
#pragma once
#include <thread>

namespace DefectStudio::Threading {

    // Wołaj jako PIERWSZA rzecz w Application::Init()
    void RegisterMainThread();

    // Zwraca true jeśli aktualny wątek to main thread
    [[nodiscard]] bool IsMainThread();

} // namespace DefectStudio::Threading

// Makro guard — tylko w Debug buduje assert, w Release to no-op
#ifdef DS_DEBUG
    #define DS_ASSERT_MAIN_THREAD() \
        DS_ASSERT(DefectStudio::Threading::IsMainThread(), \
            "This function must be called from the main thread")
#else
    #define DS_ASSERT_MAIN_THREAD() ((void)0)
#endif
```

**Uwaga:** `DS_ASSERT` — sprawdź jak jest zdefiniowany w projekcie (prawdopodobnie w `Core/Utils/` lub `dspch.hpp`). Użyj istniejącej konwencji.

### Krok 2 — Implementacja `ThreadAffinity.cpp`

```cpp
#include "dspch.hpp"
#include "ThreadAffinity.hpp"

namespace {
    std::thread::id g_MainThreadId;
}

namespace DefectStudio::Threading {

    void RegisterMainThread() {
        g_MainThreadId = std::this_thread::get_id();
    }

    bool IsMainThread() {
        return std::this_thread::get_id() == g_MainThreadId;
    }

} // namespace DefectStudio::Threading
```

### Krok 3 — Testy `ThreadAffinityTests.cpp`

```cpp
#include <gtest/gtest.h>
#include <thread>
#include "Core/Threading/ThreadAffinity.hpp"

TEST(ThreadAffinityTest, IsMainThreadReturnsTrueOnMainThread) {
    DefectStudio::Threading::RegisterMainThread();
    EXPECT_TRUE(DefectStudio::Threading::IsMainThread());
}

TEST(ThreadAffinityTest, IsMainThreadReturnsFalseOnWorkerThread) {
    DefectStudio::Threading::RegisterMainThread();
    bool result = true;
    std::thread worker([&result]() {
        result = DefectStudio::Threading::IsMainThread();
    });
    worker.join();
    EXPECT_FALSE(result);
}

// Death test — tylko w Debug
#ifdef DS_DEBUG
TEST(ThreadAffinityTest, AssertMainThreadTriggersOnWorkerThread) {
    DefectStudio::Threading::RegisterMainThread();
    EXPECT_DEATH({
        std::thread worker([]() {
            DS_ASSERT_MAIN_THREAD();
        });
        worker.join();
    }, ".*");
}
#endif
```

### Krok 4 — Podłącz do Application

W `Application::Init()` (lub `Application.cpp`) jako PIERWSZA linia:

```cpp
void Application::Init() {
    DefectStudio::Threading::RegisterMainThread();  // ← pierwsza linia!
    // ... reszta inicjalizacji
}
```

### Krok 5 — Dodaj guards do istniejących metod

Po zakończeniu pozostałych slice'ów wróć i dodaj `DS_ASSERT_MAIN_THREAD()` do:
- `EventBus::ProcessQueue()`
- `NotificationCenter::onNotificationEvent()`
- Każdej metody `Application` modyfikującej stan UI

## Pułapki

- **Nie inicjalizuj `g_MainThreadId` jako `{}` z wartością!** Domyślny `std::thread::id{}` to "no thread" — różny od każdego prawdziwego wątku. Jeśli zapomnisz wołać `RegisterMainThread()`, `IsMainThread()` zawsze zwróci false.
- Death testy wymagają `GTEST_HAS_DEATH_TEST`. Sprawdź konfigurację Google Test w projekcie.

## Checklist przed commitem

- [ ] `ThreadAffinity.hpp` + `.cpp` skompilowały się
- [ ] Oba testy (main/worker) przechodzą
- [ ] `RegisterMainThread()` dodany na początku `Application::Init()`
- [ ] `DS_ASSERT_MAIN_THREAD()` jest zdefiniowany dla Debug i Release

---

---

# SLICE E — ConfigManager (layered YAML)

**Zależność:** Brak (niezależny slice, ale Slice G powinien już istnieć)

## Co tworzyć

```
src/App/ConfigManager.hpp              ← rozszerzenie istniejącego (jeśli jest) lub nowy
src/App/ConfigManager.cpp              ← implementacja
resources/defaults/default.yaml       ← nowy plik konfiguracji
resources/defaults/windows.yaml       ← platforma Windows (opcjonalny — może być pusty)
resources/defaults/linux.yaml         ← platforma Linux (opcjonalny — może być pusty)
tests/Core/ConfigManagerTests.cpp     ← rozszerzenie istniejącego lub nowy
docs/src/core/config.md               ← dokumentacja
```

## Wymagana biblioteka

Projekt używa **yaml-cpp** (sprawdź `Vendor/` lub `premake5.lua`). Includuj jako:
```cpp
#include <yaml-cpp/yaml.h>
```

## Krok po kroku

### Krok 1 — Plik `resources/defaults/default.yaml`

Utwórz plik (to nie jest kod C++, to plik zasobów):

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

### Krok 2 — Nagłówek `ConfigManager.hpp`

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace DefectStudio {

class ConfigManager {
public:
    // ─── Inicjalizacja ───────────────────────────────────────────────────────
    // appResourcesDir  = katalog resources/ z plikami defaults/
    // userConfigDir    = %APPDATA%/DefectsStudio/ (Windows) lub ~/.config/...
    void Init(std::filesystem::path appResourcesDir,
              std::filesystem::path userConfigDir);

    // ─── Projekt ─────────────────────────────────────────────────────────────
    void LoadProjectConfig(std::filesystem::path projectDir);
    void UnloadProjectConfig();

    // ─── Odczyt ──────────────────────────────────────────────────────────────
    // Szuka od najwyższej warstwy w dół. Zwraca defaultValue jeśli nie znaleziono.
    template<typename T>
    T Get(std::string_view keyPath, T defaultValue = {}) const;

    // ─── Zapis ───────────────────────────────────────────────────────────────
    template<typename T>
    void SetUser(std::string_view keyPath, T value);

    template<typename T>
    void SetProject(std::string_view keyPath, T value);  // wymaga otwartego projektu

    void SaveUser();     // flush user.yaml na dysk
    void SaveProject();  // flush project/settings.yaml na dysk

    // ─── Reset ───────────────────────────────────────────────────────────────
    void ResetToDefault(std::string_view keyPath);   // usuwa z user layer
    void ResetAllUserSettings();

    // ─── Hot reload ──────────────────────────────────────────────────────────
    void ReloadUserConfig();
    void ReloadProjectConfig();

    // ─── Zapytania ────────────────────────────────────────────────────────────
    [[nodiscard]] bool                    HasProjectConfig() const;
    [[nodiscard]] std::filesystem::path   GetUserConfigPath() const;
    [[nodiscard]] std::filesystem::path   GetProjectConfigPath() const;
    [[nodiscard]] std::vector<std::string> GetAllKeys() const;  // dla DebugLayer

private:
    // Warstwy — kolejność od NAJWYŻSZEGO do najniższego priorytetu:
    //   [0] project  — project/.defects-studio/settings.yaml
    //   [1] user     — userConfigDir/user.yaml
    //   [2] platform — resources/defaults/{platform}.yaml
    //   [3] app      — resources/defaults/default.yaml
    std::vector<YAML::Node> m_Layers;

    // Indeksy warstw — stałe dla przejrzystości
    static constexpr size_t LAYER_PROJECT  = 0;
    static constexpr size_t LAYER_USER     = 1;
    static constexpr size_t LAYER_PLATFORM = 2;
    static constexpr size_t LAYER_APP      = 3;
    static constexpr size_t LAYER_COUNT    = 4;

    std::filesystem::path m_UserConfigPath;
    std::filesystem::path m_ProjectConfigPath;
    bool                  m_HasProjectConfig = false;

    // Prywatne utility
    [[nodiscard]] YAML::Node  traverseRead(const YAML::Node& root,
                                            std::string_view  keyPath) const;
    void                      traverseWrite(YAML::Node&       root,
                                            std::string_view  keyPath,
                                            const YAML::Node& value);
    [[nodiscard]] std::string platformName() const; // "windows" / "linux" / "macos"
    void                      ensureUserFileExists();
    YAML::Node                loadFileOrEmpty(const std::filesystem::path& path) const;
    void                      collectKeys(const YAML::Node& node,
                                          std::string        prefix,
                                          std::vector<std::string>& out) const;
};

// ─── Implementacja template (musi być w headerze) ────────────────────────────

template<typename T>
T ConfigManager::Get(std::string_view keyPath, T defaultValue) const {
    for (const auto& layer : m_Layers) {
        if (!layer.IsDefined() || layer.IsNull()) continue;
        YAML::Node found = traverseRead(layer, keyPath);
        if (found.IsDefined() && !found.IsNull()) {
            try {
                return found.as<T>();
            } catch (const YAML::Exception&) {
                // Nieprawidłowy typ w tej warstwie — idź niżej
            }
        }
    }
    return defaultValue;
}

template<typename T>
void ConfigManager::SetUser(std::string_view keyPath, T value) {
    traverseWrite(m_Layers[LAYER_USER], keyPath, YAML::Node(value));
}

template<typename T>
void ConfigManager::SetProject(std::string_view keyPath, T value) {
    DS_ASSERT(m_HasProjectConfig, "SetProject called without open project");
    traverseWrite(m_Layers[LAYER_PROJECT], keyPath, YAML::Node(value));
}

} // namespace DefectStudio
```

### Krok 3 — Implementacja `ConfigManager.cpp`

```cpp
#include "dspch.hpp"
#include "ConfigManager.hpp"
#include <fstream>
#include <sstream>

namespace DefectStudio {

// ─── platformName ────────────────────────────────────────────────────────────

std::string ConfigManager::platformName() const {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

// ─── loadFileOrEmpty ─────────────────────────────────────────────────────────

YAML::Node ConfigManager::loadFileOrEmpty(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path))
        return YAML::Node(YAML::NodeType::Map);
    try {
        return YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        // Uszkodzony YAML — loguj i zwróć pusty node (fallback do defaults)
        DS_LOG_WARN("ConfigManager: Failed to parse YAML '{}': {}", path.string(), e.what());
        return YAML::Node(YAML::NodeType::Map);
    }
}

// ─── ensureUserFileExists ────────────────────────────────────────────────────

void ConfigManager::ensureUserFileExists() {
    if (!std::filesystem::exists(m_UserConfigPath)) {
        std::filesystem::create_directories(m_UserConfigPath.parent_path());
        // Utwórz pusty plik z komentarzem
        std::ofstream file(m_UserConfigPath);
        file << "# DefectStudio user configuration\n";
        file << "# This file is auto-generated. Edit carefully.\n";
    }
}

// ─── Init ────────────────────────────────────────────────────────────────────

void ConfigManager::Init(std::filesystem::path appResourcesDir,
                          std::filesystem::path userConfigDir) {
    m_Layers.resize(LAYER_COUNT);

    // Warstwa [3] — app defaults (zawsze)
    auto defaultPath = appResourcesDir / "defaults" / "default.yaml";
    m_Layers[LAYER_APP] = loadFileOrEmpty(defaultPath);

    // Warstwa [2] — platform defaults
    auto platformPath = appResourcesDir / "defaults" / (platformName() + ".yaml");
    m_Layers[LAYER_PLATFORM] = loadFileOrEmpty(platformPath);

    // Warstwa [1] — user config
    m_UserConfigPath = userConfigDir / "user.yaml";
    ensureUserFileExists();
    m_Layers[LAYER_USER] = loadFileOrEmpty(m_UserConfigPath);

    // Warstwa [0] — project (pusta do czasu LoadProjectConfig)
    m_Layers[LAYER_PROJECT] = YAML::Node(YAML::NodeType::Map);
}

// ─── LoadProjectConfig ───────────────────────────────────────────────────────

void ConfigManager::LoadProjectConfig(std::filesystem::path projectDir) {
    m_ProjectConfigPath = projectDir / ".defects-studio" / "settings.yaml";
    std::filesystem::create_directories(m_ProjectConfigPath.parent_path());
    m_Layers[LAYER_PROJECT] = loadFileOrEmpty(m_ProjectConfigPath);
    m_HasProjectConfig = true;
}

void ConfigManager::UnloadProjectConfig() {
    m_Layers[LAYER_PROJECT] = YAML::Node(YAML::NodeType::Map);
    m_ProjectConfigPath.clear();
    m_HasProjectConfig = false;
}

// ─── Save ─────────────────────────────────────────────────────────────────────

void ConfigManager::SaveUser() {
    std::ofstream file(m_UserConfigPath);
    file << m_Layers[LAYER_USER];
}

void ConfigManager::SaveProject() {
    DS_ASSERT(m_HasProjectConfig, "SaveProject called without open project");
    std::filesystem::create_directories(m_ProjectConfigPath.parent_path());
    std::ofstream file(m_ProjectConfigPath);
    file << m_Layers[LAYER_PROJECT];
}

// ─── Reset ───────────────────────────────────────────────────────────────────

void ConfigManager::ResetToDefault(std::string_view keyPath) {
    // Usuń klucz z user layer przez traversal i usunięcie node
    // Prosta implementacja: przebuduj node bez tego klucza
    // (yaml-cpp nie ma "remove key" — trick: klonuj bez danego klucza)

    // Parsuj ścieżkę
    std::vector<std::string> parts;
    std::istringstream ss(std::string(keyPath));
    std::string part;
    while (std::getline(ss, part, '.'))
        parts.push_back(part);

    if (parts.empty()) return;

    // Traverse do rodzica i usuń ostatni klucz
    YAML::Node node = m_Layers[LAYER_USER];
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        if (!node[parts[i]].IsDefined()) return;  // już nie istnieje
        node = node[parts[i]];
    }
    node.remove(parts.back());
}

void ConfigManager::ResetAllUserSettings() {
    m_Layers[LAYER_USER] = YAML::Node(YAML::NodeType::Map);
}

// ─── Hot Reload ──────────────────────────────────────────────────────────────

void ConfigManager::ReloadUserConfig() {
    m_Layers[LAYER_USER] = loadFileOrEmpty(m_UserConfigPath);
}

void ConfigManager::ReloadProjectConfig() {
    if (!m_HasProjectConfig) return;
    m_Layers[LAYER_PROJECT] = loadFileOrEmpty(m_ProjectConfigPath);
}

// ─── Zapytania ────────────────────────────────────────────────────────────────

bool ConfigManager::HasProjectConfig() const { return m_HasProjectConfig; }

std::filesystem::path ConfigManager::GetUserConfigPath() const {
    return m_UserConfigPath;
}

std::filesystem::path ConfigManager::GetProjectConfigPath() const {
    return m_ProjectConfigPath;
}

// ─── traverseRead ────────────────────────────────────────────────────────────

YAML::Node ConfigManager::traverseRead(const YAML::Node& root,
                                        std::string_view  keyPath) const {
    std::vector<std::string> parts;
    std::istringstream ss(std::string(keyPath));
    std::string part;
    while (std::getline(ss, part, '.'))
        parts.push_back(part);

    YAML::Node current = root;
    for (const auto& p : parts) {
        if (!current.IsMap() || !current[p].IsDefined())
            return YAML::Node();  // undefined
        current = current[p];
    }
    return current;
}

// ─── traverseWrite ───────────────────────────────────────────────────────────

void ConfigManager::traverseWrite(YAML::Node&       root,
                                    std::string_view  keyPath,
                                    const YAML::Node& value) {
    std::vector<std::string> parts;
    std::istringstream ss(std::string(keyPath));
    std::string part;
    while (std::getline(ss, part, '.'))
        parts.push_back(part);

    YAML::Node current = root;
    for (size_t i = 0; i < parts.size() - 1; ++i)
        current = current[parts[i]];

    current[parts.back()] = value;
}

// ─── GetAllKeys ──────────────────────────────────────────────────────────────

std::vector<std::string> ConfigManager::GetAllKeys() const {
    std::vector<std::string> keys;
    // Zbierz ze wszystkich warstw (merge deduplicated)
    for (const auto& layer : m_Layers)
        collectKeys(layer, "", keys);
    // Deduplikacja
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

void ConfigManager::collectKeys(const YAML::Node& node,
                                  std::string        prefix,
                                  std::vector<std::string>& out) const {
    if (!node.IsMap()) return;
    for (const auto& kv : node) {
        std::string fullKey = prefix.empty()
            ? kv.first.as<std::string>()
            : prefix + "." + kv.first.as<std::string>();
        if (kv.second.IsMap())
            collectKeys(kv.second, fullKey, out);
        else
            out.push_back(fullKey);
    }
}

} // namespace DefectStudio
```

### Krok 4 — Testy `ConfigManagerTests.cpp`

```cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "App/ConfigManager.hpp"

namespace fs = std::filesystem;

// Helper: tymczasowy katalog dla testów
struct ConfigManagerFixture : ::testing::Test {
    fs::path tmpDir;
    fs::path resourcesDir;
    fs::path userConfigDir;
    DefectStudio::ConfigManager cm;

    void SetUp() override {
        tmpDir = fs::temp_directory_path() / "ds_config_test";
        fs::create_directories(tmpDir);
        resourcesDir = tmpDir / "resources";
        userConfigDir = tmpDir / "user";
        fs::create_directories(resourcesDir / "defaults");
        fs::create_directories(userConfigDir);

        // Utwórz default.yaml
        std::ofstream f(resourcesDir / "defaults" / "default.yaml");
        f << "ui:\n  theme: dark\n  font_size: 14\n";
        f << "viewport:\n  fov: 45.0\n";
    }

    void TearDown() override {
        fs::remove_all(tmpDir);
    }
};

TEST_F(ConfigManagerFixture, DefaultDocumentCreatedWhenFileMissing) {
    cm.Init(resourcesDir, userConfigDir);
    EXPECT_TRUE(fs::exists(userConfigDir / "user.yaml"));
}

TEST_F(ConfigManagerFixture, LayeredGetReadsHighestPriorityFirst) {
    cm.Init(resourcesDir, userConfigDir);
    cm.SetUser("ui.theme", std::string("light"));
    EXPECT_EQ(cm.Get<std::string>("ui.theme", "dark"), "light");
}

TEST_F(ConfigManagerFixture, SetUserWritesToUserLayerOnly) {
    cm.Init(resourcesDir, userConfigDir);
    cm.SetUser("viewport.fov", 60.0f);
    EXPECT_FLOAT_EQ(cm.Get<float>("viewport.fov", 45.0f), 60.0f);
}

TEST_F(ConfigManagerFixture, ResetToDefaultRemovesUserOverride) {
    cm.Init(resourcesDir, userConfigDir);
    cm.SetUser("viewport.fov", 90.0f);
    cm.ResetToDefault("viewport.fov");
    EXPECT_FLOAT_EQ(cm.Get<float>("viewport.fov", 45.0f), 45.0f);
}

TEST_F(ConfigManagerFixture, YamlSaveLoadRoundtrip) {
    cm.Init(resourcesDir, userConfigDir);
    cm.SetUser("ui.font_size", 20);
    cm.SaveUser();

    DefectStudio::ConfigManager cm2;
    cm2.Init(resourcesDir, userConfigDir);
    EXPECT_EQ(cm2.Get<int>("ui.font_size", 14), 20);
}

TEST_F(ConfigManagerFixture, ProjectConfigOverridesUser) {
    cm.Init(resourcesDir, userConfigDir);
    cm.SetUser("ui.theme", std::string("light"));

    auto projectDir = tmpDir / "project";
    fs::create_directories(projectDir / ".defects-studio");
    {
        std::ofstream f(projectDir / ".defects-studio" / "settings.yaml");
        f << "ui:\n  theme: classic\n";
    }

    cm.LoadProjectConfig(projectDir);
    EXPECT_EQ(cm.Get<std::string>("ui.theme", "dark"), "classic");
}

TEST_F(ConfigManagerFixture, UnloadProjectConfigRestoresUserLayer) {
    cm.Init(resourcesDir, userConfigDir);
    cm.SetUser("ui.theme", std::string("light"));

    auto projectDir = tmpDir / "project";
    fs::create_directories(projectDir);
    cm.LoadProjectConfig(projectDir);
    cm.UnloadProjectConfig();

    EXPECT_EQ(cm.Get<std::string>("ui.theme", "dark"), "light");
}

TEST_F(ConfigManagerFixture, InvalidYamlFallsBackToDefaults) {
    // Uszkodzony user.yaml
    {
        std::ofstream f(userConfigDir / "user.yaml");
        f << "this: [is: invalid: yaml::::\n";
    }
    // Init nie powinno crashować
    EXPECT_NO_THROW(cm.Init(resourcesDir, userConfigDir));
    // Fallback do defaults
    EXPECT_EQ(cm.Get<std::string>("ui.theme", "fallback"), "dark");
}
```

## Pułapki

- **`YAML::Node` kopie są shallow!** `YAML::Node copy = m_Layers[1]` nie kopiuje głęboko — zmiany w `copy` zmieniają oryginał. Gdy chcesz niezależną kopię: `YAML::Node copy = YAML::Clone(m_Layers[1])`.
- **`node.remove()` w yaml-cpp** — dostępne od yaml-cpp 0.6.0. Sprawdź wersję w `Vendor/`.  Jeśli brakuje, alternatywa: przebuduj node od zera bez usuwanego klucza.
- **Ścieżki `resources/`** — w testach przekaż tymczasowe katalogi (jak w fixtures wyżej). NIE używaj hardkodowanych ścieżek do projektu.
- **`YAML::Node` jest undefined po domyślnej konstrukcji** — zawsze inicjalizuj jako `YAML::Node(YAML::NodeType::Map)`.

## Checklist

- [ ] `default.yaml` istnieje w `resources/defaults/`
- [ ] `Init()` nie crashuje gdy brakuje pliku user.yaml
- [ ] `Init()` nie crashuje gdy user.yaml jest uszkodzony
- [ ] Wszystkie 8 testów przechodzi
- [ ] `GetAllKeys()` zwraca sensowne klucze dla DebugLayer

---

---

# SLICE F — Notification System

**Zależność:** Slice G (ThreadAffinity), istniejący EventBus

## Co tworzyć

```
src/Core/Notifications/Notification.hpp         ← typy danych
src/Core/Notifications/Notifier.hpp             ← emitter
src/Core/Notifications/Notifier.cpp
src/Core/Notifications/NotificationCenter.hpp   ← UI hub
src/Core/Notifications/NotificationCenter.cpp
src/Core/Notifications/NotificationHistory.hpp  ← read model
src/Core/Notifications/NotificationHistory.cpp
src/App/Events/NotificationEvent.hpp            ← event type
tests/Core/NotificationTests.cpp               ← nowy
```

## Krok po kroku

### Krok 1 — `NotificationEvent.hpp`

Dodaj do katalogu gdzie masz inne eventy (prawdopodobnie `src/App/Events/`):

```cpp
#pragma once
#include "Core/Notifications/Notification.hpp"

namespace DefectStudio {

struct NotificationEvent {
    Notification notification;
};

} // namespace DefectStudio
```

### Krok 2 — `Notification.hpp`

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <chrono>

namespace DefectStudio {

enum class NotificationSeverity {
    Debug,
    Info,
    Success,
    Warning,
    Error
};

enum class NotificationCategory {
    App,
    Job,
    Config,
    IO,
    Python,
    Validation,
    Internal
};

enum class NotificationDisplayPolicy {
    Toast,    // krótki popup w rogu ekranu
    Silent,   // tylko historia, brak UI
    Blocking  // modal popup (tylko przez ShowBlockingError!)
};

struct Notification {
    uint64_t                   id;
    NotificationSeverity       severity;
    NotificationCategory       category;
    NotificationDisplayPolicy  displayPolicy;
    std::string                title;
    std::string                message;
    std::string                source;       // "JobSystem", "ConfigManager"
    std::chrono::system_clock::time_point timestamp;
};

} // namespace DefectStudio
```

**Uwaga:** Jeśli masz własny `Time::TimePoint` w projekcie — użyj go zamiast `std::chrono::system_clock::time_point`. Sprawdź `Core/Utils/` lub `Platform/`.

### Krok 3 — `Notifier.hpp`

```cpp
#pragma once
#include <mutex>
#include <atomic>
#include <vector>
#include <functional>
#include "Core/Notifications/Notification.hpp"

// Forward declare — unikamy circular include
namespace DefectStudio { class EventBus; }

namespace DefectStudio {

class Notifier {
public:
    // eventBus może być nullptr — wtedy tylko historia, bez eventów
    explicit Notifier(EventBus* eventBus = nullptr);

    void Info   (NotificationCategory cat, std::string title,
                 std::string message, std::string source = {});
    void Success(NotificationCategory cat, std::string title,
                 std::string message, std::string source = {});
    void Warning(NotificationCategory cat, std::string title,
                 std::string message, std::string source = {});
    void Error  (NotificationCategory cat, std::string title,
                 std::string message, std::string source = {});
    void Debug  (NotificationCategory cat, std::string title,
                 std::string message, std::string source = {});

    // Historia — thread-safe, append-only
    [[nodiscard]] std::vector<Notification> GetHistory() const;
    [[nodiscard]] size_t                    GetCount() const;
    void                                    ClearHistory();  // TYLKO dla testów

private:
    void     emit(Notification n);
    uint64_t nextId();

    mutable std::mutex        m_Mutex;
    std::vector<Notification> m_History;
    std::atomic<uint64_t>     m_NextId{1};
    EventBus*                 m_EventBus;
};

} // namespace DefectStudio
```

### Krok 4 — `Notifier.cpp`

```cpp
#include "dspch.hpp"
#include "Notifier.hpp"
#include "Core/EventSystem/EventBus.hpp"      // dostosuj include path
#include "App/Events/NotificationEvent.hpp"

namespace DefectStudio {

Notifier::Notifier(EventBus* eventBus)
    : m_EventBus(eventBus) {}

uint64_t Notifier::nextId() {
    return m_NextId.fetch_add(1, std::memory_order_relaxed);
}

void Notifier::emit(Notification n) {
    n.id        = nextId();
    n.timestamp = std::chrono::system_clock::now();

    {
        std::lock_guard lock(m_Mutex);
        m_History.push_back(n);
    }

    // Publishuj na EventBus — używaj Queue() jeśli możemy być na worker thread!
    // Queue() = thread-safe, ProcessQueue() wywoła listener na main thread
    if (m_EventBus) {
        m_EventBus->Queue(NotificationEvent{n});  // lub Publish — sprawdź API EventBus
    }
}

void Notifier::Info(NotificationCategory cat, std::string title,
                     std::string message, std::string source) {
    emit({0, NotificationSeverity::Info, cat,
          NotificationDisplayPolicy::Toast,
          std::move(title), std::move(message), std::move(source), {}});
}

void Notifier::Success(NotificationCategory cat, std::string title,
                        std::string message, std::string source) {
    emit({0, NotificationSeverity::Success, cat,
          NotificationDisplayPolicy::Toast,
          std::move(title), std::move(message), std::move(source), {}});
}

void Notifier::Warning(NotificationCategory cat, std::string title,
                        std::string message, std::string source) {
    emit({0, NotificationSeverity::Warning, cat,
          NotificationDisplayPolicy::Toast,
          std::move(title), std::move(message), std::move(source), {}});
}

void Notifier::Error(NotificationCategory cat, std::string title,
                      std::string message, std::string source) {
    emit({0, NotificationSeverity::Error, cat,
          NotificationDisplayPolicy::Toast,
          std::move(title), std::move(message), std::move(source), {}});
}

void Notifier::Debug(NotificationCategory cat, std::string title,
                      std::string message, std::string source) {
    emit({0, NotificationSeverity::Debug, cat,
          NotificationDisplayPolicy::Silent,  // Debug = zawsze silent
          std::move(title), std::move(message), std::move(source), {}});
}

std::vector<Notification> Notifier::GetHistory() const {
    std::lock_guard lock(m_Mutex);
    return m_History;
}

size_t Notifier::GetCount() const {
    std::lock_guard lock(m_Mutex);
    return m_History.size();
}

void Notifier::ClearHistory() {
    std::lock_guard lock(m_Mutex);
    m_History.clear();
}

} // namespace DefectStudio
```

**KLUCZOWE:** Sprawdź API swojego `EventBus`. Czy ma metodę `Queue()` (thread-safe, defer na main thread) i `Publish()` (natychmiastowy)? Notifier z worker threadów musi używać `Queue()`.

### Krok 5 — `NotificationHistory.hpp` i `.cpp`

```cpp
// NotificationHistory.hpp
#pragma once
#include <optional>
#include <vector>
#include <chrono>
#include "Notification.hpp"

namespace DefectStudio {

class Notifier;

class NotificationHistory {
public:
    struct Filter {
        std::optional<NotificationSeverity>  severity;
        std::optional<NotificationCategory>  category;
        std::optional<std::string>           source;
        std::optional<std::chrono::system_clock::time_point> from;
        std::optional<std::chrono::system_clock::time_point> to;
    };

    explicit NotificationHistory(const Notifier& notifier);

    [[nodiscard]] std::vector<Notification> Query(const Filter& filter) const;
    [[nodiscard]] std::vector<Notification> GetAll() const;
    [[nodiscard]] std::vector<Notification> GetByCategory(NotificationCategory) const;
    [[nodiscard]] std::vector<Notification> GetBySeverity(NotificationSeverity) const;

private:
    const Notifier& m_Notifier;
};

} // namespace DefectStudio
```

```cpp
// NotificationHistory.cpp
#include "dspch.hpp"
#include "NotificationHistory.hpp"
#include "Notifier.hpp"

namespace DefectStudio {

NotificationHistory::NotificationHistory(const Notifier& notifier)
    : m_Notifier(notifier) {}

std::vector<Notification> NotificationHistory::GetAll() const {
    return m_Notifier.GetHistory();
}

std::vector<Notification> NotificationHistory::Query(const Filter& filter) const {
    auto all = m_Notifier.GetHistory();
    std::vector<Notification> result;
    for (const auto& n : all) {
        if (filter.severity && n.severity != *filter.severity) continue;
        if (filter.category && n.category != *filter.category) continue;
        if (filter.source   && n.source   != *filter.source)   continue;
        if (filter.from     && n.timestamp < *filter.from)      continue;
        if (filter.to       && n.timestamp > *filter.to)        continue;
        result.push_back(n);
    }
    return result;
}

std::vector<Notification> NotificationHistory::GetByCategory(NotificationCategory cat) const {
    return Query({.category = cat});
}

std::vector<Notification> NotificationHistory::GetBySeverity(NotificationSeverity sev) const {
    return Query({.severity = sev});
}

} // namespace DefectStudio
```

### Krok 6 — `NotificationCenter.hpp` i `.cpp`

```cpp
// NotificationCenter.hpp
#pragma once
#include <functional>
#include <mutex>
#include <vector>
#include "Notification.hpp"

namespace DefectStudio {

class EventBus;

class NotificationCenter {
public:
    using Listener = std::function<void(const Notification&)>;

    explicit NotificationCenter(EventBus* eventBus);
    ~NotificationCenter();

    // Zwraca handle — trzymaj aby utrzymać subskrypcję
    // (lub użyj swojego SubscriptionHandle z EventBus)
    void RegisterListener(Listener listener);

private:
    void onNotificationEvent(const struct NotificationEvent& event);

    EventBus*              m_EventBus;
    std::vector<Listener>  m_Listeners;
    mutable std::mutex     m_Mutex;
    // Subskrypcja na EventBus (typ dostosuj do swojego EventBus API)
    // SubscriptionHandle  m_Subscription;
};

} // namespace DefectStudio
```

## Pułapki

- **Circular include:** `Notifier.hpp` → `EventBus.hpp` → ... Używaj forward declarations w nagłówkach, pełne includy tylko w `.cpp`.
- **`Queue()` vs `Publish()`:** Z worker thread ZAWSZE `Queue()`. Z main thread możesz `Publish()`. Jeśli twój EventBus nie ma `Queue()`, użyj mutex + vector pending events i flush na main thread.
- **Inicjalizacja `Notifier`:** Owned by `Application`. Musi przeżyć wszystkie warstwy (Layer). Destroy po `LayerStack`.

## Checklist

- [ ] `Notifier` kompiluje się bez includowania pełnego `EventBus.hpp` w headerze
- [ ] `emit()` jest thread-safe (mutex)
- [ ] `GetHistory()` zwraca kopię (nie referencję do wewnętrznego wektora)
- [ ] Wszystkie 13 testów przechodzi

---

---

# SLICE H — UIStyleManager

**Zależność:** Slice E (ConfigManager), ImGui dostępny w projekcie

## Co tworzyć

```
src/App/UIStyleManager.hpp
src/App/UIStyleManager.cpp
tests/Core/UIStyleManagerTests.cpp
```

## Krok po kroku

### Krok 1 — `UIStyleManager.hpp`

```cpp
#pragma once
#include <string>
#include "ConfigManager.hpp"

// Forward declare ImGui — nie includuj imgui.h w headerze
struct ImGuiStyle;

namespace DefectStudio {

enum class UIStylePreset {
    Dark,
    Light,
    Classic,
    Custom
};

class UIStyleManager {
public:
    explicit UIStyleManager(ConfigManager& config);

    void ApplyPreset(UIStylePreset preset);

    // Persist / restore
    void SaveCurrentPreset();        // zapisuje do ConfigManager (user layer)
    void LoadAndApplyFromConfig();   // przy starcie, przed pierwszym renderem

    [[nodiscard]] UIStylePreset GetCurrentPreset() const;

    // Konwersja enum ↔ string (dla ConfigManager)
    static std::string       PresetToString(UIStylePreset preset);
    static UIStylePreset     StringToPreset(std::string_view str);

private:
    void applyHazelDark();
    void applyHazelLight();
    void applyHazelClassic();

    ConfigManager&  m_Config;
    UIStylePreset   m_CurrentPreset = UIStylePreset::Dark;
};

} // namespace DefectStudio
```

### Krok 2 — `UIStyleManager.cpp`

```cpp
#include "dspch.hpp"
#include "UIStyleManager.hpp"
#include <imgui.h>

namespace DefectStudio {

UIStyleManager::UIStyleManager(ConfigManager& config)
    : m_Config(config) {}

std::string UIStyleManager::PresetToString(UIStylePreset preset) {
    switch (preset) {
        case UIStylePreset::Dark:    return "dark";
        case UIStylePreset::Light:   return "light";
        case UIStylePreset::Classic: return "classic";
        case UIStylePreset::Custom:  return "custom";
        default:                     return "dark";
    }
}

UIStylePreset UIStyleManager::StringToPreset(std::string_view str) {
    if (str == "light")   return UIStylePreset::Light;
    if (str == "classic") return UIStylePreset::Classic;
    if (str == "custom")  return UIStylePreset::Custom;
    return UIStylePreset::Dark;  // fallback
}

void UIStyleManager::ApplyPreset(UIStylePreset preset) {
    m_CurrentPreset = preset;
    switch (preset) {
        case UIStylePreset::Dark:    applyHazelDark();    break;
        case UIStylePreset::Light:   applyHazelLight();   break;
        case UIStylePreset::Classic: applyHazelClassic(); break;
        case UIStylePreset::Custom:  break;  // użytkownik zarządza sam
    }
}

void UIStyleManager::SaveCurrentPreset() {
    m_Config.SetUser("ui.theme", PresetToString(m_CurrentPreset));
    m_Config.SaveUser();
}

void UIStyleManager::LoadAndApplyFromConfig() {
    auto themeStr = m_Config.Get<std::string>("ui.theme", "dark");
    ApplyPreset(StringToPreset(themeStr));
}

UIStylePreset UIStyleManager::GetCurrentPreset() const {
    return m_CurrentPreset;
}

// ─── Hazel Dark ──────────────────────────────────────────────────────────────

void UIStyleManager::applyHazelDark() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 5.0f;
    style.FrameRounding     = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(5.0f, 3.0f);
    style.ItemSpacing       = ImVec2(6.0f, 5.0f);

    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg]         = ImVec4(0.10f, 0.105f, 0.11f,  1.00f);
    colors[ImGuiCol_Header]           = ImVec4(0.20f, 0.205f, 0.21f,  1.00f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.30f, 0.305f, 0.31f,  1.00f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.15f, 0.1505f,0.151f, 1.00f);
    colors[ImGuiCol_Button]           = ImVec4(0.20f, 0.205f, 0.21f,  1.00f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.30f, 0.305f, 0.31f,  1.00f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.15f, 0.1505f,0.151f, 1.00f);
    colors[ImGuiCol_FrameBg]          = ImVec4(0.20f, 0.205f, 0.21f,  1.00f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.30f, 0.305f, 0.31f,  1.00f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.15f, 0.1505f,0.151f, 1.00f);
    colors[ImGuiCol_Tab]              = ImVec4(0.15f, 0.1505f,0.151f, 1.00f);
    colors[ImGuiCol_TabHovered]       = ImVec4(0.38f, 0.3805f,0.381f, 1.00f);
    colors[ImGuiCol_TabActive]        = ImVec4(0.28f, 0.2805f,0.281f, 1.00f);
    colors[ImGuiCol_TitleBg]          = ImVec4(0.15f, 0.1505f,0.151f, 1.00f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.15f, 0.1505f,0.151f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.1505f,0.151f, 1.00f);
}

void UIStyleManager::applyHazelLight() {
    ImGui::StyleColorsLight();  // base
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding  = 4.0f;
}

void UIStyleManager::applyHazelClassic() {
    ImGui::StyleColorsClassic();  // base
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0f;
    style.FrameRounding  = 3.0f;
}

} // namespace DefectStudio
```

### Krok 3 — Testy (BEZ ImGui context)

```cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "App/UIStyleManager.hpp"
#include "App/ConfigManager.hpp"

namespace fs = std::filesystem;

// UWAGA: testy nie testują ImGui styles — testują tylko logikę Config IO

struct UIStyleManagerFixture : ::testing::Test {
    fs::path tmpDir;
    DefectStudio::ConfigManager cm;
    // UIStyleManager nie jest tu — testujemy tylko pomocnicze metody

    void SetUp() override {
        tmpDir = fs::temp_directory_path() / "ds_style_test";
        fs::create_directories(tmpDir / "resources" / "defaults");
        {
            std::ofstream f(tmpDir / "resources" / "defaults" / "default.yaml");
            f << "ui:\n  theme: dark\n";
        }
        cm.Init(tmpDir / "resources", tmpDir / "user");
    }

    void TearDown() override { fs::remove_all(tmpDir); }
};

TEST(UIStyleManagerStaticTest, PresetToStringRoundtrip) {
    using namespace DefectStudio;
    EXPECT_EQ(UIStyleManager::PresetToString(UIStylePreset::Dark),    "dark");
    EXPECT_EQ(UIStyleManager::PresetToString(UIStylePreset::Light),   "light");
    EXPECT_EQ(UIStyleManager::PresetToString(UIStylePreset::Classic), "classic");
}

TEST(UIStyleManagerStaticTest, StringToPresetRoundtrip) {
    using namespace DefectStudio;
    EXPECT_EQ(UIStyleManager::StringToPreset("dark"),    UIStylePreset::Dark);
    EXPECT_EQ(UIStyleManager::StringToPreset("light"),   UIStylePreset::Light);
    EXPECT_EQ(UIStyleManager::StringToPreset("classic"), UIStylePreset::Classic);
}

TEST(UIStyleManagerStaticTest, UnknownPresetNameFallsBackToDefault) {
    using namespace DefectStudio;
    EXPECT_EQ(UIStyleManager::StringToPreset("garbage"), UIStylePreset::Dark);
}

TEST_F(UIStyleManagerFixture, SavePresetPersistsToConfigManager) {
    cm.SetUser("ui.theme", std::string("light"));
    cm.SaveUser();
    auto theme = cm.Get<std::string>("ui.theme", "dark");
    EXPECT_EQ(theme, "light");
}

TEST_F(UIStyleManagerFixture, RoundtripSaveLoad) {
    cm.SetUser("ui.theme", std::string("classic"));
    cm.SaveUser();
    
    DefectStudio::ConfigManager cm2;
    cm2.Init(tmpDir / "resources", tmpDir / "user");
    auto theme = cm2.Get<std::string>("ui.theme", "dark");
    EXPECT_EQ(DefectStudio::UIStyleManager::StringToPreset(theme),
              DefectStudio::UIStylePreset::Classic);
}
```

## Pułapki

- **NIE twórz `UIStyleManager` w testach** — `ApplyPreset()` woła `ImGui::GetStyle()` co wymaga aktywnego ImGui context (crash w testach). Testuj TYLKO `PresetToString/StringToPreset` i logikę ConfigManager.
- **`LoadAndApplyFromConfig()` wołaj po inicjalizacji ImGui** — nigdy przed.

---

---

# SLICE I — CapabilityRegistry + CapabilityService

**Zależność:** Istniejący `EventBus`, `StructuredError` (Slice J — możesz pominąć na razie, dodaj placeholder)

## Co tworzyć

```
src/Core/Capabilities/Capability.hpp
src/Core/Capabilities/CapabilityRegistry.hpp
src/Core/Capabilities/CapabilityRegistry.cpp
src/Core/Capabilities/CapabilityService.hpp
src/Core/Capabilities/CapabilityService.cpp
tests/Core/CapabilityTests.cpp
```

## Krok po kroku

### Krok 1 — `Capability.hpp`

```cpp
#pragma once
#include <string>
#include <stdexcept>

namespace DefectStudio {

enum class CapabilitySource {
    BuildTime,        // kompilacja (#ifdef)
    RuntimeDetected,  // wykryte w runtime (czy venv istnieje)
    Policy            // wyłączone przez politykę/licencję
};

struct CapabilityInfo {
    std::string       id;           // "PythonBridge", "TracyProfiling"
    bool              available;
    CapabilitySource  source;
    std::string       description;
};

struct CapabilityNotAvailableException : std::runtime_error {
    std::string capabilityId;
    explicit CapabilityNotAvailableException(std::string id)
        : std::runtime_error("Capability not available: " + id)
        , capabilityId(std::move(id)) {}
};

} // namespace DefectStudio
```

### Krok 2 — `CapabilityRegistry.hpp` i `.cpp`

```cpp
// CapabilityRegistry.hpp
#pragma once
#include <unordered_map>
#include <vector>
#include <string_view>
#include <optional>
#include "Capability.hpp"

namespace DefectStudio {

class CapabilityRegistry {
public:
    // Register — tylko PRZED LockAfterStartup()
    void Register(CapabilityInfo info);

    // Po LockAfterStartup() — Register() rzuca std::logic_error
    void LockAfterStartup();

    [[nodiscard]] bool             IsRegistered(std::string_view id) const;
    [[nodiscard]] CapabilityInfo   GetInfo(std::string_view id) const;  // throws jeśli nie ma
    [[nodiscard]] std::vector<CapabilityInfo> GetAll() const;
    [[nodiscard]] bool             IsLocked() const;

private:
    std::unordered_map<std::string, CapabilityInfo> m_Capabilities;
    bool                                             m_Locked = false;
};

} // namespace DefectStudio
```

```cpp
// CapabilityRegistry.cpp
#include "dspch.hpp"
#include "CapabilityRegistry.hpp"
#include <stdexcept>

namespace DefectStudio {

void CapabilityRegistry::Register(CapabilityInfo info) {
    if (m_Locked)
        throw std::logic_error("CapabilityRegistry: Cannot register after LockAfterStartup()");
    m_Capabilities[info.id] = std::move(info);
}

void CapabilityRegistry::LockAfterStartup() {
    m_Locked = true;
}

bool CapabilityRegistry::IsRegistered(std::string_view id) const {
    return m_Capabilities.contains(std::string(id));
}

CapabilityInfo CapabilityRegistry::GetInfo(std::string_view id) const {
    auto it = m_Capabilities.find(std::string(id));
    if (it == m_Capabilities.end())
        throw std::out_of_range("CapabilityRegistry: Unknown capability: " + std::string(id));
    return it->second;
}

std::vector<CapabilityInfo> CapabilityRegistry::GetAll() const {
    std::vector<CapabilityInfo> result;
    result.reserve(m_Capabilities.size());
    for (const auto& [key, val] : m_Capabilities)
        result.push_back(val);
    return result;
}

bool CapabilityRegistry::IsLocked() const { return m_Locked; }

} // namespace DefectStudio
```

### Krok 3 — `CapabilityService.hpp` i `.cpp`

```cpp
// CapabilityService.hpp
#pragma once
#include <optional>
#include <string_view>
#include "Capability.hpp"
#include "CapabilityRegistry.hpp"

namespace DefectStudio {

class CapabilityService {
public:
    explicit CapabilityService(CapabilityRegistry& registry);

    [[nodiscard]] bool IsAvailable(std::string_view id) const;

    // Rzuca CapabilityNotAvailableException jeśli unavailable
    void Require(std::string_view id) const;

    // Nie rzuca — zwraca nullopt dla nieznanych
    [[nodiscard]] std::optional<CapabilityInfo> TryGet(std::string_view id) const;

private:
    CapabilityRegistry& m_Registry;
};

} // namespace DefectStudio
```

```cpp
// CapabilityService.cpp
#include "dspch.hpp"
#include "CapabilityService.hpp"

namespace DefectStudio {

CapabilityService::CapabilityService(CapabilityRegistry& registry)
    : m_Registry(registry) {}

bool CapabilityService::IsAvailable(std::string_view id) const {
    if (!m_Registry.IsRegistered(id)) return false;
    return m_Registry.GetInfo(id).available;
}

void CapabilityService::Require(std::string_view id) const {
    if (!IsAvailable(id))
        throw CapabilityNotAvailableException(std::string(id));
}

std::optional<CapabilityInfo> CapabilityService::TryGet(std::string_view id) const {
    if (!m_Registry.IsRegistered(id)) return std::nullopt;
    return m_Registry.GetInfo(id);
}

} // namespace DefectStudio
```

### Krok 4 — Rejestracja w Application

```cpp
// W Application::Init() — PO RegisterMainThread(), PRZED EventBus::Init():

m_CapabilityRegistry = std::make_unique<CapabilityRegistry>();

m_CapabilityRegistry->Register({
    "PythonBridge",
    false,  // false dopóki nie mamy Python Bridge
    CapabilitySource::BuildTime,
    "Python scripting bridge"
});

m_CapabilityRegistry->Register({
    "TracyProfiling",
#ifdef TRACY_ENABLE
    true,
#else
    false,
#endif
    CapabilitySource::BuildTime,
    "Tracy profiler integration"
});

m_CapabilityRegistry->Register({
    "AdvancedAnalysis",
    false,
    CapabilitySource::Policy,
    "Advanced analysis modules (requires license)"
});

// ... inicjalizacja innych subsystemów ...

// NA KOŃCU Init(), przed main loop:
m_CapabilityRegistry->LockAfterStartup();
```

## Checklist

- [ ] `Register()` po `LockAfterStartup()` rzuca `std::logic_error`
- [ ] `IsAvailable("unknown")` zwraca false (nie rzuca)
- [ ] `Require()` rzuca `CapabilityNotAvailableException` dla unavailable
- [ ] `TryGet()` zwraca `nullopt` dla nieznanych
- [ ] Wszystkie 10 testów przechodzi

---

---

# SLICE J — StructuredError + Blocking Popup Policy

**Zależność:** Slice F (Notifications), Slice I (CapabilityRegistry)

## Co tworzyć

```
src/Core/Diagnostics/StructuredError.hpp
src/Core/Diagnostics/StructuredError.cpp
tests/Core/StructuredErrorTests.cpp
```

## Krok po kroku

### Krok 1 — `StructuredError.hpp`

```cpp
#pragma once
#include <string>
#include <stdexcept>
#include "Core/Notifications/Notification.hpp"  // dostosuj path

namespace DefectStudio {

enum class ErrorCategory {
    Config,
    Job,
    IO,
    Python,
    Validation,
    Internal
};

struct StructuredError {
    ErrorCategory category;
    std::string   code;             // "config.missing_file", "job.execution_failed"
    std::string   technicalContext; // stack trace, parametry — dla logów
    std::string   userMessage;      // przyjazna wiadomość dla UI
    std::string   source;           // "ConfigManager", "JobSystem"

    // Konwersja do Notification (dla Notifier)
    [[nodiscard]] Notification ToNotification() const;

    // Factory z exception
    static StructuredError FromException(const std::exception& e,
                                          ErrorCategory          category,
                                          std::string            source);
};

} // namespace DefectStudio
```

### Krok 2 — `StructuredError.cpp`

```cpp
#include "dspch.hpp"
#include "StructuredError.hpp"

namespace DefectStudio {

// Mapowanie ErrorCategory → NotificationCategory
static NotificationCategory toNotificationCategory(ErrorCategory cat) {
    switch (cat) {
        case ErrorCategory::Config:     return NotificationCategory::Config;
        case ErrorCategory::Job:        return NotificationCategory::Job;
        case ErrorCategory::IO:         return NotificationCategory::IO;
        case ErrorCategory::Python:     return NotificationCategory::Python;
        case ErrorCategory::Validation: return NotificationCategory::Validation;
        default:                        return NotificationCategory::Internal;
    }
}

Notification StructuredError::ToNotification() const {
    return Notification{
        .id            = 0,          // wypełni Notifier
        .severity      = NotificationSeverity::Error,
        .category      = toNotificationCategory(category),
        .displayPolicy = NotificationDisplayPolicy::Toast,
        .title         = "Error: " + code,
        .message       = userMessage,
        .source        = source,
        .timestamp     = {}          // wypełni Notifier
    };
}

StructuredError StructuredError::FromException(const std::exception& e,
                                                ErrorCategory          category,
                                                std::string            source) {
    return StructuredError{
        .category         = category,
        .code             = "exception.caught",
        .technicalContext = e.what(),
        .userMessage      = "An unexpected error occurred.",  // generic, bo może być sensitive
        .source           = std::move(source)
    };
}

} // namespace DefectStudio
```

### Krok 3 — Zasada routingu (WAŻNE — dodaj jako komentarz w Application.cpp)

```cpp
// RULE: Two distinct error paths exist in DefectsStudio:
//
// PATH A — runtime failures (worker job throws/fails):
//   → catch exception in JobSystem
//   → StructuredError{ErrorCategory::Job}
//   → Notifier.Error() → NotificationEvent → non-blocking toast
//
// PATH B — pre-execution validation (before job submission):
//   → StructuredError{ErrorCategory::Validation}
//   → Application::ShowBlockingError() → modal popup (blocks UI)
//
// NEVER conflate these two paths.
// ShowBlockingError() = pre-execution ONLY.
// Runtime failures NEVER use ShowBlockingError().
```

### Krok 4 — `ShowBlockingError` w Application

```cpp
// Application.hpp — dodaj metodę:
void ShowBlockingError(const StructuredError& error);

// Application.cpp:
void Application::ShowBlockingError(const StructuredError& error) {
    DS_ASSERT_MAIN_THREAD();
    // Na razie: log + assert
    // Docelowo: otwórz modal ImGui z error.userMessage
    DS_LOG_ERROR("[BlockingError][{}] {} — {}", error.source, error.code, error.userMessage);
    // TODO: otworzyć modal ImGui w kolejnym framie
}
```

### Krok 5 — Integracja z JobSystem

W `JobSystem` (jeśli masz exception boundary przy wykonywaniu jobów), dodaj:

```cpp
try {
    job->Execute(context);
} catch (const std::exception& e) {
    auto error = StructuredError::FromException(e, ErrorCategory::Job, job->GetName());
    // Wyślij na main thread przez EventBus::Queue() → Notifier.Error()
    // (nie Notifier bezpośrednio — możemy być na worker thread)
}
```

## Checklist

- [ ] `ToNotification()` zwraca `NotificationSeverity::Error`
- [ ] `FromException()` nie rzuca (nawet dla dziwnych exceptions)
- [ ] `ShowBlockingError()` jest wołana TYLKO z main thread (DS_ASSERT_MAIN_THREAD)
- [ ] W kodzie JobSystem jest exception boundary z StructuredError

---

---

# Kolejność implementacji — podsumowanie

```
Tydzień 1:
  ├── G: ThreadAffinity          (~1-2h, prosta utility)
  ├── E: ConfigManager           (~4-6h, najwięcej edge cases)
  └── I: CapabilityRegistry      (~2-3h, prosta logika)

Tydzień 2:
  ├── F: Notification System     (~4-5h, threading ważny)
  ├── H: UIStyleManager          (~1-2h, głównie ImGui)
  └── J: StructuredError         (~2-3h, logika routing'u)

Tydzień 3:
  └── Integration Tests          (~3-4h)
```

---

# Testy integracyjne (po wszystkich slice'ach)

Plik: `tests/Core/IntegrationTests.cpp`

```cpp
#include <gtest/gtest.h>
// ... includy

TEST(IntegrationTest, ApplicationBootsWithAllSlicesIntegrated) {
    // Application::Init() → Run(1 frame) → Shutdown() — bez crashu
}

TEST(IntegrationTest, SleepJobSubmitProgressCompleteFlow) {
    // SleepJob → JobSystem → ProgressTracker snapshot
    // → NotificationCenter receives JobCompletedEvent
}

TEST(IntegrationTest, FailingJobProducesNotificationViaNotifier) {
    // FailingJob → snapshot.status == Failed
    // → Notifier history zawiera error notification
}

TEST(IntegrationTest, ConfigLoadSaveRoundtripWithActiveApplication) {
    // Init → SetUser(key, value) → SaveUser() → ReloadUserConfig() → Get() == value
}
```

---

# Acceptance Criteria T04 — checkup

Po ukończeniu wszystkich slice'ów, zweryfikuj każdy punkt:

- [ ] `Application` bootuje i shutdownuje bezpiecznie
- [ ] `ThreadAffinity::RegisterMainThread()` jako pierwsza linia `Init()`
- [ ] `EventBus::ProcessQueue()` ma `DS_ASSERT_MAIN_THREAD()`
- [ ] `CapabilityRegistry` zablokowany (`LockAfterStartup()`) przed main loop
- [ ] `ConfigManager` jest JEDYNYM kodem dotykającym pliki config
- [ ] Job failures → `Notifier.Error()` (NIE `ShowBlockingError`)
- [ ] Pre-execution failures → `ShowBlockingError()` (NIE `Notifier`)
- [ ] `Notifier::emit()` jest thread-safe
- [ ] Worker threads nie mutują UI state bezpośrednio
- [ ] Wszystkie testy Slice E–J przechodzą w Debug + Release

---

*Dokument wygenerowany na podstawie: 01-T04-slices-E-J.md, code review z poprzednich chatów, struktura repo (screenshot branch task/04-core)*