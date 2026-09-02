# Code Review – Defect Studio (`task/04-core`)

> **Autor analizy:** Senior C++ Software Architect / Technical Lead  
> **Repozytorium:** https://github.com/sjcmdev/Defect-Studio.git  
> **Gałąź:** `task/04-core`  
> **Data:** 2026-05-01  
> **Wersja dokumentu:** 3.0 – rozszerzona o komentarze analityczne i sekcję odchudzenia Application

---

## Legenda oznaczeń

| Znacznik              | Opis                                                             |
| --------------------- | ---------------------------------------------------------------- |
| 🔴 **Critical**        | Blokuje architekturę lub poprawność działania                    |
| 🟠 **High**            | Poważny problem do naprawy w najbliższym sprincie                |
| 🟡 **Medium**          | Istotne, ale może poczekać iterację                              |
| 🟢 **Low / Good**      | Drobnostka lub pozytywna obserwacja                              |
| 💬 **Autor**           | Uwaga wniesiona przez autora projektu                            |
| ✅ **Trafne**          | Uwaga autora jest w pełni słuszna i powinna być zrealizowana     |
| ⚠️ **Częściowo**       | Uwaga jest słuszna, ale wymaga doprecyzowania lub ma alternatywy |
| 🔁 **Do przemyślenia** | Uwaga otwiera ważne pytanie, ale odpowiedź nie jest oczywista    |
| ➡️ **Analiza**         | Rozwinięcie merytoryczne uwagi                                   |

---

## 1. Executive Summary

Defect Studio to wczesny, ale dobrze zarysowany projekt aplikacji desktopowej w C++. Kod wykazuje świadomość architektoniczną znacznie powyżej przeciętnego projektu na tym etapie: istnieją ADRy, wytyczne C++, dokumentacja mdbook, struktura warstw, dwa niezależne systemy eventów, job system z obsługą priorytetów i opóźnień, system konfiguracji oparty na eventach, i zaczątki testów jednostkowych. To solidna baza.

**Najpoważniejszy problem jest jeden i wymaga natychmiastowej uwagi:** rdzeń systemu (`Core`) zależy od warstwy aplikacyjnej (`App`). To odwrócona zależność, która łamie założoną architekturę i zablokuje testowalność Core w izolacji.

Przy pogłębionej analizie systemów Core (Capabilities, Commands, HotKey, Notifications, Undo-Redo) widoczna jest druga klasa problemów: **fragmentacja odpowiedzialności** – klasy z za dużą liczbą zadań (`CommandRegistry`), brak spójności między pokrewnymi komponentami (trzy osobne magazyny historii notyfikacji) i niespójne wzorce tam gdzie projekt ma już ustalone konwencje (raw pointery w CommandRegistry mimo obecności `Ref<T>`).

---

## 2. Aktualna architektura repozytorium

### Wyłaniający się styl architektoniczny

Projekt jest **engine-style layered monolith** z:
- jasnym composition rootem (`Application`),
- stosem warstw (`LayerStack`) z `OnAttach/OnDetach/OnUpdate/OnImGuiRender`,
- dwoma równoległymi systemami eventów: `EventBus` (pub/sub, typowany) i `EventQueue` (kolejka wariantów platform events),
- kontrolerami domenowymi jako subskrybentami EventBus,
- IOLayer jako warstwą persystencji reagującą na zdarzenia,
- modelem kompozycji przez wstrzykiwanie zależności do warstw przez `BindRuntimeServices`.

Styl jest spójny z ADR-001 (modular domain monolith). Widać, że autor faktycznie przeczytał własne ADRy i stara się je realizować.

### Podział warstw (faktyczny)

```
Application (composition root, ~1063 LoC)
├── Core/        – platformowe utilities, event system, job system, layer base
├── App/         – konfiguracja, cykl życia aplikacji, kontrolery eventów
├── Presentation/– ImGui, EditorLayer, panele UI
├── IO/          – persystencja plików, reaktywna przez EventBus
├── Storage/     – (stub)
├── Domain/      – (stub)
├── ScientificRuntime/ – (stub)
├── Debug/       – DebugLayer (dev-only)
└── Demo/        – playground/demo (dev-only)
```

---

## 3. Ocena zgodności z założoną architekturą

### Co jest zgodne

- `Application` jako composition root jest dobrze realizowane.
- `IOLayer` jako reaktywna warstwa IO jest prawidłowo oddzielona od logiki.
- `CoreLayer` agreguje JobSystem i ProgressTracker – sensowny podział.
- `ApplicationEventController` → `ApplicationConfigController` – wzorzec agregacji kontrolerów jest dobry.
- Podział `EventBus` (logika app) vs `EventQueue` (platform events) jest przemyślany i spójny z dokumentacją.

### Co łamie architekturę

🔴 **Core zależy od App:**

| Plik w Core                                       | Importuje z App                                                      |
| ------------------------------------------------- | -------------------------------------------------------------------- |
| `Core/JobSystem/JobSystem.cpp`                    | `App/Events/JobEvents.hpp`                                           |
| `Core/CoreLayer.cpp`                              | `App/ApplicationState.hpp`, `App/Events/ApplicationConfigEvents.hpp` |
| `Core/ProgressTrackingSystem/ProgressTracker.cpp` | `App/Events/JobEvents.hpp`                                           |

➡️ To nie jest błąd implementacyjny, lecz decyzja o tym, _gdzie_ zdefiniowano eventy. `JobEvents.hpp` mógłby żyć w `Core/JobSystem/` – `JobSystem` i tak go definiuje, a `App` tylko go konsumuje. Naprawa wymaga jednej decyzji architektonicznej (lokalizacja event typów) i 3-4 plików zmienionych includes.

---

🟠 **`Application::Get()` w Presentation/Demo:**

`Presentation/Panels/Settings.cpp` (3 razy) i `Demo/DemoLayer.cpp` (6+ razy) wywołują `Application::Get()`.

---

💬 **Autor [pkt 1]:** `initializeEventInfrastructure()` inicjalizuje `CapabilityService`, `CapabilityRegistry` i `Notifier` – to osobne systemy, które powinny być inicjalizowane w `initializeCoreRuntimeServices()`.

✅ **Trafne.**  
➡️ Rozróżnienie `initializeEventInfrastructure()` vs `initializeCoreRuntimeServices()` powinno być semantyczne: infrastruktura eventów = `EventBus`, `EventQueue`, `EventController`. Runtime services = `CapabilityRegistry/Service`, `Notifier`, `AssetManager`, `JobSystem`. `Notifier` wymaga `EventBus`, więc musi być inicjalizowany po nim – ale to nadal `initializeCoreRuntimeServices()`, bo zależność na `EventBus` wyraża się przez wstrzykiwanie referencji, nie przez kolejność funkcji inicjalizacji.

---

💬 **Autor [pkt 13]:** `Application` trzyma `m_BlockingError` – to zdecydowanie nie jest miejsce na obsługę błędów blokujących.

✅ **Trafne.**  
➡️ `m_BlockingError` to stan UI, nie stan aplikacji. Wzorzec powinien wyglądać tak: `Application` emituje event `AppEvents::FatalError{error}`, a `ImGuiLayer` subskrybuje i ustawia własne `m_PendingBlockingError`. Renderowanie błędu jest wtedy czystą odpowiedzialnością warstwy renderującej, a `Application` jest wolna od jakiegokolwiek stanu UI.

---

💬 **Autor [pkt 18]:** `ShowBlockingError` nie pokazuje blocking window. Renderowanie powinno być odpowiedzialnością `ImGuiLayer`.

✅ **Trafne i ściśle powiązane z pkt 13.**  
➡️ To dwa aspekty tego samego problemu. `Application::ShowBlockingError` to placeholder – faktycznej implementacji okna blokującego brakuje. Zanim to zostanie zaimplementowane, warto zrobić to poprawnie: event z `Application` → `ImGuiLayer` renderuje popup w `OnImGuiRender()` gdy `m_PendingBlockingError` jest ustawione.

---

💬 **Autor [pkt 11]:** `CapabilityRegistry` powinno być wewnętrzną częścią `CapabilityService`.

✅ **Trafne.**  
➡️ Obecny stan: `Application` trzyma zarówno `Ref<CapabilityRegistry>` jak i `Ref<CapabilityService>`. Sens `CapabilityService` jako fasady jest w tym, że konsument nie musi wiedzieć nic o Registry. Dlatego `CapabilityRegistry` powinno być prywatnym `Unique<CapabilityRegistry>` wewnątrz `CapabilityService`, a `CapabilityService` powinna udostępniać zarówno `IsAvailable/Require` (consumer API) jak i `RegisterCapability/LockAfterStartup` (startup API). `Application` trzyma tylko `Ref<CapabilityService>`.

---

## 4. Review struktury repozytorium

### Problemy

🟡 **Logging rozrzucony po dwóch katalogach:**

💬 **Autor [pkt 2]:** Moduł Loggera jest rozrzucony po folderach, powinien być w jednym miejscu.

✅ **Trafne.**  
➡️ Podział jest historyczny – `Logger.hpp` prawdopodobnie powstał wcześniej w `Utils/`, a `Logging/` dodano później dla bardziej zaawansowanych komponentów. Naprawienie wymaga przeniesienia `Logger.hpp/.cpp` i `SpdlogEventSink.hpp/.cpp` z `Core/Utils/` do `Core/Logging/`. Wszystkie include paths w projekcie trzeba zaktualizować (find/replace w IDE). Drobna praca, duże korzyści nawigacyjne.

---

💬 **Autor [pkt 5]:** W `DemoLayer.hpp/.cpp` jest wiele klas służących do pokazania przykładów – powinny być przeniesione do osobnych plików.

✅ **Trafne.**  
➡️ `DemoLayer` to warstwa prezentacyjna, a nie kontener dla wszystkich klas demo. Każda klasa demo (DemoCommands, DemoNotifications, DemoJobSystem, DemoUndoRedo) powinna być w osobnym pliku, żeby móc łatwo włączać/wyłączać poszczególne przykłady. Dodatkowo ułatwia navigation i wyszukiwanie.

---

💬 **Autor [pkt 33]:** W pliku `KeyBinding.hpp` jest bardzo dużo klas i struktur, które zasługują na własne pliki.

✅ **Trafne.**  
➡️ `KeyBinding.hpp` zawiera aktualnie: `KeyModifiers`, `KeyChord`, `ContextManager`, `ContextExpr`, `KeymapLayer`, `KeyBindingConflictType`, `KeyBinding`, `KeyBindingConflict`, `KeymapResolver`, `KeyInputResult`, `KeyInputProcessor` i `ToString`. To 11 pojęć w jednym pliku. Każde z nich ma klarowną semantykę – rozdzielenie jest nie tylko możliwe, ale wskazane. Warto trzymać `KeyChord` i `KeyModifiers` razem (to jeden koncept), ale reszta powinna mieć osobne pliki.

---

💬 **Autor [pkt 34]:** `ContextManager` zdecydowanie powinien być w osobnym pliku.

✅ **Trafne i spójne z pkt 33.**  
➡️ `ContextManager` jest niezależnym komponentem z własnym stanem i API. Można go używać bez `KeymapResolver` (np. do `CanExecute` w UI). Osobny plik eliminuje niepotrzebne includowanie całego `KeyBinding.hpp` gdy potrzebny jest tylko kontekst.

---

### Proponowana docelowa struktura

```
src/
├── Core/
│   ├── Assets/
│   ├── Capabilities/
│   ├── Commands/
│   │   ├── Command.hpp/.cpp
│   │   ├── CommandContext.hpp/.cpp    ← implementacja template poniżej klasy
│   │   ├── CommandRegistry.hpp/.cpp
│   │   ├── CommandService.hpp/.cpp    ← NOWE: wydzielone z Registry
│   │   └── CommandPalette.hpp/.cpp
│   ├── Diagnostics/
│   ├── EventSystem/
│   │   ├── BusEventSystem/
│   │   │   └── Events/               ← NOWE: JobEvents.hpp, bazowe eventy Core
│   │   └── DispatchingEventSystem/
│   ├── Input/
│   │   ├── KeyChord.hpp               ← KeyChord + KeyModifiers
│   │   ├── ContextManager.hpp/.cpp
│   │   ├── ContextExpr.hpp/.cpp
│   │   ├── KeyBinding.hpp             ← tylko struct KeyBinding + KeyBindingConflict
│   │   ├── KeymapResolver.hpp/.cpp
│   │   └── KeyInputProcessor.hpp/.cpp
│   ├── JobSystem/
│   ├── Logging/                       ← Logger.hpp + Logger.cpp przeniesione z Utils/
│   │   ├── Logger.hpp
│   │   ├── Logger.cpp
│   │   ├── LogRegistry.hpp/.cpp
│   │   ├── LogRegistrySink.hpp/.cpp
│   │   └── SpdlogEventSink.hpp/.cpp
│   ├── Notifications/
│   │   ├── Notification.hpp
│   │   ├── Notifier.hpp/.cpp
│   │   ├── NotificationCenter.hpp/.cpp
│   │   ├── NotificationHistory.hpp/.cpp
│   │   └── NotificationEvents.hpp
│   ├── Platform/
│   ├── ProgressTrackingSystem/
│   ├── Threading/
│   ├── Types/
│   ├── Undo/
│   └── Utils/                         ← Memory, Path, Time, Assert, KeyCodes
├── App/
│   ├── Events/
│   ├── Controllers/
│   ├── Managers/
│   └── Serialization/
├── Presentation/
├── IO/
├── Domain/
├── Storage/
├── ScientificRuntime/
├── Debug/
├── Demo/
│   ├── DemoLayer.hpp/.cpp
│   ├── DemoCommands.hpp/.cpp
│   ├── DemoNotifications.hpp/.cpp
│   └── DemoUndoRedo.hpp/.cpp
└── main.cpp
```

---

## 5. Review modułów i zależności

### EventBus

🟡 **`const_cast` w `EventBus::Publish`:**

```cpp
void EventBus::Publish(const TEvent &event) {
    dispatchByType(GetTypeId(...), const_cast<TEvent &>(event));
}
```

➡️ `BusEvent::stopPropagation` i `handled` powinny być `mutable` – to poprawna semantyka dla flagi modyfikowanej przez dispatch, a nie przez logikę aplikacyjną. Alternatywnie `Publish` przyjmuje `TEvent &` (non-const), co jest uczciwe: dispatch może mutować event. Obecny const_cast jest technicznie UB jeśli event był zadeklarowany jako `const`.

---

### `persistUiSettings` – double ProcessQueue

🟡 Dwa wywołania `m_EventBus->ProcessQueue()` w sekwencji. Lepiej: `while (m_EventBus->GetQueuedEventCount() > 0) m_EventBus->ProcessQueue()`.

---

💬 **Autor [pkt 3]:** Czy można zmienić ustawienia Loggera bez reinicjalizacji? `LoggerController` vs callback.

🔁 **Do przemyślenia.**  
➡️ Pytanie jest słuszne. Odpowiedź: **tak, ale tylko częściowo**. Log level można zmienić przez `spdlog::set_level()` bez reinicjalizacji. Nie można bez reinicjalizacji: zmienić ścieżki pliku log, dodać/usunąć sink (np. włączyć logowanie do pliku które było wyłączone). Oznacza to, że reinicjalizacja jest wymagana tylko przy zmianie _struktury_ loggera, nie samych parametrów. Warto wydzielić te dwa przypadki.

Co do `LoggerController`: jest to dobry wzorzec **jeśli planujesz więcej niż jeden konfigurowaly subsystem**. Jeśli Logger jest jedynym systemem reinicjalizowanym przy `Config::Applied` – callback wstrzyknięty do `ApplicationConfigController` jest prostszy i w pełni wystarczający. `LoggerController` dodaje klasę, plik, subskrypcję i punkt testowania – warto go dodać gdy pojawi się drugi podobny system (np. `TracyController` dla profilowania).

---

## 6. Review systemów Core

### 6.1 Capabilities

🟢 Wzorzec prawidłowy: `CapabilityRegistry` jako write-once store z `LockAfterStartup()`, `CapabilityService` jako fasada.

🟠 **`Require()` rzuca wyjątek zamiast `Result<void>`:**

```cpp
void CapabilityService::Require(const std::string &name) const {
    if (!IsAvailable(name))
        throw CapabilityNotAvailableException(name);
}
```

➡️ Niezgodne z resztą projektu. Zamienić na `[[nodiscard]] Result<void> Require(const std::string &name) const`. `CommandRegistry::ValidateCapabilities` już używa `Result` i wywołuje `IsAvailable` – może teraz wywołać `Require` zamiast własnego sprawdzenia.

---

💬 **Autor [pkt 11]:** `CapabilityRegistry` powinno być wewnętrzną częścią `CapabilityService`.

✅ **Trafne.** Odpowiedź w sekcji 3.

---

💬 **Autor [pkt 12]:** Brak przykładu jak się rejestruje capability.

✅ **Trafne.**  
➡️ Brak przykładu rejestracji capabilities to luka w `DemoLayer`. Bez niej programista implementujący nowy moduł nie wie: gdzie rejestrować (w `OnAttach`?), co rejestrować (jakie kategorie: `BuildTime`, `RuntimeDetected`, `Policy`?), czy rejestracja po `LockAfterStartup()` jest wyciszana bez błędu (tak – log warn i return, ale to łatwo przeoczyć). Demo powinno zawierać przykład `RegisterCapability` + `LockAfterStartup()` + `Require()` w trybie sukcesu i porażki.

---

### 6.2 Commands

🟢 Jeden z lepiej zaprojektowanych fragmentów projektu.

🟠 **Raw pointery na opcjonalne zależności:**

```cpp
CapabilityService *m_CapabilityService = nullptr;
UndoStack *m_UndoStack = nullptr;
```

➡️ Projekt ma `Ref<T>` i `WeakRef<T>`. `WeakRef<T>` jest odpowiedni gdy `CommandRegistry` nie jest właścicielem (co jest właśnie tym przypadkiem – `Application` jest właścicielem obu). Użycie `WeakRef` komunikuje semantykę natychmiast: "nie owning, może być null".

---

💬 **Autor [pkt 20]:** Raw-pointer/raw smart_ptr w `CommandRegistry`, `CommandPalette`, `UndoScope`, `CommandContext`.

✅ **Trafne.**  
➡️ Wszystkie cztery miejsca mają różne uzasadnienia:
- `CommandRegistry`: `CapabilityService*`, `UndoStack*` – zmienić na `WeakRef<T>`.
- `CommandPalette`: `const KeymapResolver*`, `const ContextManager*` – obserwatory (nie owning), można zmienić na `std::optional<std::reference_wrapper<const T>>` lub po prostu zachować raw pointer z dokumentacją "non-owning, nullable".
- `UndoScope`: `UndoStack *m_Stack = nullptr` – RAII wrapper, raw pointer z nullptr semantics jest tu akceptowalny i idiomatyczny (tak działa `std::lock_guard`). Nie zmieniać.
- `CommandContext`: brak raw pointerów – `std::any` jest idiomatyczny dla property bag. Nie zmieniać.

---

💬 **Autor [pkt 23]:** Czemu `command-category` jest `string` a nie enum?

✅ **Trafne – ale z ważnym zastrzeżeniem.**  
➡️ Kategoryzacja przez string ma jedną zaletę: można dodać nową kategorię domenową bez zmiany enum w `Core/`. Gdy `Domain` i `ScientificRuntime` będą miały własne komendy z własnymi kategoriami, `enum class CommandCategory` w `Core/Commands/` musiałby być rozszerzany przez warstwy wyższe – co jest odwróconą zależnością. Alternatywa: zachować stringa jako ID, ale zdefiniować `namespace CommandCategories { constexpr std::string_view Project = "project"; ... }` per-moduł zamiast magic strings. Najlepsza opcja na obecnym etapie: `constexpr string_view` constants w `Core/Commands/CommandCategories.hpp` + dokumentacja wzorca dla nowych modułów.

---

💬 **Autor [pkt 24]:** Sposób rejestracji komend w DemoLayer jest problematyczny.

✅ **Trafne – to jest Demo, ale Demo jest też dokumentacją.**  
➡️ DemoLayer jest de facto przykładem jak pisać komendy. Jeśli Demo robi to źle, nowy developer skopiuje zły wzorzec. Poprawny wzorzec rejestracji:
```cpp
// Zamiast lambdy jako factory: osobna klasa dziedzicząca ICommand
class DemoSayHelloCommand final : public ICommand {
public:
    Result<void> Execute(CommandContext &) override { /* ... */ return {}; }
    std::string Description() const override { return "Say Hello"; }
};

// Rejestracja: factory jako Unique<ICommand>
m_CommandRegistry->Register(
    CommandMeta{CommandID{"demo.say_hello"}, "Say Hello", "Demo"},
    [](CommandContext &) -> Unique<ICommand> {
        return CreateUnique<DemoSayHelloCommand>();
    });
```
`CommandFlags::None` powinno być domyślną wartością pola w `CommandMeta` (jest i tak – ale może warto dodać konstruktor pomocniczy).

---

💬 **Autor [pkt 26]:** Funkcja `Notify` w `CommandRegistry` jest myląca.

✅ **Trafne.**  
➡️ `CommandRegistry::Notify(event)` wywołuje observerów execution. W tym samym projekcie jest `Notifier` który notyfikuje użytkownika. Nazwy kolidują semantycznie. Zmienić na `notifyObservers` (małe litery zgodnie z konwencją prywatnych funkcji) lub `dispatchExecutionEvent`.

---

💬 **Autor [pkt 28]:** Dlaczego `command->Execute()` zwraca `Result<void>` a nie faktyczny wynik?

🔁 **Do przemyślenia – otwarte pytanie architektoniczne.**  
➡️ `Result<void>` vs `Result<SomeOutput>` to fundamentalna decyzja. Argumenty za `Result<void>`:
1. Komendy w Command Pattern klasycznie nie zwracają wartości – mutują stan.
2. `ICommand` jest interfejsem polimorficznym – nie da się wyrazić różnych typów wynikowych bez `std::any` lub dziedziczenia, co komplikuje UndoStack.
3. Jeśli komenda musi "zwrócić" coś (np. ID nowo stworzonego elementu), powinna to zapisać przez event lub do `CommandContext` jako output.

Argument za bogatym wynikiem: łatwiejsze testowanie (assert na wartość zamiast na side effect). Rekomendacja: zachować `Result<void>`, a output komendy przekazywać przez `CommandContext` jako output key z dokumentacją.

---

💬 **Autor [pkt 29]:** `CommandOutcome` powinien mieć lepszy konstruktor.

✅ **Trafne – kosmetycznie, ale poprawia czytelność.**  
➡️ Aktualny kod w `Execute()`:
```cpp
CommandOutcome outcome;
outcome.id = id;
outcome.description = command->Description();
outcome.undoable = command->IsUndoable();
outcome.pushedToUndoStack = ...;
```
Lepiej: designated initializers lub factory method:
```cpp
static CommandOutcome FromCommand(const CommandID &id, const ICommand &cmd, bool pushed) {
    return {id, cmd.Description(), cmd.IsUndoable(), pushed};
}
```

---

💬 **Autor [pkt 31] + [pkt 25] + [pkt 30]:** `CommandRegistry` ma za dużo odpowiedzialności; brak klasy spinającej commands i undo-redo; wypychanie do UndoStack powinno być w `CommandService`.

✅ **Trafne i wzajemnie powiązane.**  
➡️ To jedno architektoniczne spostrzeżenie wyrażone na trzy sposoby. Proponowany podział:

```
CommandRegistry  →  storage + lookup (RegisterCommand, HasCommand, GetMeta, ListCommands)
CommandService   →  execution + undo + observers (Execute, CanExecute, AddObserver, m_UndoStack)
CommandPalette   →  search + recent (bez zmian, operuje na CommandRegistry + KeymapResolver)
```

`CommandService` wstrzykuje `CommandRegistry` (przez `Ref<>`) i `UndoStack` (przez `WeakRef<>`). `KeyInputProcessor` i `CommandPalette` korzystają z `CommandService::Execute`, nie z `CommandRegistry::Execute`. Zmiana jest lokalna, nie wymaga przepisywania żadnej logiki – tylko przeniesienie.

---

💬 **Autor [pkt 17]:** Czy `CommandFactory` i `CommandExecutionObserver` powinny być `std::function`?

🔁 **Częściowo trafne, odpowiedź jest różna dla każdego.**  
➡️ `CommandExecutionObserver = std::function<void(const CommandExecutionEvent &)>` – pozostawić jako `std::function`. Jest to wzorzec event handlera, nie obiekt z lifecycle. Lambda jest tu naturalnym wyborem i nie utrudnia testowania (łatwo mockować przez lambda z capture).

`CommandFactory = std::function<Unique<ICommand>(CommandContext &)>` – tu jest subtelny problem: factory jest przechowywana per-komenda w mapie, co oznacza, że _każde_ wywołanie `Execute` alokuje nową lambda closure. To akceptowalne. Interfejs `ICommandFactory` dałby możliwość mockowania i kompozycji, ale dodałby boilerplate. Na obecnym etapie `std::function` jest OK. Gdy pojawią się komendy z zależnościami (np. factory potrzebuje wstrzyknąć `ProjectWorkspace`), wróci temat interfejsu.

---

💬 **Autor [pkt 32]:** Brak logowania błędów rejestracji przez system logowania.

✅ **Trafne i ważne poza Demo.**  
➡️ `CommandRegistry::Register` zwraca `Result<void>`. Wywołanie bez sprawdzenia go jest silent failure. Wzorzec który powinien obowiązywać wszędzie poza Demo:
```cpp
if (auto result = registry.Register(meta, factory); !result)
    DS_LOG_ERROR("Command registration failed [{}]: {}", meta.id.value, result.Error().technicalDetails);
```
Warto rozważyć dodanie tego logowania bezpośrednio do `CommandRegistry::Register` po stronie implementacji, żeby nie wymagać tego od każdego callsite.

---

### 6.3 Hot-Key (KeyBinding)

🟢 Kompletna implementacja jak na etap projektu.

---

💬 **Autor [pkt 21]:** Brak globalnych skrótów: `Ctrl+Z/Y`, `Ctrl+Shift+P`, `Ctrl+Shift+W`, `Ctrl+S`.

✅ **Trafne – to oczywiste minimum dla każdej aplikacji desktopowej.**  
➡️ Skróty powinny być rejestrowane w `CoreLayer::OnAttach()` lub dedykowanej funkcji `registerDefaultBindings()`. Każdy skrót wymaga:
1. Zarejestrowanej komendy w `CommandRegistry` (np. `edit.undo`, `edit.redo`, `app.command_palette`, `app.quit`, `project.save`).
2. Zarejestrowanego bindingu w `KeymapResolver`.

Undo/Redo są szczególnym przypadkiem – komendy `edit.undo/redo` muszą wywoływać `UndoStack::Undo/Redo`, więc powinny być rejestrowane przez `CommandService` który trzyma referencję do UndoStack.

---

💬 **Autor [pkt 16]:** Brak w SettingsPanel wyświetlania skrótów i możliwości ich modyfikacji.

⚠️ **Trafne, ale to feature na późniejszy etap.**  
➡️ Wyświetlanie skrótów w Settings wymaga jedynie `KeymapResolver::ListBindings()` + iteracji po nich w ImGui – to godzina pracy. Modyfikacja skrótów przez UI to inny kaliber: wymaga `KeymapResolver::Clear()` + `RegisterBinding()`, persystencji do pliku (KeyBindings YAML), obsługi konfliktów w UI, i prawdopodobnie osobnego panelu (nie Settings, a KeyBindings). Wyświetlanie: teraz. Edycja: gdy `IOLayer` obsłuży persystencję keybindings.

---

### 6.4 Notifications

🟠 **Trzy osobne magazyny historii:**

```
Notifier::m_History        – vector<Notification>
NotificationCenter::m_Notifications – vector<Notification>
NotificationHistory::m_Entries – vector<Notification>
```

➡️ Właściwy model: `NotificationHistory` jest jedynym magazynem, do którego zapisuje `NotificationCenter` (jako observer EventBus). `Notifier::m_History` powinno zniknąć – `Notifier` produkuje eventy, nie przechowuje historii. `NotificationCenter` nie duplikuje do `m_Notifications`, tylko deleguje do `NotificationHistory`. Historia jest queryowalna przez `NotificationHistory::FilterBySeverity` itp. Jeden write path, jedno miejsce zapytań.

---

🔴 **`Notifier` używa `Publish` zamiast `Queue`:**

➡️ Natychmiast po tym jak `JobSystem` zacznie generować notyfikacje (a to nastąpi przy pierwszym długim obliczeniu), kod trafi na data race. Zmiana na `Queue` jest jedną linią i jest bezpieczna – `Queue` jest thread-safe w obecnej implementacji `EventBus`. **To zmiana priorytet #1** przed rozbudową Domain.

---

💬 **Autor [pkt 8]:** `NotificationCenter` definiuje typ `Listener`.

✅ **Trafne.**  
➡️ `Listener` to zbyt ogólna nazwa dla typedefa w klasie. W kodzie który korzysta z `NotificationCenter::Listener` i `EventBus`-owych subskrypcji naraz, czytelnik gubi się który "listener" jest który. Lepsza nazwa: `NotificationHandler` lub `NotificationCallback`.

---

💬 **Autor [pkt 6]:** W DemoLayer `Notifier` pobierany z `Application` zamiast z dummy-version wewnątrz DemoLayer.

✅ **Trafne.**  
➡️ To bezpośrednia konsekwencja używania `Application::Get()`. DemoLayer dla notyfikacji powinno albo: (a) używać wstrzykniętego `Ref<Notifier>` przez `BindRuntimeServices`, albo (b) tworzyć lokalny `Notifier` bez EventBus jeśli demo ma działać standalone. Opcja (a) jest lepsza – pokazuje właściwy wzorzec.

---

💬 **Autor [pkt 7]:** `NotificationCenter` nie jest używany w przykładzie.

✅ **Trafne.**  
➡️ `NotificationCenter` jest kluczowym komponentem systemu notyfikacji (subskrybuje `NotificationEvent` z EventBus i dystrybuje do listenerów), a Demo go pomija. Bez przykładu developer nie wie jak połączyć `Notifier` → EventBus → `NotificationCenter` → `NotificationHistory` → UI. Demo powinno pokazać dokładnie ten łańcuch.

---

💬 **Autor [pkt 9]:** `ImGui::InsertNotification` wywołane wewnątrz lambdy `emitNotification`.

✅ **Trafne – to poważne naruszenie warstwy renderującej.**  
➡️ `ImGui::InsertNotification` to call ImGui API poza `OnImGuiRender()`. W ImGui wszystkie call muszą być między `NewFrame()` a `Render()`. Lambda `emitNotification` jest wywoływana w trakcie przetwarzania eventów, nie w trakcie renderowania. W praktyce może to działać przez implementację `ImGui-Toast` która kolejkuje toast do następnej ramki, ale jest to przypadkowe zachowanie, a nie gwarantowane. Wzorzec powinien być: `ImGuiLayer` subskrybuje `NotificationEvent` → zapisuje do `m_PendingToasts` → w `OnImGuiRender()` wywołuje `ImGui::InsertNotification` dla każdego pending toast.

---

💬 **Autor [pkt 10]:** `ImGuiLayer` powinno słuchać na `NotificationEvent`.

✅ **Trafne i bezpośrednio wynika z pkt 9.**  
➡️ To właściwy wzorzec: `ImGuiLayer::OnAttach()` subskrybuje `NotificationEvent`, zapisuje do lokalnego `deque<Notification> m_PendingToasts`, a `OnImGuiRender()` przetwarza kolejkę. Korzyści: render jest deterministyczny, wątek główny kontroluje kiedy toast się pojawi, toast nigdy nie jest renderowany poza frame boundary.

---

### 6.5 Undo-Redo

🟢 Najlepiej zaimplementowany z pięciu systemów.

🔴 **`ApplyRedoRecord` wywołuje `Execute`, nie `Redo`:**

➡️ Rozwiązanie jest jedno-linijkowe w deklaracji interfejsu, ale wymaga przemyślenia semantyki:
```cpp
// ICommand.hpp
virtual Result<void> Redo(CommandContext &context) {
    return Execute(context);  // domyślna implementacja: OK dla idempotentnych komend
}
```
Każda komenda domenowa, która ma stan (`m_previousValue`, `m_selection`, itp.) musi overridować `Redo()`. Zmiana nie łamie żadnych istniejących klas – domyślna implementacja deleguje do `Execute`, więc wszystkie obecne komendy działają jak dotychczas. Nowe komendy domenowe dodają `Redo` gdy są nieidempotentne.

---

🟠 **Partial rollback przy błędzie w grupie:**

➡️ Klasyczne podejście: przy błędzie Undo w grupie, kontynuować Undo pozostałych komend (best-effort), zebrać wszystkie błędy w `std::vector<StructuredError>` i zwrócić je razem. Stan domenowy będzie przynajmniej bliżej spójności niż przy porzuceniu po pierwszym błędzie. Drugie podejście: "atomic undo groups" które wymagają compensating commands – to overengineering na tym etapie. **Minimum teraz:** dodać komentarz dokumentujący znane ograniczenie.

---

🟠 **`CancelGroup` zeruje `m_GroupDepth` zamiast dekrementować:**

➡️ Prawdopodobnie intencjonalne – `CancelGroup` semantycznie anuluje _całą_ operację, nie tylko jeden poziom zagnieżdżenia. Jednak jest niespójne z `EndGroup`. Jeśli to intencja: dodać komentarz `// Intentionally resets entire nesting depth – cancel is atomic`. Jeśli nie: zmienić na `--m_GroupDepth` i wywołać `CancelGroup` rekurencyjnie przez poziomy.

---

### 6.6 `Result<T>` – osobna obserwacja

🟢 Dobra decyzja – używana spójnie przez Commands, UndoStack i KeymapResolver.

💬 **Autor [pkt 4]:** Brak spójnego systemu zarządzania błędami i komunikowania przez `Notification` i `Logging`.

✅ **Trafne i kluczowe przed rozbudową Domain.**  
➡️ Aktualny stan jest trójdzielny: `Result<T>` (Commands, UndoStack), `bool + std::string &error` (Config, YAML), wyjątki (`CapabilityService::Require`). Pipeline komunikacji błędu do użytkownika nie istnieje – `Result` jest zwracany ale nie ma miejsca które tłumaczy go na `Notifier::Error(...)`.

Proponowany pełny pipeline:
```
Result<T> zwrócony z operacji
    → jeśli błąd: Notifier::Error(error.userMessage, ...)
    → Notifier emituje NotificationEvent (przez Queue)
    → ImGuiLayer renderuje toast
    → Opcjonalnie: DS_LOG_ERROR(error.technicalDetails)
```

Unifikacja na `Result<T>` w całym kodzie (Config, YAML, IO) powinna być zrobiona przed domeną.

---

## 7. Review stylu C++

### Co jest dobre

- Konsekwentne `[[nodiscard]]` na funkcjach zwracających wyniki operacji.
- `Ref<T>`, `WeakRef<T>`, `Unique<T>` jako aliasy na smart pointery.
- `enum class` wszędzie.
- `std::jthread` ze stop tokenem – nowoczesny C++20.
- Concepts w EventBus.

### Problemy

🟠 **Anonimowe namespaces – naruszenie wytycznych:**

Wytyczne projektu zabraniają anonimowych namespaceów. Pliki naruszające:
`ApplicationConfigController.cpp`, `CoreLayer.cpp`, `IOLayer.cpp`, `LogRegistrySink.cpp`, `Application.cpp`, `CommandRegistry.cpp`, `CommandPalette.cpp`, `KeyBinding.cpp`, `UndoStack.cpp`.

💬 **Autor [pkt 22]:** Jest dużo niepotrzebnych anonymous namespace.

✅ **Trafne.**  
➡️ Wszystkie przypadki to wolne funkcje pomocnicze w plikach `.cpp`. Właściwy zamiennik: `static` przed definicją funkcji. `static` ma tą samą semantykę linkera (internal linkage), jest idiomatyczne w C++ i jest zgodne z wytycznymi. Zamiana jest mechaniczna: usunąć `namespace { }`, dodać `static` przed każdą funkcją.

---

💬 **Autor [pkt 37]:** Funkcje prywatne zawsze powinny być z małej litery.

⚠️ **Częściowo trafne – zależy od przyjętej konwencji.**  
➡️ Projekt używa `PascalCase` dla metod publicznych i prywatnych (`OnAttach`, `initializeLogger`, `bindEvents`). Jednak część prywatnych metod jest już z małej litery (`initializeLogger`, `bindEvents`, `applyConfigRequest`, `onConfigApplyRequested`). Inne są z wielkiej (`LockAfterStartup`, `RegisterCapability`). Brak spójności jest realnym problemem. Konwencja z guidelines: prywatne z małej litery – to dobra reguła, ale wymaga ujednolicenia _wszystkich_ klas, nie tylko nowych.

---

💬 **Autor [pkt 14]:** W `LoggingPanel` tablice bool – zmienić na mapy i enumy.

✅ **Trafne.**  
➡️ `bool m_ShowLevel[6]` to typowy anty-pattern ImGui który rośnie z kodem. `std::array<bool, static_cast<int>(LogLevel::Count)>` jest lepszy (indeksowany enum), a `std::unordered_map<LogCategory, bool>` dla kategorii jest czytelny i nie wymaga magicznych indeksów.

---

💬 **Autor [pkt 15]:** Wyłączyć możliwość klonowania w: Settings, Logging, Thread, ProgressTracking, TaskMonitor.

✅ **Trafne.**  
➡️ Panele UI są singletonami per-session i nie powinny być kopiowalne. Standardowa forma: `= delete` na copy/move constructor i assignment operator. W projekcie istnieje wzorzec (większość klas Core już go stosuje). Panele i widoki UI należy ujednolicić.

---

💬 **Autor [pkt 19]:** Brak ogólnego profilowania. `tracy` jest w projekcie, ale nieużywany.

⚠️ **Trafne, ale nie pilne.**  
➡️ `tracy` jest zintegrowany i gotowy – nie wymaga dodatkowej konfiguracji. Warto dodać makra profilowania (`ZoneScopedN`, `FrameMark`) przynajmniej w: głównej pętli aplikacji (`runMainLoopFrame`), `JobSystem::processJob`, `EventBus::Publish/ProcessQueue`, i renderowaniu ImGui. To godzina pracy i daje natychmiastowy wgląd w bottlenecki. Nie warto profilować wszystkiego teraz – warto zaprofilować MainLoop i JobSystem.

---

## 8. Review rozszerzalności

### Dobrze przygotowane

- **LayerStack** – dodanie nowej warstwy to jedna linia.
- **EventBus** – zero zmian w infra dla nowego event type.
- **CapabilityRegistry/Service** – gotowy punkt rozszerzeń.
- **JobSystem** – pełna implementacja.
- **IOLayer** – wzorzec request/persist jest doskonale rozszerzalny.
- **PanelRegistry** – panele UI rejestrowane przez templated factory.
- **CommandRegistry** – Command Palette, KeyInputProcessor, UndoStack są luźno spięte.

### Miejsca zbyt sztywne

🟡 **`CategoryFromSource` w `LogRegistrySink.cpp`:**

➡️ String matching na nazwie pliku jest delikatny i nierozszerzalny. Każda nowa klasa wymaga edycji tej funkcji. Lepsza alternatywa: każdy plik `.cpp` deklaruje statyczny tag:
```cpp
// Na początku każdego pliku .cpp
static constexpr DefectStudio::LogCategory FILE_LOG_CATEGORY = DefectStudio::LogCategory::Config;
```
A `LogRegistrySink` odczytuje go przez... no właśnie, problem polega na tym że `spdlog` nie przekazuje tej informacji. Realne rozwiązanie: `DS_LOG_INFO` etc. ← makro może przyjąć kategorię jako parametr opcjonalny, albo kategoria jest ustawiana per-logger-name i mapowana przez `LogRegistrySink`. Najmniej inwazyjne teraz: dodać komentarz że `categoryFromSource` jest kruchy i wymaga ręcznej aktualizacji.

---

## 9. Odchudzenie `Application` i `Application.cpp`

`Application.cpp` ma ~1063 linii i łączy 8+ logicznie odrębnych odpowiedzialności. Poniżej schemat podziału który nie wymaga zmiany publicznego API `Application`.

### Aktualne odpowiedzialności `Application`

| Odpowiedzialność                                        | Linie szacunkowo |
| ------------------------------------------------------- | ---------------- |
| Lifecycle orchestration (`Run`, `Shutdown`, `mainLoop`) | ~100             |
| GLFW + Window + OpenGL inicjalizacja                    | ~120             |
| Wiring warstw + composition root                        | ~80              |
| Konfiguracja i ścieżki startowe                         | ~100             |
| Logger startup/shutdown                                 | ~30              |
| Crash handlers                                          | ~50              |
| Frame loop + ImGui frame                                | ~150             |
| Event queue + platform events                           | ~80              |
| Asset management                                        | ~40              |
| Błędy blokujące + `m_BlockingError`                     | ~30              |
| Singleton `s_Instance` + `Get()`                        | ~20              |

### Proponowany podział

```
Application.cpp           ← zostaje: Run, Shutdown, mainLoop, OnEvent, singleton
ApplicationBootstrap.cpp  ← NOWE: createFromSpecification, wszystkie initializeXxx
ApplicationWindow.cpp     ← NOWE: initializeGlfw, createMainWindow, initializeGraphics,
                                   shutdownWindow, shutdownGlfw, configureInputBackend
ApplicationFrame.cpp      ← NOWE: beginFrame, drawMainPanel, renderFrame,
                                   runMainLoopFrame
```

**Ważne:** to są prywatne metody – mogą być zdefiniowane w osobnych plikach `.cpp` które `#include "Application.hpp"`. Nie wymaga to nowych klas ani zmian w publicznym API.

```cpp
// ApplicationBootstrap.cpp
#include "App/Application.hpp"
// ... implementacje initializeEventInfrastructure, initializeCoreRuntimeServices itp.
```

### Co przenieść do `ImGuiLayer`

🟠 Aktualnie w `Application`:
- `m_BlockingError` + `ShowBlockingError` → `ImGuiLayer::SetPendingBlockingError` + render w `OnImGuiRender`
- `drawMainPanel` (ImGui dockspace, menu bar) → `ImGuiLayer::OnImGuiRender`
- `beginFrame` (ImGui NewFrame) → `ImGuiLayer::BeginFrame`

### Docelowy `Application.cpp` – ~300 linii

```cpp
// Zostaje w Application.cpp:
Application Application::Create(int argc, char **argv) { ... }  // deleguje do bootstrap
int Application::Run() { mainLoop(); return exitCode; }
void Application::Shutdown() { m_Runtime.lifecycle.TryBeginShutdown(); }
void Application::EmitEvent(EventVariant event) { ... }
void Application::mainLoop() { /* pętla, deleguje do frame */ }
Application &Application::Get() { ... }
// + gettery runtime services
```

### Kolejność refactoringu `Application.cpp`

1. Przenieść `m_BlockingError` + `ShowBlockingError` do `ImGuiLayer` (tydzień 1).
2. Przenieść `drawMainPanel` + `beginFrame` + `renderFrame` do `ImGuiLayer::OnImGuiRender` (tydzień 1).
3. Wydzielić `ApplicationWindow.cpp` z metodami GLFW/OpenGL (tydzień 2).
4. Wydzielić `ApplicationBootstrap.cpp` z wszystkimi `initializeXxx` (tydzień 3).
5. `Application.cpp` zostaje z ~300 liniami lifecycle + singleton + gettery.

---

## 10. Największe ryzyka architektoniczne

### RYZYKO 1: Core → App dependency inversion
**Powaga:** 🔴 Critical  
**Lokalizacja:** `Core/JobSystem/JobSystem.cpp`, `Core/CoreLayer.cpp`, `Core/ProgressTrackingSystem/ProgressTracker.cpp`  
**Naprawa:** Przenieść `JobEvents.hpp` do `Core/JobSystem/`. Zakres: 3-4 pliki.

### RYZYKO 2: `ApplyRedoRecord` wywołuje `Execute` zamiast `Redo`
**Powaga:** 🔴 Critical  
**Lokalizacja:** `Core/Undo/UndoStack.cpp`  
**Naprawa:** Dodać `virtual Result<void> Redo()` do `ICommand` z defaultem delegującym do `Execute`.

### RYZYKO 3: `Notifier::Publish` z wątku roboczego
**Powaga:** 🔴 Critical  
**Lokalizacja:** `Core/Notifications/Notifier.cpp`  
**Naprawa:** Zastąpić `Publish` → `Queue`. 1 linia.

### RYZYKO 4: `DS_ASSERT` wyłączony w produkcji
**Powaga:** 🟠 High  
**Naprawa:** Rozdzielić debug asserts od runtime precondition checks.

### RYZYKO 5: `Application::Get()` w Presentation/Demo
**Powaga:** 🟠 High  
**Naprawa:** Wstrzyknąć przez konstruktor lub `BindRuntimeServices`.

### RYZYKO 6: `Application.cpp` – 1063 linie
**Powaga:** 🟠 High  
**Lokalizacja:** `App/Application.cpp`  
**Opis:** Klasa łączy lifecycle, GLFW, ImGui, konfigurację, crash handlers, assety i event loop.  
**Naprawa:** Podział na `ApplicationBootstrap.cpp`, `ApplicationWindow.cpp`, `ApplicationFrame.cpp`. Przeniesienie UI state do `ImGuiLayer`.

### RYZYKO 7: `DemoLayer` w Dist build
**Powaga:** 🟡 Medium  
**Naprawa:** `#ifndef DS_DIST`. 1 linia.

### RYZYKO 8: `Memory.hpp` inkluduje `dspch.hpp`
**Powaga:** 🟡 Medium  
**Naprawa:** Usunąć, dodać `<memory>`.

### RYZYKO 9: `NotificationCenter` bez `RemoveListener`
**Powaga:** 🟡 Medium  
**Naprawa:** Dodać `[[nodiscard]] std::size_t RegisterListener(Listener)` + `RemoveListener(id)`.

### RYZYKO 10: `CommandRegistry` – zbyt wiele odpowiedzialności
**Powaga:** 🟡 Medium  
**Naprawa:** Wydzielić `CommandService`.

---

## 11. Konkretne rekomendacje

### Top 10 najważniejszych problemów

1. Core inkluduje App
2. `ApplyRedoRecord` wywołuje `Execute` zamiast `Redo`
3. `Notifier::Publish` zamiast `Queue` – data race
4. `Application.cpp` – 1063 linie, 8+ odpowiedzialności
5. `Application::Get()` w Presentation/Demo
6. `DS_ASSERT` wyłączony w Release/Dist
7. `DemoLayer` w Dist build
8. `Memory.hpp` inkluduje `dspch.hpp`
9. Anonimowe namespaces w 9+ plikach
10. `CommandRegistry` – zbyt wiele odpowiedzialności

### Top 10 rekomendowanych refactoringów

1. Dodać `virtual Redo()` do `ICommand`, naprawić `ApplyRedoRecord`
2. Zastąpić `Publish` → `Queue` w `Notifier`
3. Przenieść `m_BlockingError` + rendering błędów do `ImGuiLayer`
4. Przenieść `drawMainPanel/beginFrame/renderFrame` do `ImGuiLayer`
5. Przenieść `JobEvents.hpp` do `Core/`
6. Wydzielić `CommandService` z `CommandRegistry`
7. Wydzielić `ApplicationWindow.cpp` i `ApplicationBootstrap.cpp`
8. Dodać `#ifndef DS_DIST` wokół DemoLayer
9. Usunąć `dspch.hpp` z `Memory.hpp`
10. Zastąpić anonimowe namespaces `static`

### Top 5 decyzji architektonicznych do podjęcia teraz

1. Gdzie żyją eventy Core systemów? (`JobEvents.hpp` → `Core/`)
2. `CommandRegistry` vs `CommandService` – podział przed rozbudową Domain
3. `CapabilityRegistry` wewnątrz `CapabilityService` – `Application` trzyma tylko Service
4. Ujednolicenie error handling – `Result<T>` wszędzie przed domeną
5. `ImGuiLayer` jako właściciel całego stanu UI (blocking error, toast queue, dockspace)

---

## 12. Krótkoterminowy plan naprawczy

**Tydzień 1 – poprawność i UI state:**
- `ICommand::Redo()` + naprawienie `ApplyRedoRecord`
- `Notifier::appendAndPublish` → `Queue` zamiast `Publish`
- `ImGuiLayer` subskrybuje `NotificationEvent`, renderuje toast w `OnImGuiRender`
- Przenieść `m_BlockingError` + `ShowBlockingError` do `ImGuiLayer`
- `#ifndef DS_DIST` wokół DemoLayer

**Tydzień 2 – architektura Core:**
- Przenieść `JobEvents.hpp` do `Core/JobSystem/`
- Zaktualizować includes w `JobSystem.cpp`, `ProgressTracker.cpp`, `CoreLayer.cpp`
- `CapabilityRegistry` → prywatny member `CapabilityService`
- `CapabilityService::Require()` → `Result<void>`

**Tydzień 3 – odchudzenie Application:**
- Wydzielić `ApplicationWindow.cpp` (GLFW + OpenGL)
- Wydzielić `ApplicationBootstrap.cpp` (wszystkie `initializeXxx`)
- Przenieść `drawMainPanel/beginFrame/renderFrame` do `ImGuiLayer`
- Naprawić `DS_ASSERT` – osobne makro dla runtime preconditions

**Tydzień 4 – porządki:**
- Usunąć `dspch.hpp` z `Memory.hpp`
- Zastąpić anonimowe namespaces → `static`
- Przenieść Logging do `Core/Logging/`
- Podzielić `KeyBinding.hpp` na osobne pliki
- Podzielić `DemoLayer` na osobne pliki
- Przenieść `TestJobs` do `tests/`

---

## 13. Długoterminowy plan

**Gdy Domain zacznie rosnąć:**
- `Result<T>` everywhere przed pierwszymi parserami
- `ProjectWorkspace` jako runtime container (ADR-002)
- `CommandService` jest gotowy – Domain rejestruje własne komendy

**Gdy ScientificRuntime dostanie implementację:**
- Python runtime za `IScriptingRuntime` interfejsem (ADR-003)
- `ProgressTracker` od razu dla długich obliczeń

**Gdy Storage dostanie implementację:**
- Separacja Storage/IO (ADR-004)
- `UndoStack::SetMaxDepth()` przed dużymi danymi w komendach

**Gdy pojawi się renderer:**
- Izolować OpenGL calls przed `IRenderer`
- `ASSERT_MAIN_THREAD()` w renderze

---

## Czy obecny kierunek architektury jest dobry?

**Tak, warto kontynuować.**

Uwagi autora projektu są w ogromnej większości trafne i dobrze oceniają sytuację. Autor wykazuje intuicję architektoniczną – widzi te same problemy co zewnętrzny reviewer, choć często z nieco innej perspektywy. Kilka uwag (pkt 3 o LoggerController, pkt 28 o typie wyniku Execute) to otwarte pytania architektoniczne bez jednoznacznej odpowiedzi, co jest właściwą postawą – te decyzje warto podjąć świadomie, nie przez domysł.

**Co poprawić natychmiast:**
- `Notifier::Queue` (thread safety)
- `ICommand::Redo()` (poprawność domenowa)
- `ImGuiLayer` jako właściciel UI state (architektura)

**Czego absolutnie unikać:**
- `Application::Get()` w nowym kodzie
- `EventBus::Publish` z wątków roboczych
- Dodawania systemów bez ujednolicenia error handling