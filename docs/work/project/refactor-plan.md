# Defect Studio – Copilot Refactoring Instructions

> Nie opisuj w czacie co robisz. Nie wyjaśniaj zmian. Wykonuj je bezpośrednio.
> Każda SESJA to osobny PR lub commit. Nie łącz sesji.
> Przed każdą zmianą przeczytaj wskazane pliki. Stosuj styl kodu istniejący w projekcie.
> Konwencje projektu: `PascalCase` dla typów i publicznych metod, `camelCase` dla prywatnych metod,
> `m_PascalCase` dla prywatnych pól, `Ref<T>`/`Unique<T>`/`WeakRef<T>` zamiast surowych smart_ptr,
> `[[nodiscard]]` na każdej funkcji zwracającej wynik, `Result<T>` jako model błędów,
> brak anonimowych namespace – zamiast nich `static` przed funkcją pomocniczą.

---

## SESJA 1 – Rozbicie DemoLayer na osobne pliki

**Przeczytaj przed rozpoczęciem:**
`src/Demo/DemoLayer.hpp`, `src/Demo/DemoLayer.cpp`

### 1.1 Utwórz `src/Demo/DemoCommands.hpp`

Przenieś z anonimowego namespace w `DemoLayer.cpp` do nowego pliku:
- `class DemoValueDeltaCommand final : public ICommand`
- `class DemoSetValueCommand final : public ICommand`
- `class DemoCallbackCommand final : public ICommand`
- stałe `kCommandIncrement`, `kCommandDecrement`, `kCommandReset`, `kCommandUndo`, `kCommandRedo`, `kCommandNotify`, `kCommandMissingCapability`

Zmień stałe z `const CommandID` na `inline constexpr` i przenieś do namespace `DefectStudio::Demo`.
Plik nagłówkowy musi zawierać pełne definicje klas (nie są szablonami).

Utwórz `src/Demo/DemoCommands.cpp`:
- `#include "Core/dspch.hpp"` jako pierwsza linia
- `#include "Demo/DemoCommands.hpp"`
- Przenieś implementacje metod klas które mają implementację w .cpp (jeśli są)

### 1.2 Utwórz `src/Demo/DemoNotifications.hpp` i `DemoNotifications.cpp`

Z `DemoLayer.cpp` przenieś:
- Prywatną metodę `renderNotificationDemo()` jako metodę klasy `DemoNotificationsPanel`
- Wolne funkcje pomocnicze `toToastType(NotificationSeverity)` i `severityColor(NotificationSeverity)` jako `static` funkcje w `DemoNotifications.cpp`
- Pole `m_NotificationDemoCounter` jako prywatny member klasy

`DemoNotificationsPanel` przyjmuje w konstruktorze `Ref<Notifier> notifier` (nie `Application::Get()`).

Deklaracja klasy w `.hpp`:
```cpp
namespace DefectStudio::Demo
{
    class DemoNotificationsPanel
    {
    public:
        explicit DemoNotificationsPanel(Ref<Notifier> notifier);
        void Render();
    private:
        Ref<Notifier> m_Notifier;
        std::uint32_t m_NotificationDemoCounter = 0;
    };
}
```

### 1.3 Utwórz `src/Demo/DemoBackendRuntime.hpp` i `DemoBackendRuntime.cpp`

Z `DemoLayer.cpp` przenieś:
- Prywatne metody: `setupBackendRuntimeDemo()`, `renderBackendRuntimeDemo()`, `onBackendDemoKeyPressed()`, `executeBackendDemoChord()`, `appendBackendRuntimeLog()`
- Prywatne pola: `m_BackendDemoValue`, `m_BackendHotkeysEnabled`, `m_CommandPaletteSearch`, `m_BackendRuntimeLog`, `m_BackendUndoStack`, `m_BackendCommandRegistry`, `m_BackendKeymapResolver`, `m_BackendContextManager`, `m_BackendCommandPalette`

Klasa przyjmuje w konstruktorze `CapabilityService *capabilityService` (tymczasowo – zmienione w Sesji 6).

```cpp
namespace DefectStudio::Demo
{
    class DemoBackendRuntime
    {
    public:
        explicit DemoBackendRuntime(CapabilityService *capabilityService);
        ~DemoBackendRuntime();
        void OnEvent(Event &event);
        void Render();
    private:
        // wszystkie metody i pola przeniesione z DemoLayer
    };
}
```

### 1.4 Zaktualizuj `DemoLayer.hpp` i `DemoLayer.cpp`

`DemoLayer.hpp` po zmianach:
```cpp
namespace DefectStudio::Demo
{
    class EventDispatcherDemo;
    class EventBusDemo;
    class JobSystemDemo;
    class DemoNotificationsPanel;
    class DemoBackendRuntime;

    class DemoLayer final : public Layer
    {
    public:
        DemoLayer();
        ~DemoLayer();
        void OnAttach() override;
        void OnDetach() override;
        void OnEvent(Event &event) override;
        void OnImGuiRender() override;
    private:
        Ref<EventBus> m_DemoEventBus;
        Unique<EventDispatcherDemo> m_EventDispatcherDemo;
        Unique<EventBusDemo> m_EventBusDemo;
        Unique<JobSystemDemo> m_JobSystemDemo;
        Unique<DemoNotificationsPanel> m_NotificationsPanel;
        Unique<DemoBackendRuntime> m_BackendRuntime;
    };
}
```

`DemoLayer.cpp` po zmianach zawiera wyłącznie: konstruktor, destruktor, `OnAttach`, `OnDetach`, `OnEvent`, `OnImGuiRender` – każda z metod deleguje do odpowiednich obiektów składowych.

W `OnAttach` tymczasowo nadal używaj `Application::Get()` do pobrania `Notifier` i `CapabilityService` – to zostanie naprawione w Sesji 6. Dodaj komentarz `// TODO(Sesja 6): inject via BindRuntimeServices`.

Usuń `#include "ImGuiNotify.hpp"` z `DemoLayer.cpp` – zostaje tylko w `DemoNotifications.cpp`.

---

## SESJA 2 – Komentarze known limitations w UndoStack

**Przeczytaj przed rozpoczęciem:**
`src/Core/Undo/UndoStack.cpp`

### 2.1 Dokumentacja partial rollback

W `UndoStack::ApplyUndoRecord`, bezpośrednio przed pętlą `for`, dodaj:
```cpp
// Known limitation: if Undo fails mid-group, commands processed before the
// failure are already reverted while remaining commands are not. The stack
// index is not decremented on failure, leaving domain state partially reverted.
// Mitigation: implement Undo as an infallible operation in all ICommand subclasses.
```

### 2.2 Dokumentacja CancelGroup semantyki

W `UndoStack::CancelGroup`, bezpośrednio przed `m_GroupDepth = 0;`, dodaj:
```cpp
// Intentionally resets entire nesting depth to zero rather than decrementing.
// Cancel is atomic: the entire group hierarchy is abandoned, not just the
// outermost level. This is asymmetric with EndGroup by design.
```

---

## SESJA 3 – ICommand::Redo i naprawa ApplyRedoRecord

**Przeczytaj przed rozpoczęciem:**
`src/Core/Commands/Command.hpp`, `src/Core/Commands/Command.cpp`, `src/Core/Undo/UndoStack.cpp`

### 3.1 Dodaj `Redo` do `ICommand`

W `src/Core/Commands/Command.hpp`, po deklaracji `Undo`, dodaj:
```cpp
[[nodiscard]] virtual Result<void> Redo(CommandContext &context);
```

W `src/Core/Commands/Command.cpp`, po implementacji `ICommand::Undo`, dodaj:
```cpp
Result<void> ICommand::Redo(CommandContext &context)
{
    return Execute(context);
}
```

### 3.2 Napraw `ApplyRedoRecord`

W `src/Core/Undo/UndoStack.cpp`, w funkcji `ApplyRedoRecord`, zmień:
```cpp
Result<void> result = command->Execute(context);
```
Na:
```cpp
Result<void> result = command->Redo(context);
```

### 3.3 Napraw `DemoSetValueCommand` (wymaga Redo override)

W `src/Demo/DemoCommands.hpp` (po Sesji 1), w klasie `DemoSetValueCommand`, dodaj:
```cpp
Result<void> Redo(CommandContext &context) override
{
    (void)context;
    *m_Value = m_NewValue;
    return {};
}
```
Ta klasa jest nieidempotentna – `Execute` zapisuje `m_OldValue = *m_Value` przed zmianą, więc drugie wywołanie `Execute` nadpisałoby `m_OldValue` błędną wartością. `Redo` wykonuje zmianę bez side effectu na `m_OldValue`.

---

## SESJA 4 – Notifier: Publish → Queue

**Przeczytaj przed rozpoczęciem:**
`src/Core/Notifications/Notifier.cpp`

### 4.1 Zmiana w `appendAndPublish`

W `src/Core/Notifications/Notifier.cpp`, na końcu funkcji `appendAndPublish`, zmień:
```cpp
if (m_EventBus)
    m_EventBus->Publish(NotificationEvent{std::move(notification)});
```
Na:
```cpp
if (m_EventBus)
    m_EventBus->Queue(NotificationEvent{std::move(notification)});
```

---

## SESJA 5 – DemoLayer i DebugLayer wyłączone z Dist build

**Przeczytaj przed rozpoczęciem:**
`src/App/Application.cpp` – funkcja `setupDefaultLayers`

### 5.1 Otocz DemoLayer i DebugLayer

W `setupDefaultLayers()`, znajdź linie pushujące `Demo::DemoLayer` i `Debug::DebugLayer`. Otocz każdą z nich:
```cpp
#ifndef DS_DIST
    m_LayerStack.PushLayer(CreateUnique<Demo::DemoLayer>());
#endif
```
```cpp
#ifndef DS_DIST
    m_LayerStack.PushLayer(CreateUnique<Debug::DebugLayer>());
#endif
```

---

## SESJA 6 – Uniezależnienie Core od App

**Przeczytaj przed rozpoczęciem:**
`src/App/Events/JobEvents.hpp`,
`src/Core/JobSystem/JobSystem.cpp`,
`src/Core/ProgressTrackingSystem/ProgressTracker.cpp`,
`src/Core/CoreLayer.cpp`,
`src/Core/CoreLayer.hpp`

### 6.1 Skopiuj `JobEvents.hpp` do `Core`

Skopiuj `src/App/Events/JobEvents.hpp` do `src/Core/JobSystem/JobEvents.hpp`.

W nowym pliku nie zmieniaj namespace ani zawartości – tylko aktualizuj include guard/pragma once jeśli potrzeba.

### 6.2 Zaktualizuj includes

W każdym z poniższych plików zamień `#include "App/Events/JobEvents.hpp"` na `#include "Core/JobSystem/JobEvents.hpp"`:
- `src/Core/JobSystem/JobSystem.cpp`
- `src/Core/ProgressTrackingSystem/ProgressTracker.cpp`
- `src/Core/CoreLayer.cpp`

Przeszukaj `grep -r "App/Events/JobEvents.hpp" src/` i zaktualizuj każde znalezione miejsce.

### 6.3 Usuń `App/Events/JobEvents.hpp`

Usuń `src/App/Events/JobEvents.hpp`.

Jeśli plik jest includowany w `src/App/Application.cpp` lub innych plikach `App/`, zaktualizuj tam include na `Core/JobSystem/JobEvents.hpp`.

### 6.4 Ogranicz include `ApplicationConfigEvents.hpp` w CoreLayer

W `src/Core/CoreLayer.cpp` sprawdź które typy z `App/Events/ApplicationConfigEvents.hpp` są używane.
Jeśli używane są tylko pola `config.jobs` lub `config.eventQueue` z eventu `AppEvents::Config::Applied`:

Zostaw include `App/Events/ApplicationConfigEvents.hpp` w `CoreLayer.cpp` z komentarzem:
```cpp
// TODO: wydzielić CoreConfigEvents z ApplicationConfigEvents, żeby Core nie zależało od App/Events.
// Aktualnie CoreLayer używa tylko jobs i eventQueue z AppEvents::Config::Applied.
```

Nie usuwaj tego include teraz – wymaga osobnej decyzji architektonicznej o lokalizacji event typów.

### 6.5 Zaktualizuj `DemoLayer.cpp` (po Sesji 1)

W `src/Demo/DemoBackendRuntime.cpp`, zmień sposób pobierania `CapabilityService`:
- Usuń `#include "App/Application.hpp"` jeśli był używany tylko do tego
- Konstruktor `DemoBackendRuntime` przyjmuje `CapabilityService *capabilityService` – już tak jest po Sesji 1
- W `DemoLayer::OnAttach` przekaż: `CreateUnique<DemoBackendRuntime>(&Application::Get().GetCapabilityService())`

---

## SESJA 7 – CapabilityService::Require → Result\<void\>

**Przeczytaj przed rozpoczęciem:**
`src/Core/Capabilities/CapabilityService.hpp`,
`src/Core/Capabilities/CapabilityService.cpp`,
`src/Demo/DemoLayer.cpp` lub `src/Demo/DemoNotifications.cpp` (po Sesji 1)

### 7.1 Zmień sygnaturę `Require`

W `src/Core/Capabilities/CapabilityService.hpp`, zmień:
```cpp
void Require(const std::string &name) const;
```
Na:
```cpp
[[nodiscard]] Result<void> Require(const std::string &name) const;
```

Dodaj include jeśli brakuje:
```cpp
#include "Core/Diagnostics/StructuredError.hpp"
```

### 7.2 Zmień implementację `Require`

W `src/Core/Capabilities/CapabilityService.cpp`, zmień implementację:
```cpp
Result<void> CapabilityService::Require(const std::string &name) const
{
    if (!IsAvailable(name))
    {
        DS_LOG_ERROR("CapabilityService: required capability '{}' is not available", name);
        return StructuredError{
            ErrorCategory::Capability,
            Severity::Error,
            "Required capability is not available.",
            "Capability '" + name + "' was required but is not registered or is disabled.",
            "Check capability registration order and ensure LockAfterStartup() was called.",
            "CapabilityService",
            "capability.require.unavailable"};
    }
    return {};
}
```

Usuń wszelki kod rzucający wyjątek `CapabilityNotAvailableException`. Jeśli klasa `CapabilityNotAvailableException` jest zdefiniowana tylko w tym pliku – usuń ją. Jeśli jest w osobnym pliku – zostaw plik ale usuń użycie.

### 7.3 Zaktualizuj callsites

W `src/Demo/DemoNotifications.cpp` (lub `DemoLayer.cpp` przed Sesją 1), w przyciskach `Require`:

Zamień `try/catch` na sprawdzenie `Result`:
```cpp
if (ImGui::Button("Require ui.notifications"))
{
    auto result = capabilityService.Require("ui.notifications");
    if (result)
        m_Notifier->Info("Capability available", "ui.notifications requirement satisfied", NotificationCategory::Capability);
    else
        m_Notifier->Error("Capability missing", result.Error().userMessage, NotificationCategory::Capability);
}
```

Analogicznie dla `Require("demo.missing")`.

---

## SESJA 8 – Usunięcie Application::Get() z Settings i DemoLayer

**Przeczytaj przed rozpoczęciem:**
`src/Presentation/Panels/Settings.cpp`,
`src/Presentation/ImGuiLayer.hpp`,
`src/App/Application.hpp`

### 8.1 `Settings.cpp` – wstrzykiwanie przez `ImGuiLayerRuntime`

Sprawdź które wywołania `Application::Get()` są w `Settings.cpp`. Są to:
- `Application::Get().GetEventBus()` → dostępne przez `ImGuiLayerRuntime::eventBus` (już jest w runtime)
- `Application::Get().GetJobSystem()` → wymaga rozszerzenia `ImGuiLayerRuntime`
- `Application::Get().GetConfigManager()` → dostępne przez `ImGuiLayerRuntime::configManager` (już jest)

W `src/Presentation/ImGuiLayer.hpp`, w struct `ImGuiLayerRuntime`, dodaj:
```cpp
WeakRef<JobSystem> jobSystem;
```

W `src/App/Application.cpp`, w miejscu gdzie tworzony jest `ImGuiLayerRuntime`, uzupełnij:
```cpp
runtime.jobSystem = m_CoreLayer->GetJobSystem();  // lub odpowiednie miejsce
```

W `Settings.cpp`, zamień każde `Application::Get().GetXxx()` na odpowiedni member pobrany przez `ImGuiLayer` z `m_Runtime` lub przekazany do panelu przez setter/konstruktor. Konkretne zmiany zależą od faktycznych wywołań – sprawdź cały plik `Settings.cpp` grep-em i zastąp każde `Application::Get()`.

### 8.2 `DemoLayer.cpp` / `DemoNotifications.cpp` – usunięcie Application::Get()

Po Sesji 1, w `DemoNotificationsPanel::Render()`, zamień:
```cpp
auto &application = Application::Get();
auto &notifier = application.GetNotifier();
auto &capabilityRegistry = application.GetCapabilityRegistry();
auto &capabilityService = application.GetCapabilityService();
```
Na użycie wstrzykniętych memberów klasy (`m_Notifier`, `m_CapabilityService`).

`DemoNotificationsPanel` rozszerz o:
```cpp
class DemoNotificationsPanel
{
public:
    DemoNotificationsPanel(Ref<Notifier> notifier, CapabilityService *capabilityService);
    // ...
private:
    Ref<Notifier> m_Notifier;
    CapabilityService *m_CapabilityService = nullptr;  // tymczasowo raw ptr; zmienione w Sesji 11
};
```

W `DemoLayer::OnAttach()` przekaż zależności:
```cpp
m_NotificationsPanel = CreateUnique<DemoNotificationsPanel>(
    Application::Get().GetNotifierRef(),       // Ref<Notifier>
    &Application::Get().GetCapabilityService() // tymczasowo
);
```

Wymagane: w `Application` musi istnieć `GetNotifier()` zwracające `Notifier &` (jest) i opcjonalnie `GetNotifierRef()` zwracające `Ref<Notifier>` (dodaj jeśli brakuje).

W `DemoBackendRuntime`, wywołania `Application::Get().GetNotifier().Notify(...)` zamień na przyjęty przez konstruktor `Notifier *m_Notifier` (tymczasowo raw ptr) lub wstrzyknięty `Ref<Notifier>`.

---

## SESJA 9 – NotificationCenter w Demo + ImGuiLayer jako właściciel toastów

**Przeczytaj przed rozpoczęciem:**
`src/Core/Notifications/NotificationCenter.hpp`,
`src/Core/Notifications/NotificationCenter.cpp`,
`src/Core/Notifications/NotificationHistory.hpp`,
`src/Presentation/ImGuiLayer.hpp`,
`src/Presentation/ImGuiLayer.cpp`,
`src/Events/NotificationEvents.hpp`

### 9.1 `ImGuiLayer` subskrybuje `NotificationEvent`

W `src/Presentation/ImGuiLayer.hpp` dodaj include:
```cpp
#include "Core/Notifications/Notification.hpp"
```

Dodaj prywatne members:
```cpp
std::deque<Notification> m_PendingToasts;
```

Dodaj deklarację prywatnej metody:
```cpp
void onNotificationEvent(const NotificationEvent &event);
```

W `src/Presentation/ImGuiLayer.cpp`:

Dodaj include:
```cpp
#include "Events/NotificationEvents.hpp"
```

W metodzie `bindEventBus` (lub `OnAttach`), po podpięciu istniejących subskrypcji, dodaj:
```cpp
AddSubscription(eventBus->Subscribe<NotificationEvent>(
    std::bind_front(&ImGuiLayer::onNotificationEvent, this)));
```

Dodaj implementację:
```cpp
void ImGuiLayer::onNotificationEvent(const NotificationEvent &event)
{
    m_PendingToasts.push_back(event.notification);
}
```

### 9.2 `ImGuiLayer::OnImGuiRender` renderuje tosty

W `src/Presentation/ImGuiLayer.cpp`, w metodzie `OnImGuiRender` (lub w odpowiednim miejscu – po `ImGui::NewFrame`, przed `ImGui::Render`), dodaj obsługę kolejki toastów:

```cpp
while (!m_PendingToasts.empty())
{
    const Notification &notification = m_PendingToasts.front();
    ImGuiToastType toastType = ImGuiToastType::Info;
    switch (notification.severity)
    {
        case NotificationSeverity::Warn:     toastType = ImGuiToastType::Warning; break;
        case NotificationSeverity::Error:
        case NotificationSeverity::Critical: toastType = ImGuiToastType::Error;   break;
        default: break;
    }
    ImGui::InsertNotification({toastType,
        static_cast<int>(notification.displayDurationMs),
        "%s", notification.message.c_str()});
    m_PendingToasts.pop_front();
}
```

Dodaj niezbędny include `ImGuiNotify.hpp` do `ImGuiLayer.cpp` jeśli go tam nie ma (przenieś z `DemoLayer.cpp`).

Dodaj `#include <deque>` do `ImGuiLayer.cpp`.

### 9.3 Usuń `ImGui::InsertNotification` z `DemoNotifications.cpp`

W `DemoNotificationsPanel::Render()` (po Sesji 1), w lambdzie `emitNotification`:
- Usuń linię `ImGui::InsertNotification({toToastType(severity), 3000, "%s", resolvedMessage.c_str()});`
- Lambda wywołuje tylko `m_Notifier->Info/Warning/Error/Critical(...)` – toast pojawi się przez EventBus → ImGuiLayer

Usuń lokalną funkcję `toToastType` z `DemoNotifications.cpp` jeśli była używana tylko dla `InsertNotification`.

Usuń `#include "ImGuiNotify.hpp"` z `DemoNotifications.cpp`.

### 9.4 Dodaj przykład `NotificationCenter` w Demo

W `src/Demo/DemoNotifications.hpp` dodaj members:
```cpp
private:
    Unique<NotificationCenter> m_NotificationCenter;
    NotificationHistory m_NotificationHistory;
    Ref<EventBus> m_EventBus;
```

Zmień konstruktor:
```cpp
DemoNotificationsPanel(Ref<Notifier> notifier, CapabilityService *capabilityService, Ref<EventBus> eventBus);
```

W `DemoNotifications.cpp`, w konstruktorze:
```cpp
DemoNotificationsPanel::DemoNotificationsPanel(
    Ref<Notifier> notifier,
    CapabilityService *capabilityService,
    Ref<EventBus> eventBus)
    : m_Notifier(std::move(notifier))
    , m_CapabilityService(capabilityService)
    , m_EventBus(std::move(eventBus))
{
    if (m_EventBus)
    {
        m_NotificationCenter = CreateUnique<NotificationCenter>(m_EventBus);
        m_NotificationCenter->RegisterListener([this](const Notification &notification) {
            m_NotificationHistory.Append(notification);
        });
    }
}
```

W `Render()`, zastąp sekcję `SeparatorText("Recent notifier history")` na:
```cpp
ImGui::SeparatorText("NotificationCenter history");
const auto &entries = m_NotificationHistory.GetAll();
ImGui::Text("History entries: %zu", entries.size());
if (ImGui::BeginChild("##notification_history", ImVec2(0.0f, 180.0f), true))
{
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
    {
        ImGui::BulletText("[%s] %s: %s",
            ToString(it->severity), it->title.c_str(), it->message.c_str());
    }
}
ImGui::EndChild();
```

Zaktualizuj `DemoLayer::OnAttach()` aby przekazywał `m_DemoEventBus` do konstruktora panelu.

---

## SESJA 10 – CommandRegistry::Notify → notifyObservers

**Przeczytaj przed rozpoczęciem:**
`src/Core/Commands/CommandRegistry.hpp`,
`src/Core/Commands/CommandRegistry.cpp`

### 10.1 Zmień nazwę prywatnej metody

W `src/Core/Commands/CommandRegistry.hpp`, w sekcji `private`, zmień:
```cpp
void Notify(const CommandExecutionEvent &event) const;
```
Na:
```cpp
void notifyObservers(const CommandExecutionEvent &event) const;
```

W `src/Core/Commands/CommandRegistry.cpp`:
- Zmień definicję metody z `CommandRegistry::Notify` na `CommandRegistry::notifyObservers`
- Zamień każde wywołanie `Notify(` wewnątrz `CommandRegistry.cpp` na `notifyObservers(`

---

## SESJA 11 – CommandRegistry: logowanie błędów rejestracji

**Przeczytaj przed rozpoczęciem:**
`src/Core/Commands/CommandRegistry.cpp`

### 11.1 Dodaj DS_LOG_ERROR do każdego punktu błędu w `Register`

W funkcji `CommandRegistry::Register`, przed każdym `return` zwracającym błąd, dodaj logowanie. Przykład dla pustego ID:
```cpp
{
    DS_LOG_ERROR("CommandRegistry::Register failed [command.register.empty_id]: CommandMeta.id is empty");
    return StructuredError{ /* ... istniejące pola ... */ };
}
```

Dla duplikatu ID:
```cpp
DS_LOG_ERROR("CommandRegistry::Register failed [command.register.duplicate_id]: id='{}' already registered", meta.id.value);
```

Dla brakującej factory:
```cpp
DS_LOG_ERROR("CommandRegistry::Register failed [command.register.missing_factory]: id='{}'", meta.id.value);
```

Nie zmieniaj struktury `StructuredError` – tylko dodaj `DS_LOG_ERROR` bezpośrednio przed `return`.

---

## SESJA 12 – raw-ptr → WeakRef w CommandRegistry

**Przeczytaj przed rozpoczęciem:**
`src/Core/Commands/CommandRegistry.hpp`,
`src/Core/Commands/CommandRegistry.cpp`,
`src/Core/Utils/Memory.hpp`

### 12.1 Zmień typy pól

W `src/Core/Commands/CommandRegistry.hpp`, zmień:
```cpp
CapabilityService *m_CapabilityService = nullptr;
UndoStack *m_UndoStack = nullptr;
```
Na:
```cpp
WeakRef<CapabilityService> m_CapabilityService;
WeakRef<UndoStack> m_UndoStack;
```

### 12.2 Zmień settery

Zmień:
```cpp
void SetCapabilityService(CapabilityService *capabilityService) noexcept;
void SetUndoStack(UndoStack *undoStack) noexcept;
```
Na:
```cpp
void SetCapabilityService(WeakRef<CapabilityService> capabilityService) noexcept;
void SetUndoStack(WeakRef<UndoStack> undoStack) noexcept;
```

### 12.3 Zmień konstruktor

Zmień:
```cpp
explicit CommandRegistry(CapabilityService *capabilityService = nullptr);
```
Na:
```cpp
explicit CommandRegistry(WeakRef<CapabilityService> capabilityService = {});
```

### 12.4 Zaktualizuj implementację w `CommandRegistry.cpp`

Wszędzie gdzie kod sprawdza `m_CapabilityService != nullptr`, zmień na `!m_CapabilityService.expired()`.
Wszędzie gdzie kod wywołuje metody przez `m_CapabilityService->`, dodaj `lock()`:
```cpp
if (auto service = m_CapabilityService.lock())
    service->IsAvailable(name);
```

Analogicznie dla `m_UndoStack`.

### 12.5 Zaktualizuj callsite w `DemoLayer.cpp` / `DemoBackendRuntime.cpp`

Zmień:
```cpp
m_BackendCommandRegistry = CreateUnique<CommandRegistry>(&Application::Get().GetCapabilityService());
m_BackendCommandRegistry->SetUndoStack(m_BackendUndoStack.get());
```
Na:
```cpp
// CapabilityService jest zarządzany przez Application jako Ref<> – przekazuj WeakRef
m_BackendCommandRegistry = CreateUnique<CommandRegistry>(Application::Get().GetCapabilityServiceRef());
m_BackendCommandRegistry->SetUndoStack(CreateWeakRef(m_BackendUndoStack));
```

Wymagane: `Application` musi mieć `GetCapabilityServiceRef()` zwracające `WeakRef<CapabilityService>`. Dodaj jeśli brakuje.

---

## SESJA 13 – CommandFactory: std::unique_ptr → Unique\<ICommand\>

**Przeczytaj przed rozpoczęciem:**
`src/Core/Commands/CommandRegistry.hpp`,
`src/Demo/DemoCommands.hpp` (po Sesji 1)

### 13.1 Zmień alias `CommandFactory`

W `src/Core/Commands/CommandRegistry.hpp`, zmień:
```cpp
using CommandFactory = std::function<std::unique_ptr<ICommand>(CommandContext &)>;
```
Na:
```cpp
using CommandFactory = std::function<Unique<ICommand>(CommandContext &)>;
```

### 13.2 Zaktualizuj DemoCommands

W `src/Demo/DemoBackendRuntime.cpp`, w każdym wywołaniu `registerCommand`, zmień factories z:
```cpp
[this](CommandContext &) {
    return std::make_unique<DemoValueDeltaCommand>(...);
}
```
Na:
```cpp
[this](CommandContext &) -> Unique<ICommand> {
    return CreateUnique<DemoValueDeltaCommand>(...);
}
```

Zmień wszystkie `std::make_unique<...>` na `CreateUnique<...>` w plikach Demo.

---

## SESJA 14 – Rejestracja komend przez bind zamiast inline-lambda

**Przeczytaj przed rozpoczęciem:**
`src/Demo/DemoBackendRuntime.cpp` (po Sesji 1 i 13)

### 14.1 Wyodrębnij metody fabryki komend

W `src/Demo/DemoBackendRuntime.hpp`, dodaj prywatne metody fabryki:
```cpp
private:
    [[nodiscard]] Unique<ICommand> createIncrementCommand(CommandContext &context);
    [[nodiscard]] Unique<ICommand> createDecrementCommand(CommandContext &context);
    [[nodiscard]] Unique<ICommand> createResetCommand(CommandContext &context);
    [[nodiscard]] Unique<ICommand> createUndoCommand(CommandContext &context);
    [[nodiscard]] Unique<ICommand> createRedoCommand(CommandContext &context);
    [[nodiscard]] Unique<ICommand> createNotifyCommand(CommandContext &context);
    [[nodiscard]] Unique<ICommand> createMissingCapabilityCommand(CommandContext &context);
```

### 14.2 Zaimplementuj metody fabryki

W `src/Demo/DemoBackendRuntime.cpp`, dodaj implementacje. Przykład:
```cpp
Unique<ICommand> DemoBackendRuntime::createIncrementCommand(CommandContext &)
{
    return CreateUnique<DemoValueDeltaCommand>(m_BackendDemoValue, 1, "Increment demo value");
}
```

### 14.3 Zmień rejestrację komend

W `setupBackendRuntimeDemo()`, zamień lambdy na `std::bind_front`:
```cpp
registerCommand(
    CommandMeta{kCommandIncrement, "Increment demo value", "Demo",
        "Mutates demo state through CommandRegistry and pushes UndoStack.",
        {}, CommandFlags::Undoable},
    std::bind_front(&DemoBackendRuntime::createIncrementCommand, this));
```

Analogicznie dla wszystkich innych komend. Nie używaj `[this](CommandContext &ctx)` – używaj `std::bind_front`.

---

## SESJA 15 – Rozbicie KeyBinding.hpp na osobne pliki

**Przeczytaj przed rozpoczęciem:**
`src/Core/Input/KeyBinding.hpp`,
`src/Core/Input/KeyBinding.cpp`

### 15.1 Utwórz `src/Core/Input/KeyChord.hpp`

Przenieś z `KeyBinding.hpp`:
- `enum class KeyModifiers`
- `operator|`, `operator&`, `HasModifier` dla `KeyModifiers`
- `struct KeyChord`
- Funkcja `ToString(const KeyChord &)`

### 15.2 Utwórz `src/Core/Input/ContextExpr.hpp` i `ContextExpr.cpp`

Przenieś klasę `ContextExpr`. Wolne funkcje pomocnicze `splitAndExpression` i `trim` zostają w `ContextExpr.cpp` jako `static`.

### 15.3 Utwórz `src/Core/Input/ContextManager.hpp` i `ContextManager.cpp`

Przenieś klasę `ContextManager`. Plik `.hpp` include-uje tylko `<string>`, `<vector>`.

### 15.4 Utwórz `src/Core/Input/KeymapResolver.hpp` i `KeymapResolver.cpp`

Przenieś:
- `enum class KeymapLayer`
- `enum class KeyBindingConflictType`
- `struct KeyBinding`
- `struct KeyBindingConflict`
- `class KeymapResolver`

`KeymapResolver.hpp` include-uje `KeyChord.hpp` i `ContextExpr.hpp`.

### 15.5 Utwórz `src/Core/Input/KeyInputProcessor.hpp` i `KeyInputProcessor.cpp`

Przenieś:
- `struct KeyInputResult`
- `class KeyInputProcessor`

### 15.6 Zaktualizuj `KeyBinding.hpp`

`KeyBinding.hpp` zostaje jako convenience umbrella header:
```cpp
#pragma once
// Convenience header – includes all Input subsystem components.
#include "Core/Input/KeyChord.hpp"
#include "Core/Input/ContextExpr.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Input/KeyInputProcessor.hpp"
```

Przenieś implementacje z `KeyBinding.cpp` do odpowiednich nowych plików `.cpp`. Każdy nowy `.cpp` zaczyna się od `#include "Core/dspch.hpp"`.

`KeyBinding.cpp` usuń lub zostaw pusty z komentarzem.

Anonimowe namespace w nowych plikach `.cpp` zastąp funkcjami `static`.

---

## SESJA 16 – Rejestracja globalnych skrótów klawiaturowych

**Przeczytaj przed rozpoczęciem:**
`src/Core/CoreLayer.hpp`,
`src/Core/CoreLayer.cpp`,
`src/Core/Commands/Command.hpp`,
`src/Core/Input/KeymapResolver.hpp`

### 16.1 Utwórz komendy systemowe

W `src/Core/Commands/` utwórz `SystemCommands.hpp` i `SystemCommands.cpp`.

Zdefiniuj w namespace `DefectStudio`:
```cpp
// Identyfikatory komend systemowych jako inline constexpr
inline constexpr CommandID kCmdEditUndo{"edit.undo"};
inline constexpr CommandID kCmdEditRedo{"edit.redo"};
inline constexpr CommandID kCmdAppCommandPalette{"app.command_palette"};
inline constexpr CommandID kCmdAppQuit{"app.quit"};
inline constexpr CommandID kCmdProjectSave{"project.save"};
```

Zdefiniuj klasy komend. Dla `edit.undo` i `edit.redo` klasy przyjmują `UndoStack &` w konstruktorze:
```cpp
class UndoCommand final : public ICommand
{
public:
    explicit UndoCommand(UndoStack &stack) : m_Stack(stack) {}
    Result<void> Execute(CommandContext &context) override
    {
        return m_Stack.Undo(std::move(context));
    }
    std::string Description() const override { return "Undo"; }
private:
    UndoStack &m_Stack;
};
```

Analogicznie `RedoCommand`. Dla `QuitCommand` używa `EventBus` do emitowania eventu shutdown.

### 16.2 Zarejestruj komendy i bindingi w `CoreLayer`

`CoreLayer` już ma dostęp do `CommandRegistry`, `KeymapResolver`, `UndoStack`, `EventBus`.

W `CoreLayer::OnAttach()` lub dedykowanej prywatnej metodzie `registerSystemCommands()`, dodaj rejestrację:

```cpp
void CoreLayer::registerSystemCommands()
{
    if (!m_CommandRegistry || !m_KeymapResolver)
        return;

    // edit.undo
    auto undoResult = m_CommandRegistry->Register(
        CommandMeta{kCmdEditUndo, "Undo", "Edit", "Undo the last action",
            {}, CommandFlags::HiddenFromPalette},
        [this](CommandContext &) -> Unique<ICommand> {
            return CreateUnique<UndoCommand>(*m_UndoStack);
        });
    if (!undoResult)
        DS_LOG_ERROR("Failed to register edit.undo: {}", undoResult.Error().technicalDetails);

    // edit.redo
    auto redoResult = m_CommandRegistry->Register(
        CommandMeta{kCmdEditRedo, "Redo", "Edit", "Redo the last undone action",
            {}, CommandFlags::HiddenFromPalette},
        [this](CommandContext &) -> Unique<ICommand> {
            return CreateUnique<RedoCommand>(*m_UndoStack);
        });
    if (!redoResult)
        DS_LOG_ERROR("Failed to register edit.redo: {}", redoResult.Error().technicalDetails);

    // app.command_palette, app.quit, project.save – analogicznie

    // Bindingi
    auto registerBinding = [this](KeyBinding binding) {
        auto result = m_KeymapResolver->RegisterBinding(std::move(binding));
        if (!result)
            DS_LOG_WARN("Keybinding conflict: {}", result.Error().technicalDetails);
    };

    registerBinding(KeyBinding{
        "system.undo",
        KeyChord{KeyCode::Z, KeyModifiers::Ctrl},
        kCmdEditUndo, {}, KeymapLayer::Global, true});

    registerBinding(KeyBinding{
        "system.redo",
        KeyChord{KeyCode::Y, KeyModifiers::Ctrl},
        kCmdEditRedo, {}, KeymapLayer::Global, true});

    registerBinding(KeyBinding{
        "system.command_palette",
        KeyChord{KeyCode::P, KeyModifiers::Ctrl | KeyModifiers::Shift},
        kCmdAppCommandPalette, {}, KeymapLayer::Global, true});

    registerBinding(KeyBinding{
        "system.quit",
        KeyChord{KeyCode::W, KeyModifiers::Ctrl | KeyModifiers::Shift},
        kCmdAppQuit, {}, KeymapLayer::Global, true});

    registerBinding(KeyBinding{
        "system.save",
        KeyChord{KeyCode::S, KeyModifiers::Ctrl},
        kCmdProjectSave, {}, KeymapLayer::Global, true});
}
```

Wywołaj `registerSystemCommands()` na końcu `OnAttach()`.

---

## SESJA 17 – Schowanie CapabilityRegistry za CapabilityService

**Przeczytaj przed rozpoczęciem:**
`src/Core/Capabilities/CapabilityService.hpp`,
`src/Core/Capabilities/CapabilityService.cpp`,
`src/Core/Capabilities/CapabilityRegistry.hpp`,
`src/App/Application.hpp`,
`src/App/Application.cpp`

### 17.1 Rozszerz `CapabilityService`

W `src/Core/Capabilities/CapabilityService.hpp`:

Dodaj forward declaration (zamiast include):
```cpp
class CapabilityRegistry;
```
Usuń `#include "Core/Capabilities/CapabilityRegistry.hpp"` z nagłówka.

Zmień konstruktor i member:
```cpp
class CapabilityService
{
public:
    CapabilityService();    // domyślny – tworzy własny Registry
    ~CapabilityService();   // potrzebny dla Unique<> z forward declaration

    // Startup API (wywołaj przed LockAfterStartup)
    void RegisterCapability(CapabilityEntry capability);
    void LockAfterStartup();
    [[nodiscard]] bool IsLocked() const;
    [[nodiscard]] std::vector<CapabilityEntry> GetAll() const;

    // Runtime API
    [[nodiscard]] bool IsAvailable(const std::string &name) const;
    [[nodiscard]] Result<void> Require(const std::string &name) const;

private:
    Unique<CapabilityRegistry> m_Registry;
};
```

### 17.2 Zaktualizuj `CapabilityService.cpp`

Dodaj `#include "Core/Capabilities/CapabilityRegistry.hpp"` do `.cpp` (nie do `.hpp`).

Zmień konstruktor:
```cpp
CapabilityService::CapabilityService()
    : m_Registry(CreateUnique<CapabilityRegistry>())
{
}

CapabilityService::~CapabilityService() = default;
```

Dodaj delegacje:
```cpp
void CapabilityService::RegisterCapability(CapabilityEntry capability)
{
    m_Registry->RegisterCapability(std::move(capability));
}

void CapabilityService::LockAfterStartup()
{
    m_Registry->LockAfterStartup();
}

bool CapabilityService::IsLocked() const
{
    return m_Registry->IsLocked();
}

std::vector<CapabilityEntry> CapabilityService::GetAll() const
{
    return m_Registry->GetAll();
}
```

Zaktualizuj `IsAvailable` i `Require` aby używały `m_Registry->` (pointer, operator`->`).

### 17.3 Zaktualizuj `Application`

W `src/App/Application.hpp`:
- Usuń `Ref<CapabilityRegistry> m_CapabilityRegistry;`
- Usuń getter `[[nodiscard]] CapabilityRegistry &GetCapabilityRegistry();`
- Usuń forward declaration `class CapabilityRegistry;` jeśli nie jest używana gdzie indziej

W `src/App/Application.cpp`:
- Usuń `#include "Core/Capabilities/CapabilityRegistry.hpp"`
- Zmień inicjalizację z:
  ```cpp
  m_CapabilityRegistry = CreateRef<CapabilityRegistry>();
  m_CapabilityService = CreateRef<CapabilityService>(*m_CapabilityRegistry);
  ```
  Na:
  ```cpp
  m_CapabilityService = CreateRef<CapabilityService>();
  ```
- Każde `m_CapabilityRegistry->RegisterCapability(...)` zmień na `m_CapabilityService->RegisterCapability(...)`
- Każde `m_CapabilityRegistry->LockAfterStartup()` zmień na `m_CapabilityService->LockAfterStartup()`
- Usuń implementację `GetCapabilityRegistry()`

W `src/Demo/DemoNotifications.cpp` (po Sesji 9), usuń odwołania do `capabilityRegistry`. Zamień `capabilityRegistry.IsLocked()` i `capabilityRegistry.GetAll()` na `m_CapabilityService->IsLocked()` i `m_CapabilityService->GetAll()`.

---

## SESJA 18 – Przeniesienie inicjalizacji do właściwych funkcji

**Przeczytaj przed rozpoczęciem:**
`src/App/Application.cpp` – funkcje `initializeEventInfrastructure` i `initializeCoreRuntimeServices`

### 18.1 Przenieś inicjalizację

Sprawdź czy `m_CapabilityService` i `m_Notifier` są tworzone w `initializeEventInfrastructure()`.
Jeśli tak – przenieś ich inicjalizację do `initializeCoreRuntimeServices()`.

Kolejność w `initializeCoreRuntimeServices()` po zmianie:
1. `m_CapabilityService = CreateRef<CapabilityService>();`
2. Rejestracje capabilities (`m_CapabilityService->RegisterCapability(...)`)
3. `m_CapabilityService->LockAfterStartup();`
4. `m_Notifier = CreateRef<Notifier>(m_EventBus);`
5. Reszta istniejących inicjalizacji

`initializeEventInfrastructure()` zostaje z wyłącznie: `m_EventBus`, `m_EventQueue`, `m_EventController`.

---

## SESJA 19 – NotificationCenter: RemoveListener i zmiana nazwy Listener

**Przeczytaj przed rozpoczęciem:**
`src/Core/Notifications/NotificationCenter.hpp`,
`src/Core/Notifications/NotificationCenter.cpp`

### 19.1 Zmień API

W `src/Core/Notifications/NotificationCenter.hpp`:

Zmień:
```cpp
using Listener = std::function<void(const Notification &)>;
void RegisterListener(Listener listener);
```
Na:
```cpp
using NotificationHandler = std::function<void(const Notification &)>;
[[nodiscard]] std::size_t RegisterListener(NotificationHandler listener);
void RemoveListener(std::size_t listenerId);
```

Zmień prywatny member:
```cpp
std::vector<Listener> m_Listeners;
```
Na:
```cpp
std::unordered_map<std::size_t, NotificationHandler> m_Listeners;
std::size_t m_NextListenerId = 1;
```

### 19.2 Zaktualizuj implementację

W `src/Core/Notifications/NotificationCenter.cpp`:

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

void NotificationCenter::ClearListeners()
{
    m_Listeners.clear();
}
```

W `onNotification`:
```cpp
void NotificationCenter::onNotification(const Notification &notification)
{
    m_Notifications.push_back(notification);
    for (const auto &[id, handler] : m_Listeners)
        if (handler)
            handler(notification);
}
```

### 19.3 Zaktualizuj callsite w DemoNotifications.cpp

Zmień `m_NotificationCenter->RegisterListener(...)` – teraz zwraca ID. Zapisz ID jeśli planujesz `RemoveListener` w destruktorze (na razie opcjonalne, zapisz z komentarzem):
```cpp
m_NotificationCenterListenerId = m_NotificationCenter->RegisterListener([this](...) { ... });
```

Dodaj `m_NotificationCenterListenerId` jako `std::size_t` member w `DemoNotificationsPanel`.

---

## SESJA 20 – Usunięcie podwójnego ProcessQueue

**Przeczytaj przed rozpoczęciem:**
`src/App/Application.cpp` – funkcja `persistUiSettings`

### 20.1 Napraw podwójne ProcessQueue

Znajdź w `persistUiSettings()` fragment z dwoma wywołaniami `m_EventBus->ProcessQueue()`. Zamień na:
```cpp
m_EventBus->Queue(AppEvents::Config::SaveUserRequested{m_Config});
while (m_EventBus->GetQueuedEventCount() > 0)
    m_EventBus->ProcessQueue();
```

Jeśli `GetQueuedEventCount()` nie istnieje w `EventBus`, użyj:
```cpp
m_EventBus->Queue(AppEvents::Config::SaveUserRequested{m_Config});
m_EventBus->ProcessQueue(); // SaveUserRequested → queues PersistRequested
m_EventBus->ProcessQueue(); // PersistRequested → processed
```
i usuń ewentualne trzecie wywołanie jeśli było.

---

## SESJA 21 – Memory.hpp: usunięcie dspch.hpp

**Przeczytaj przed rozpoczęciem:**
`src/Core/Utils/Memory.hpp`

### 21.1 Zmień include

W `src/Core/Utils/Memory.hpp`, usuń:
```cpp
#include "Core/dspch.hpp"
```
Dodaj:
```cpp
#include <memory>
```

### 21.2 Napraw kompilację

Skompiluj projekt. Dla każdego pliku który przestał się kompilować z powodu brakującego include:
- Dodaj jawny `#include` dla brakującego nagłówka w tym konkretnym pliku
- Nie przywracaj `dspch.hpp` do `Memory.hpp`

---

## SESJA 22 – Anonimowe namespaces → static

**Przeczytaj przed rozpoczęciem:**
Każdy z poniższych plików osobno.

### 22.1 We wszystkich poniższych plikach wykonaj tę samą transformację

Dla każdego `namespace { ... }` w pliku `.cpp`:
1. Usuń `namespace {` i zamykający `}`
2. Przed każdą funkcją która była w tym namespace dodaj `static`
3. Jeśli funkcja miała nazwę `PascalCase` – zmień na `camelCase` (zgodnie z konwencją prywatnych funkcji)

Pliki do zmiany:
- `src/App/Controllers/ApplicationConfigController.cpp`
- `src/Core/CoreLayer.cpp`
- `src/IO/IOLayer.cpp`
- `src/Core/Logging/LogRegistrySink.cpp`
- `src/App/Application.cpp`
- `src/Core/Commands/CommandRegistry.cpp`
- `src/Core/Commands/CommandPalette.cpp`
- `src/Core/Undo/UndoStack.cpp`
- Nowe pliki z Sesji 15 (KeyChord/ContextExpr/ContextManager/KeymapResolver/KeyInputProcessor `.cpp`)

Przykład transformacji dla `ApplicationConfigController.cpp`:
```cpp
// Przed:
namespace
{
    template <typename EventType>
    SubscriptionHandle subscribeConfigController(...)
    { ... }
}

// Po:
template <typename EventType>
static SubscriptionHandle subscribeConfigController(...)
{ ... }
```

---

## SESJA 23 – Przeniesienie plików Logging

**Przeczytaj przed rozpoczęciem:**
`src/Core/Utils/Logger.hpp`, `src/Core/Utils/Logger.cpp`,
`src/Core/Utils/SpdlogEventSink.hpp`, `src/Core/Utils/SpdlogEventSink.cpp`,
`src/Events/LogEvents.hpp`, `src/Events/NotificationEvents.hpp`

### 23.1 Przenieś pliki

Przesuń fizycznie (git mv):
- `src/Core/Utils/Logger.hpp` → `src/Core/Logging/Logger.hpp`
- `src/Core/Utils/Logger.cpp` → `src/Core/Logging/Logger.cpp`
- `src/Core/Utils/SpdlogEventSink.hpp` → `src/Core/Logging/SpdlogEventSink.hpp`
- `src/Core/Utils/SpdlogEventSink.cpp` → `src/Core/Logging/SpdlogEventSink.cpp`
- `src/Events/LogEvents.hpp` → `src/Core/Logging/LogEvents.hpp`
- `src/Events/NotificationEvents.hpp` → `src/Core/Notifications/NotificationEvents.hpp`

### 23.2 Zaktualizuj wszystkie include paths

Wykonaj w całym repozytorium:
- `"Core/Utils/Logger.hpp"` → `"Core/Logging/Logger.hpp"`
- `"Core/Utils/SpdlogEventSink.hpp"` → `"Core/Logging/SpdlogEventSink.hpp"`
- `"Events/LogEvents.hpp"` → `"Core/Logging/LogEvents.hpp"`
- `"Events/NotificationEvents.hpp"` → `"Core/Notifications/NotificationEvents.hpp"`

### 23.3 Zaktualizuj `dspch.hpp`

Jeśli `dspch.hpp` zawiera `#include "Core/Utils/Logger.hpp"`, zaktualizuj na nową ścieżkę.

### 23.4 Usuń `src/Events/` jeśli pusty

Jeśli katalog `src/Events/` jest po tej zmianie pusty, usuń go i zaktualizuj `premake5.lua`.

### 23.5 Zaktualizuj `premake5.lua`

Dodaj nowe katalogi do `includedirs` lub `files` jeśli są jawnie wymienione.

---

## SESJA 24 – TestJobs → tests/

**Przeczytaj przed rozpoczęciem:**
`src/Core/JobSystem/TestJobs/TestJobs.hpp`,
`src/Core/JobSystem/TestJobs/TestJobs.cpp`,
`premake5.lua`

### 24.1 Przenieś pliki

```
src/Core/JobSystem/TestJobs/ → tests/Core/JobSystem/TestJobs/
```

### 24.2 Zaktualizuj `premake5.lua`

Usuń `src/Core/JobSystem/TestJobs/` ze źródeł produkcyjnych.
Dodaj `tests/Core/JobSystem/TestJobs/` do projektu testowego.

---

## SESJA 25 – Rozbicie Application.cpp

**Przeczytaj przed rozpoczęciem:**
`src/App/Application.hpp`, `src/App/Application.cpp`

### 25.1 Utwórz `src/App/ApplicationWindow.cpp`

Przenieś z `Application.cpp` implementacje następujących metod. Nie zmieniaj deklaracji w `.hpp`.

Metody do przeniesienia:
- `Application::initializeGlfw()`
- `Application::createMainWindow()`
- `Application::initializeGraphics()`
- `Application::shutdownWindow()`
- `Application::shutdownGlfw()`
- `Application::configureInputBackend()`

Pierwsze linie `ApplicationWindow.cpp`:
```cpp
#include "Core/dspch.hpp"
#include "App/Application.hpp"
```

Dodaj wszystkie includes które były w `Application.cpp` wyłącznie dla tych metod (GLFW, GLAD, OpenGL itp.). Usuń te includes z `Application.cpp`.

### 25.2 Utwórz `src/App/ApplicationBootstrap.cpp`

Przenieś implementacje:
- `Application::createFromSpecification()`
- `Application::beginCreateFromSpecification()`
- `Application::bootstrapApplicationConfiguration()`
- `Application::initializeEventInfrastructure()`
- `Application::initializeWindowingAndGraphics()`
- `Application::initializeApplicationLayers()`
- `Application::initializeCoreRuntimeServices()`
- `Application::finishCreateFromSpecification()`
- `Application::shutdownInternal()`
- `Application::setupDefaultLayers()`
- `Application::bootstrapConfiguration()`
- `Application::applySpecificationFromDefaultConfig()`
- `Application::logStartupSpecification()`
- `Application::initializeLogger()`
- `Application::shutdownLogger()`
- `Application::initializeEventDispatchingSystem()`
- `Application::initializeCoreLayerSystems()`
- `Application::initializeAssetManager()`
- `Application::persistUiSettings()`

Pierwsze linie `ApplicationBootstrap.cpp`:
```cpp
#include "Core/dspch.hpp"
#include "App/Application.hpp"
```

### 25.3 Zostaw w `Application.cpp`

Po przeniesieniu, `Application.cpp` zawiera wyłącznie:
- `Application::Create()`
- `Application::Application()` (konstruktor)
- `Application::~Application()`
- `Application::Run()`
- `Application::Shutdown()`
- `Application::mainLoop()`
- `Application::runMainLoopFrame()`
- `Application::beginFrame()`
- `Application::drawMainPanel()`
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
- `Application::ProcessQueuedEvents()`

---

## SESJA 26 – m_BlockingError → ImGuiLayer

**Przeczytaj przed rozpoczęciem:**
`src/App/Application.hpp`, `src/App/Application.cpp`,
`src/Presentation/ImGuiLayer.hpp`, `src/Presentation/ImGuiLayer.cpp`

### 26.1 Dodaj state do `ImGuiLayer`

W `src/Presentation/ImGuiLayer.hpp`, dodaj:
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

### 26.2 Dodaj rendering w `OnImGuiRender`

W `src/Presentation/ImGuiLayer.cpp`, na początku `OnImGuiRender()` (przed resztą UI), dodaj:
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

### 26.3 Zaktualizuj `Application`

W `src/App/Application.hpp`:
- Usuń `std::optional<StructuredError> m_BlockingError;`

W `src/App/Application.cpp`, zmień `ShowBlockingError`:
```cpp
void Application::ShowBlockingError(const StructuredError &error)
{
    DS_LOG_CRITICAL("Blocking error: {} [{}]", error.userMessage, error.code);
    auto *imguiLayer = m_LayerStack.Find<ImGuiLayer>();
    if (imguiLayer != nullptr)
        imguiLayer->SetBlockingError(error);
}
```

Jeśli `LayerStack::Find<T>()` nie istnieje – dodaj templated metodę do `LayerStack` która iteruje i dynamic_cast'uje.

Usuń wszystkie miejsca w `Application.cpp` gdzie `m_BlockingError` było sprawdzane i renderowane.

---

## SESJA 27 – EventBus: usunięcie const_cast

**Przeczytaj przed rozpoczęciem:**
`src/Core/EventSystem/BusEventSystem/Event.hpp`,
`src/Core/EventSystem/BusEventSystem/EventBus.hpp`,
`src/Core/EventSystem/BusEventSystem/EventBus.cpp`

### 27.1 Dodaj `mutable` do `BusEvent`

W `src/Core/EventSystem/BusEventSystem/Event.hpp`, w strukturze `BusEvent` (lub bazowej), znajdź pola `stopPropagation` i `handled`. Dodaj `mutable`:
```cpp
mutable bool stopPropagation = false;
mutable bool handled = false;
```

### 27.2 Zmień sygnaturę `Publish`

W `EventBus.hpp` i `EventBus.cpp`, zmień:
```cpp
template <BusEventType TEvent>
void Publish(const TEvent &event);
```
Na:
```cpp
template <BusEventType TEvent>
void Publish(TEvent &event);
```

Usuń `const_cast<TEvent &>(event)` z ciała `Publish` – przekazuj `event` bezpośrednio.

### 27.3 Zaktualizuj callsites

Sprawdź `grep -r "->Publish\|\.Publish" src/` i dla każdego wywołania które przekazuje `const TEvent`:
```cpp
// Jeśli było:
const SomeEvent event{...};
m_EventBus->Publish(event);

// Zmień na:
SomeEvent event{...};
m_EventBus->Publish(event);
```

---

## SESJA 28 – LoggingPanel: tablice bool → mapy z enum

**Przeczytaj przed rozpoczęciem:**
`src/Presentation/Panels/LoggingPanel.hpp`,
`src/Presentation/Panels/LoggingPanel.cpp`

### 28.1 Zmień typy pól filtrów

W `LoggingPanel.hpp`, zastąp tablice bool:
```cpp
// Usuń cokolwiek z tablicami bool dla poziomów i kategorii, dodaj:
std::array<bool, static_cast<std::size_t>(LogLevel::Count)> m_ShowLevel;
std::unordered_map<LogCategory, bool> m_ShowCategory;
```

Dodaj niezbędne includes: `<array>`, `<unordered_map>`.

### 28.2 Zaktualizuj inicjalizację

W konstruktorze lub `OnAttach`:
```cpp
m_ShowLevel.fill(true);
for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
    m_ShowCategory[static_cast<LogCategory>(i)] = true;
```

### 28.3 Zaktualizuj rendering

W `LoggingPanel.cpp`, zamień wszystkie `m_ShowLevel[i]` (z magicznym indeksem) na:
```cpp
m_ShowLevel[static_cast<std::size_t>(entry.level)]
```

Zamień `m_ShowCategory[i]` na:
```cpp
m_ShowCategory[entry.category]
```

Zamień imgui checkboxy na iteracje po enum:
```cpp
for (std::size_t i = 0; i < m_ShowLevel.size(); ++i)
{
    const auto level = static_cast<LogLevel>(i);
    ImGui::Checkbox(ToString(level), &m_ShowLevel[i]);
    ImGui::SameLine();
}
```

---

## SESJA 29 – Wyłączenie klonowania paneli-singletonów

**Przeczytaj przed rozpoczęciem:**
`src/Presentation/Panels/LoggingPanel.hpp`,
`src/Presentation/Panels/SettingsPanel.hpp`,
`src/Presentation/Panels/ProgressMonitorWindow.hpp`,
`src/Presentation/Panels/TaskMonitorWindow.hpp`

### 29.1 Dodaj = delete do każdego panelu

W każdym z poniższych plików, po deklaracji konstruktora publicznego, dodaj:
```cpp
ClassName(const ClassName &) = delete;
ClassName &operator=(const ClassName &) = delete;
ClassName(ClassName &&) = delete;
ClassName &operator=(ClassName &&) = delete;
```

Panele: `LoggingPanel`, `SettingsPanel` (lub klasa w `Settings.cpp`), `ProgressMonitorWindow`, `TaskMonitorWindow`.

**Nie dodawaj** `= delete` do: `IPanel`, klas z `Demo/`, klas które reprezentują dane per-dokument lub per-analiza.

Dodaj komentarz nad `= delete` w każdym pliku:
```cpp
// Non-copyable: this panel represents global application state (single session instance).
```