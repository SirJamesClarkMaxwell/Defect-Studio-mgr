# Code Review — task/04-core
## Przed implementacją Slice E–J

> Celem jest identyfikacja problemów które **ugryją w tyłek** przy implementacji Slice E (ConfigManager),
> Slice F (Notifications), i dalej. Nie jest to review stylistyczny — skupiam się na bugach,
> architektonicznych pułapkach i długu który trzeba spłacić **zanim** nałożymy kolejne warstwy.

---

## 🔴 Krytyczne — napraw przed Slice E

### CR-01: `EventBus` jest własnością `CoreLayer`, a powinien być własnością `Application`

**Gdzie:** `CoreLayer.hpp` linia 32: `Ref<EventBus> m_EventBus;`  
**Problem:** Plan T04 section 0.2 mówi wprost: `Application` owns `EventBus`.
`Notifier` (Slice F) będzie emitował eventy z `Application::shutdownInternal()` i z pre-execution
validation paths. Jeśli EventBus żyje w CoreLayer, a CoreLayer jest niszczony przez `m_LayerStack.Clear()`
w `shutdownInternal()`, `Notifier` nie ma jak emitować przy shutdown — EventBus już nie żyje.

**Skutek:** Slice F (Notifier) nie da się poprawnie podłączyć do EventBusa własnego przez Application
bez refactoringu. Każdy test integracyjny Notifiera będzie walczył z lifetime'em.

**Fix:**
```cpp
// Application.hpp — dodaj:
Ref<EventBus>  m_EventBus;
Unique<Notifier> m_Notifier;  // Slice F

// Application::createFromSpecification():
// Krok 1: utwórz EventBus PRZED setupDefaultLayers()
m_EventBus = CreateRef<EventBus>();

// CoreLayer::InitializeSystems() — zmień:
// zamiast tworzyć własny EventBus, przyjmuje WeakRef z zewnątrz
bool InitializeSystems(WeakRef<EventBus> eventBus);

// Application::GetEventBus() — zwraca m_EventBus zamiast CoreLayer::GetEventBus()
```

**Szacowany czas:** 1–2h.

---

### CR-02: YAML sekwencje nie są obsługiwane — default.yaml je wymaga

**Gdzie:** `ConfigManager.cpp` linia 89–92:
```cpp
if (node.IsSequence()) {
    error = "YAML sequences are not supported in config files yet";
    return;
}
```

**Problem:** Planowany `default.yaml` ma:
```yaml
viewport:
  background: [0.12, 0.12, 0.16]   # sekwencja!
recent_projects:
  paths: []                          # sekwencja!
```
Ładowanie tego pliku zwróci błąd z powodu sekwencji. Całe ładowanie konfiguracji padnie.

**Fix opcja A (minimalna, szybka):** Serializuj sekwencje do comma-separated string:
```cpp
if (node.IsSequence()) {
    std::string joined;
    for (const auto& item : node) {
        if (!joined.empty()) joined += ", ";
        joined += item.Scalar();
    }
    document.Set(prefix, joined);
    return;
}
```

**Fix opcja B (właściwa, przy Slice E):** Przy przebudowie ConfigManager na instancję z `YAML::Node`
per-layer (zamiast flat `map<string,string>`), sekwencje są obsługiwane natywnie przez yaml-cpp.
Polecam B — ale musisz wiedzieć o tym problemie zanim zaczniesz pisać default.yaml.

---

### CR-03: YAML write — flat keys z kropkami, nie zagnieżdżone

**Gdzie:** `ConfigManager.cpp`, `WriteYamlFile()`:
```cpp
for (const auto &[key, value] : document.values)
    emitter << YAML::Key << key << YAML::Value << value;
```

**Problem:** `ConfigDocument` przechowuje klucze jako `"viewport.fov"` (flat z kropką).
`FlattenYaml` poprawnie czyta zagnieżdżony YAML (`viewport: { fov: 45.0 }`) i spłaszcza
do `"viewport.fov"`. Ale zapis zapisuje LITERALNY klucz `"viewport.fov": "45.0"`.

Wynikowy plik YAML:
```yaml
viewport.fov: "45.0"   ← literalny klucz z kropką, NIE zagnieżdżone!
```

Roundtrip flat→flat działa, ale plik nie jest czytelny ani kompatybilny z ręcznie pisanym YAML.
Obecny test `SaveAndLoadRoundTripAcrossFormats` nie łapie tego bo testuje płaskie klucze
(`show_demo_window`) bez hierarchii.

**Fix (przy Slice E):** Przebudowa ConfigManager na natywny `YAML::Node` per-layer eliminuje
ten problem — nie ma już flat map. Do tego czasu: świadomość, że zapisany YAML nie jest "ładny".

---

### CR-04: Config IO jest w `Application.cpp`, nie w `ConfigManager`

**Gdzie:** `Application.cpp` linki ~385, ~512–554, ~643–648, ~758–764.

```cpp
// W Application::initializeImGui():
ConfigDocument uiDocument = ConfigManager::CreateUiSettingsDocument();
loadUiSettingsDocument(uiDocument);
m_Config.fontScale = static_cast<float>(ConfigManager::GetDouble(uiDocument, "font_scale", 1.0));
// ...

// W Application::shutdownImGui():
ConfigDocument uiDocument = ConfigManager::CreateUiSettingsDocument();
// ...czyta, modyfikuje, zapisuje...
saveUiSettingsDocument(uiDocument);

// W Application::drawMainPanel():
if (saveUiSettings) {
    ConfigDocument uiDocument = ConfigManager::CreateUiSettingsDocument();
    loadUiSettingsDocument(uiDocument);
    // ...
    saveUiSettingsDocument(uiDocument);
}
```

**Problem:** Config IO jest rozsypane po 3–4 metodach Application. Slice E wymaga że
`ConfigManager` jest jedynym właścicielem config file IO. Te ścieżki trzeba będzie przepisać
tak czy tak — lepiej wiedzieć o tym zakresie pracy z góry.

**Fix (Slice E):** Przy przebudowie ConfigManager na instancję, Application przekazuje mu
`appResourcesDir` i `userConfigDir` — a potem woła wyłącznie `config.Get(key)` i `config.SetUser(key, value)`.
Metody `loadUiSettingsDocument` / `saveUiSettingsDocument` / `loadConfigFromPath` znikają.

---

## 🟡 Poważne — napraw w tej samej iteracji co napotkasz

### CR-05: `skipThisDispatch` nigdy nie jest resetowany do `false`

**Gdzie:** `EventBus.cpp` — `dispatchByType()` i `unsubscribeById()`

```cpp
// unsubscribeById podczas dispatch:
listener.pendingRemoval = true;
listener.skipThisDispatch = true;   // ← ustawiane

// dispatchByType sprawdza:
if (listener.skipThisDispatch) continue;

// Po dispatch — listener jest usuwany bo pendingRemoval = true
// ALE: skipThisDispatch nigdy nie jest resetowane do false
```

**Problem:** Flaga `skipThisDispatch` jest nigdy nie czyszczona. W obecnym kodzie nie jest to
aktywny bug — każdy listener z `skipThisDispatch = true` ma też `pendingRemoval = true`
i jest usuwany po dispatch. Ale jest to fragile: jeśli kiedyś `skipThisDispatch` zostanie
użyte bez `pendingRemoval` (np. przy "pause delivery"), listener będzie cicho ignorowany na zawsze.

**Fix (mały, teraz):**
```cpp
// W dispatchByType, po erase-remove:
for (auto &listener : list.listeners)
    listener.skipThisDispatch = false;
```
Albo — lepiej — upewnij się że `skipThisDispatch` jest zawsze ustawiane razem z `pendingRemoval`
i dodaj `static_assert` lub komentarz.

---

### CR-06: `Application::s_Instance` ustawiany dwukrotnie

**Gdzie:** `Application.cpp` linia 191 (konstruktor) i linia 218 (`createFromSpecification`):
```cpp
// Konstruktor:
s_Instance = this;   // linia 191

// createFromSpecification():
s_Instance = this;   // linia 218 — redundantne
```

**Problem:** Między linią 191 a `createFromSpecification()` jest wywołanie
`parseApplicationArguments()`. Jeśli cokolwiek w tej funkcji (lub statyczny inicjalizator)
wywołałoby `Application::Get()`, dostałoby częściowo-skonstruowaną instancję.
To jest init-order bug czekający na ujawnienie.

**Fix:**
```cpp
// Konstruktor — NIE ustawiaj s_Instance tutaj
// Application(int argc, char **argv):
//   m_Runtime.argc = argc; m_Runtime.argv = argv;
//   createFromSpecification(parseApplicationArguments(...));

// createFromSpecification() — ustawiaj s_Instance dopiero TU
// po TryMarkCreated() i zanim cokolwiek może wywołać Get()
```

---

### CR-07: `Application::Get()` używany jako service locator w panelach

**Gdzie:** `Presentation/Panels/Settings.cpp`, `TaskMonitorWindow.cpp`, `ProgressMonitorWindow.cpp`,
`EditorLayer.cpp` — łącznie ~12 wywołań.

```cpp
// TaskMonitorWindow.cpp:
auto &jobSystem = Application::Get().GetJobSystem();
// ProgressMonitorWindow.cpp:
auto &progressTracker = Application::Get().GetProgressTracker();
```

**Problem:** Panele są ściśle powiązane z `Application` jako service locatorem.
Nie można ich testować w izolacji, nie da się podmienić implementacji.
To jest świadomy dług — ale przy Slice E i Slice F sytuacja się pogorszy
(więcej serwisów będzie pobieranych przez `Application::Get()`), więc warto ustawić
wzorzec dependency injection dla nowych paneli.

**Rekomendacja:** Dla istniejących paneli zostaw tymczasowo. Dla NOWYCH paneli (NotificationHistoryPanel,
JobMonitorPanel, CapabilitySnapshotPanel) — przekazuj serwisy przez konstruktor:
```cpp
// Nowy wzorzec:
class JobMonitorPanel : public IPanel {
public:
    JobMonitorPanel(WeakRef<JobSystem> jobSystem, 
                    WeakRef<ProgressTracker> tracker)
        : m_JobSystem(jobSystem), m_Tracker(tracker) {}
private:
    WeakRef<JobSystem>      m_JobSystem;
    WeakRef<ProgressTracker> m_Tracker;
};
```

---

### CR-08: `ConfigManager` to zbiór metod statycznych — niezgodny z planem Slice E

**Gdzie:** `App/ConfigManager.hpp` — cała klasa.

**Problem:** Obecny `ConfigManager` to fasada bezstanowa (wszystkie metody `static`).
Plan Slice E wymaga instancji z 4-warstwowym storage, hot-reload, metodami `Get<T>()` / `SetUser<T>()`.
To nie jest refactoring — to całkowita wymiana interfejsu.

**Co to oznacza praktycznie:** Przy Slice E nie "rozszerzasz" ConfigManagera, tylko piszesz go od nowa.
Stary kod (Application.cpp który woła `ConfigManager::LoadFile`, `ConfigManager::GetDouble`)
musi być przepisany razem.

**Fix — zaplanuj zmianę jako 2 kroki:**
1. Nowy `ConfigManager` jako instancja (nowy plik lub nowa klasa)
2. Migracja wywołań w Application.cpp (CR-04)

---

## 🟢 Niskie — notuj, nie blokują

### CR-09: `EventPriority::sortByPriority` — kierunek sortowania jest poprawny, ale nieintuicyjny

**Gdzie:** `EventBus.cpp`:
```cpp
// Highest = 0, High = 1, Normal = 2, ..., Lowest = 4
// stable_sort ascending: Highest(0) < High(1) < ... — Highest FIRST ✓
return static_cast<int>(left.priority) < static_cast<int>(right.priority);
```

Kierunek jest **poprawny** — `Highest=0` sortuje się na początek.
Ale enum `Highest=0` jest nieoczywisty przy czytaniu sortowania.

**Sugestia:** Dodaj komentarz inline:
```cpp
// Ascending by enum value: Highest(0) → ... → Lowest(4). Lower value = higher priority = earlier delivery.
```

---

### CR-10: Font path hardcoded do Windows

**Gdzie:** `Application.cpp` linia ~623:
```cpp
const std::array<const char*, 3> preferredFonts = {
    "C:/Windows/Fonts/segoeui.ttf"
};
```

Na Linux silently falluje do ImGui domyślnej czcionki. Będzie to problem z AssetManager
(Slice H UI style). Na razie OK, ale przy cross-platform check bądź świadomy.

---

### CR-11: `drawMainPanel()` w Application — prototype UI code

**Gdzie:** `Application.cpp` `drawMainPanel()` — clear color, font scale slider, demo window checkbox.

To jest tymczasowy panel testowy który powinien zniknąć gdy pojawią się właściwe panele.
Zawiera bezpośredni zapis do config przez copy-edit-save pattern (CR-04).

**Nie ruszaj teraz** — zniknie przy Slice E cleanup.

---

### CR-12: `ApplicationLifecycleState::m_Running` nie jest `atomic`

**Gdzie:** `ApplicationLifecycle.hpp`.

`IsRunning()` jest wołane w pętli głównej (`while (m_Runtime.Running() && ...)`).
Na razie single-threaded access — OK. Przy T04/T05 gdzie wątki robocze mogą wołać Shutdown,
to może stać się data race. Świadomość na przyszłość.

---

### CR-13: `DebugLayer` jest pustą powłoką

**Gdzie:** `Debug/DebugLayer.cpp` — `OnAttach`, `OnDetach`, `OnImGuiRender` wszystkie puste.

To jest celowy placeholder. Slice F i Slice I mają dodać do niego panele.
Nie jest to bug — ale upewnij się że po Slice J `DebugLayer` naprawdę ma
`LogPanel`, `JobMonitorPanel`, `NotificationHistoryPanel`, `CapabilitySnapshotPanel`.

---

## Podsumowanie i kolejność napraw

| Priorytet | ID | Problem | Kiedy naprawić |
|-----------|-----|---------|---------------|
| 🔴 | CR-01 | EventBus → przenieść do Application | **Przed Slice F** |
| 🔴 | CR-02 | YAML sequences fail | **Przed pisaniem default.yaml** |
| 🔴 | CR-03 | YAML write flat keys | **Przy Slice E (rewrite ConfigManager)** |
| 🔴 | CR-04 | Config IO w Application.cpp | **Przy Slice E** |
| 🟡 | CR-05 | skipThisDispatch nie resetowany | **Teraz, 15 min fix** |
| 🟡 | CR-06 | s_Instance ustawiany 2x | **Teraz, 5 min fix** |
| 🟡 | CR-07 | Application::Get() service locator | **Nowe panele: inject; stare: świadomość** |
| 🟡 | CR-08 | ConfigManager static-only | **Przy Slice E: full rewrite** |
| 🟢 | CR-09 | Priority sort comment | **Przy przeglądzie — 2 min** |
| 🟢 | CR-10 | Font path hardcoded | **Przy AssetManager** |
| 🟢 | CR-11 | drawMainPanel prototype | **Przy Slice E cleanup** |
| 🟢 | CR-12 | m_Running non-atomic | **Świadomość, fix przy threading** |
| 🟢 | CR-13 | DebugLayer pusty | **Slice F / Slice I** |

---

## Rekomendowany plan działania przed Slice E

```
Krok 1 (15 min): Napraw CR-05 (skipThisDispatch reset) + CR-06 (s_Instance double-set)
    → Małe, izolowane zmiany. Nie naruszają niczego innego.
    → Dodaj test dla CR-05: subscribe, unsubscribe during dispatch, re-subscribe — sprawdź callback.

Krok 2 (2–3h): CR-01 — przenieś EventBus do Application
    → Zmień CoreLayer::InitializeSystems() na przyjmowanie WeakRef<EventBus>
    → Application tworzy EventBus PRZED setupDefaultLayers()
    → Application::GetEventBus() → zwraca m_EventBus (nie deleguje do CoreLayer)
    → CoreLayer::GetEventBus() może zostać jako accessor dla backward compat, ale nie OWNI już
    → Uruchom testy EventBus i JobSystem — powinny przejść bez zmian

Krok 3: Teraz zacznij Slice E (ConfigManager rewrite)
    → Nowa instancja-based klasa obsługuje CR-02, CR-03, CR-04 razem
```

---

## Co jest naprawdę dobrze zrobione

Żeby nie było jednostronnie — kilka rzeczy jest zrobionych solidnie:

- **EventBus threading contract** jest poprawny: JobSystem używa `Queue()` z worker threads,
  `ProcessQueue()` jest wołany tylko z main thread przez CoreLayer::OnUpdate. Nie ma wyścigów.
  
- **SubscriptionHandle RAII** jest poprawnie zaimplementowany — destruktor wywołuje unsubscribe,
  move semantics przenosi ownership, `m_Bus` jest WeakRef więc nie ma use-after-free jeśli
  EventBus zginie pierwszy.

- **JobRecord vs JobSnapshot** separacja jest dobra — UI widzi tylko snapshoty, record jest
  prywatny dla JobSystem. `m_RecordsMutex` chroni właściwy zakres.

- **JobSystem używa `Queue()` nie `Publish()`** z worker threads — threading contract jest
  spełniony.

- **ProgressTracker** jest read-only consumer EventBusu — poprawna separacja.

- **IJob / JobContext callbacks** zamiast bezpośrednich referencji do JobSystem —
  dobra enkapsulacja, JobContext jest niezależny od implementacji JobSystem.
