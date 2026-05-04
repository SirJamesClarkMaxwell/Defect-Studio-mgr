# Defect Studio – Copilot Instructions (pozostałe zadania)

> Nie opisuj w czacie co robisz. Nie wyjaśniaj zmian. Wykonuj je bezpośrednio.
> Każda SESJA to osobny commit. Nie łącz sesji.
> Konwencje projektu: `PascalCase` publiczne, `camelCase` prywatne, `m_PascalCase` pola,
> `Ref<T>`/`Unique<T>`/`WeakRef<T>`, `[[nodiscard]]`, `Result<T>`, `static` zamiast anonymous namespace.

---

## SESJA A – CommandOutcome: lepszy konstruktor

**Przeczytaj przed rozpoczęciem:**
`src/Core/Commands/Command.hpp`,
`src/Core/Commands/CommandRegistry.cpp`

### A.1 Dodaj statyczną metodę fabryczną do `CommandOutcome`

W `src/Core/Commands/Command.hpp`, w struct `CommandOutcome`, dodaj po polach:

```cpp
struct CommandOutcome
{
    CommandID   id;
    std::string description;
    bool        undoable          = false;
    bool        pushedToUndoStack = false;

    // Factory: tworzy outcome z komendy przed próbą wypchnięcia na UndoStack.
    // pushedToUndoStack należy ustawić osobno po wywołaniu UndoStack::PushExecuted.
    [[nodiscard]] static CommandOutcome FromCommand(const CommandID &id, const ICommand &command)
    {
        return {id, command.Description(), command.IsUndoable(), false};
    }
};
```

### A.2 Zaktualizuj `CommandRegistry::Execute`

W `src/Core/Commands/CommandRegistry.cpp`, w funkcji `Execute`, znajdź blok:

```cpp
CommandOutcome outcome;
outcome.id = id;
outcome.description = command->Description();
outcome.undoable = command->IsUndoable();

if (outcome.undoable && m_UndoStack != nullptr)
    outcome.pushedToUndoStack = m_UndoStack->PushExecuted(std::move(command));
```

Zastąp:

```cpp
CommandOutcome outcome = CommandOutcome::FromCommand(id, *command);
if (outcome.undoable && m_UndoStack != nullptr)
    outcome.pushedToUndoStack = m_UndoStack->PushExecuted(std::move(command));
```

---

## SESJA B – CommandService

**Przeczytaj przed rozpoczęciem:**
`src/Core/Commands/CommandRegistry.hpp`,
`src/Core/Commands/CommandRegistry.cpp`,
`src/Core/Input/KeyBinding.hpp`,
`src/Core/Input/KeyBinding.cpp`,
`src/Core/CoreLayer.hpp`

### B.1 Utwórz `src/Core/Commands/CommandService.hpp`

```cpp
#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandContext.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
    class UndoStack;

    class CommandService
    {
    public:
        CommandService(
            Ref<CommandRegistry>       registry,
            WeakRef<UndoStack>         undoStack,
            WeakRef<CapabilityService> capabilityService = {});

        CommandService(const CommandService &) = delete;
        CommandService &operator=(const CommandService &) = delete;
        CommandService(CommandService &&)                 = delete;
        CommandService &operator=(CommandService &&)      = delete;

        // Główny punkt wejścia – jedyna ścieżka wykonania komendy
        [[nodiscard]] Result<CommandOutcome> Execute(
            const CommandID &id,
            CommandContext   context = {});

        // Undo/Redo przechodzi przez ten sam serwis
        [[nodiscard]] Result<void> Undo(CommandContext context = {});
        [[nodiscard]] Result<void> Redo(CommandContext context = {});

        // Zapytania – delegowane do Registry
        [[nodiscard]] Result<void>        CanExecute(const CommandID &id) const;
        [[nodiscard]] Result<CommandMeta> GetMeta(const CommandID &id)    const;
        [[nodiscard]] bool                HasCommand(const CommandID &id)  const;

        // Obserwatorzy wykonania (UI, logi, testy)
        [[nodiscard]] std::size_t AddObserver(CommandExecutionObserver observer);
        void                      RemoveObserver(std::size_t observerId);

    private:
        void notifyObservers(const CommandExecutionEvent &event) const;

        Ref<CommandRegistry>       m_Registry;
        WeakRef<UndoStack>         m_UndoStack;
        WeakRef<CapabilityService> m_CapabilityService;

        std::unordered_map<std::size_t, CommandExecutionObserver> m_Observers;
        std::size_t m_NextObserverId = 1;
        bool        m_IsExecuting    = false;
    };

} // namespace DefectStudio
```

### B.2 Utwórz `src/Core/Commands/CommandService.cpp`

```cpp
#include "Core/dspch.hpp"

#include "Core/Commands/CommandService.hpp"

#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	[[nodiscard]] static StructuredError makeServiceError(
		std::string code,
		std::string userMessage,
		std::string technicalDetails,
		std::string suggestion,
		ErrorCategory category = ErrorCategory::Validation)
	{
		return StructuredError{
			category,
			Severity::Error,
			std::move(userMessage),
			std::move(technicalDetails),
			std::move(suggestion),
			"CommandService",
			std::move(code)};
	}

    CommandService::CommandService(
        Ref<CommandRegistry>       registry,
        WeakRef<UndoStack>         undoStack,
        WeakRef<CapabilityService> capabilityService)
        : m_Registry(std::move(registry))
        , m_UndoStack(std::move(undoStack))
        , m_CapabilityService(std::move(capabilityService))
    {
        DS_ASSERT(m_Registry != nullptr, "CommandService requires a valid CommandRegistry");
    }

    Result<CommandOutcome> CommandService::Execute(const CommandID &id, CommandContext context)
    {
        if (m_IsExecuting)
        {
            return makeServiceError(
                "command.execute.reentrant",
                "Command execution was rejected.",
                "Command '" + id.value + "' attempted to execute while another command is active.",
                "Defer nested work through the job system or main-thread dispatcher.");
        }

        // Capability check przed delegacją do Registry
        auto metaResult = m_Registry->GetMeta(id);
        if (!metaResult)
            return metaResult.Error();

        if (!metaResult->requiredCapabilities.empty())
        {
            auto capService = m_CapabilityService.lock();
            if (capService == nullptr)
            {
                return makeServiceError(
                    "command.capability.no_service",
                    "Command cannot verify required capabilities.",
                    "Command '" + id.value + "' declares capability requirements but no CapabilityService is available.",
                    "Attach CapabilityService to CommandService during application startup.",
                    ErrorCategory::Capability);
            }

            for (const CapabilityID &cap : metaResult->requiredCapabilities)
            {
                auto requireResult = capService->Require(cap.value);
                if (!requireResult)
                {
                    notifyObservers({id, metaResult->name, CommandExecutionState::Rejected, requireResult.Error()});
                    return requireResult.Error();
                }
            }
        }

        m_IsExecuting = true;
        notifyObservers({id, metaResult->name, CommandExecutionState::Started, std::nullopt});

        // Deleguj wykonanie do Registry (factory + Execute)
        auto result = m_Registry->Execute(id, std::move(context));

        m_IsExecuting = false;

        if (!result)
        {
            notifyObservers({id, metaResult->name, CommandExecutionState::Failed, result.Error()});
            return result.Error();
        }

        // Undo push – Registry już to zrobił jeśli ma UndoStack;
        // tutaj nie robimy tego ponownie, outcome pochodzi z Registry.

        notifyObservers({id, metaResult->name, CommandExecutionState::Succeeded, std::nullopt});
        return result;
    }

    Result<void> CommandService::Undo(CommandContext context)
    {
        auto undoStack = m_UndoStack.lock();
        if (undoStack == nullptr)
        {
            return makeServiceError(
                "command.undo.no_stack",
                "Undo is not available.",
                "CommandService has no UndoStack attached.",
                "Attach an UndoStack to CommandService during application startup.");
        }
        return undoStack->Undo(std::move(context));
    }

    Result<void> CommandService::Redo(CommandContext context)
    {
        auto undoStack = m_UndoStack.lock();
        if (undoStack == nullptr)
        {
            return makeServiceError(
                "command.redo.no_stack",
                "Redo is not available.",
                "CommandService has no UndoStack attached.",
                "Attach an UndoStack to CommandService during application startup.");
        }
        return undoStack->Redo(std::move(context));
    }

    Result<void> CommandService::CanExecute(const CommandID &id) const
    {
        return m_Registry->CanExecute(id);
    }

    Result<CommandMeta> CommandService::GetMeta(const CommandID &id) const
    {
        return m_Registry->GetMeta(id);
    }

    bool CommandService::HasCommand(const CommandID &id) const
    {
        return m_Registry->HasCommand(id);
    }

    std::size_t CommandService::AddObserver(CommandExecutionObserver observer)
    {
        if (!observer)
            return 0;
        const std::size_t observerId = m_NextObserverId++;
        m_Observers.emplace(observerId, std::move(observer));
        return observerId;
    }

    void CommandService::RemoveObserver(std::size_t observerId)
    {
        m_Observers.erase(observerId);
    }

    void CommandService::notifyObservers(const CommandExecutionEvent &event) const
    {
        for (const auto &[id, observer] : m_Observers)
        {
            (void)id;
            if (observer)
                observer(event);
        }
    }

} // namespace DefectStudio
```

### B.3 Zaktualizuj `KeyInputProcessor` – Registry → Service

W `src/Core/Input/KeyBinding.hpp`, zmień klasę `KeyInputProcessor`:

```cpp
class KeyInputProcessor
{
public:
    KeyInputProcessor(
        CommandService  &commandService,   // ← było: CommandRegistry
        KeymapResolver  &resolver,
        ContextManager  &contextManager);

    [[nodiscard]] Result<KeyInputResult> HandleKeyPressed(
        const KeyChord &chord,
        CommandContext  context = {});

private:
    CommandService  &m_CommandService;     // ← było: m_CommandRegistry
    KeymapResolver  &m_Resolver;
    ContextManager  &m_ContextManager;
};
```

Dodaj forward declaration ponad klasą (usuń `#include "Core/Commands/CommandRegistry.hpp"` jeśli nie jest używany przez inne typy w tym pliku, dodaj forward declaration zamiast):

```cpp
class CommandService;
```

Jeśli `CommandRegistry.hpp` jest potrzebny dla `CommandExecutionEvent` lub `CommandFactory` – zostaw include, tylko podmień typ w `KeyInputProcessor`.

W `src/Core/Input/KeyBinding.cpp`, zmień implementację:

```cpp
KeyInputProcessor::KeyInputProcessor(
    CommandService &commandService,
    KeymapResolver &resolver,
    ContextManager &contextManager)
    : m_CommandService(commandService)
    , m_Resolver(resolver)
    , m_ContextManager(contextManager)
{
}

Result<KeyInputResult> KeyInputProcessor::HandleKeyPressed(
    const KeyChord &chord, CommandContext context)
{
    std::optional<KeyBinding> binding = m_Resolver.Resolve(chord, m_ContextManager);
    if (!binding)
        return KeyInputResult{};

    if (context.GetSource().empty())
        context.SetSource("KeyInputProcessor");

    auto result = m_CommandService.Execute(binding->commandId, std::move(context));
    if (!result)
        return result.Error();

    return KeyInputResult{.handled = true, .commandId = binding->commandId};
}
```

Dodaj `#include "Core/Commands/CommandService.hpp"` do `KeyBinding.cpp`.

### B.4 Zaktualizuj `CoreLayer`

W `src/Core/CoreLayer.hpp` dodaj:

```cpp
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/CommandService.hpp"
#include "Core/Input/KeyBinding.hpp"
#include "Core/Undo/UndoStack.hpp"
```

Dodaj prywatne pola:

```cpp
Ref<UndoStack>          m_UndoStack;
Ref<CommandRegistry>    m_CommandRegistry;
Ref<CommandService>     m_CommandService;
Unique<KeymapResolver>  m_KeymapResolver;
Unique<ContextManager>  m_ContextManager;
```

Dodaj publiczne gettery:

```cpp
[[nodiscard]] CommandService  &GetCommandService();
[[nodiscard]] KeymapResolver  &GetKeymapResolver();
[[nodiscard]] ContextManager  &GetContextManager();
[[nodiscard]] UndoStack       &GetUndoStack();
[[nodiscard]] WeakRef<CommandService> GetCommandServiceHandle() const;
```

W `src/Core/CoreLayer.cpp`, w `OnAttach()`, dodaj po inicjalizacji JobSystem i ProgressTracker:

```cpp
m_UndoStack         = CreateRef<UndoStack>();
m_CommandRegistry   = CreateRef<CommandRegistry>();
m_CommandService    = CreateRef<CommandService>(
    m_CommandRegistry,
    CreateWeakRef(m_UndoStack));
m_KeymapResolver    = CreateUnique<KeymapResolver>();
m_ContextManager    = CreateUnique<ContextManager>();

registerSystemCommands();
```

Dodaj prywatną deklarację w `CoreLayer.hpp`:

```cpp
void registerSystemCommands();
```

### B.5 Zarejestruj systemowe komendy i skróty

W `src/Core/CoreLayer.cpp` dodaj implementację `registerSystemCommands()`:

```cpp
void CoreLayer::registerSystemCommands()
{
    auto registerCmd = [this](CommandMeta meta, CommandFactory factory) {
	//To jest bardzo brzydkie, ma być za pomocą function-binding
	
        auto result = m_CommandRegistry->Register(std::move(meta), std::move(factory));
        if (!result)
            DS_LOG_ERROR("CoreLayer: failed to register command: {}", result.Error().technicalDetails);
    };

    auto registerBinding = [this](KeyBinding binding) {
	//To jest bardzo brzydkie, ma być za pomocą function-binding
	
        auto result = m_KeymapResolver->RegisterBinding(std::move(binding));
        if (!result)
            DS_LOG_WARN("CoreLayer: keybinding conflict: {}", result.Error().technicalDetails);
    };

    // ── edit.undo ──────────────────────────────────────────────────────────
	//To jest bardzo brzydkie, ma być za pomocą function-binding

    registerCmd(
        CommandMeta{
            CommandID{"edit.undo"}, "Undo", "Edit",
            "Undo the last action.", {}, CommandFlags::HiddenFromPalette},
        [this](CommandContext &context) -> Unique<ICommand> {
            // Komenda delegate: wywołuje UndoStack::Undo przez CommandService
			// to powinno być gdzieś indziej zdefiniowane, pewnie w osobnym pliku na komendy
            struct UndoCommand final : ICommand
            {
                UndoStack &stack;
                explicit UndoCommand(UndoStack &s) : stack(s) {}
                Result<void> Execute(CommandContext &ctx) override { return stack.Undo(std::move(ctx)); }
                std::string Description() const override { return "Undo"; }
            };
            return CreateUnique<UndoCommand>(*m_UndoStack);
        });

    // ── edit.redo ──────────────────────────────────────────────────────────
	//To jest bardzo brzydkie, ma być za pomocą function-binding

    registerCmd(
        CommandMeta{
            CommandID{"edit.redo"}, "Redo", "Edit",
            "Redo the last undone action.", {}, CommandFlags::HiddenFromPalette},
        [this](CommandContext &context) -> Unique<ICommand> {
			// to powinno być gdzieś indziej zdefiniowane, pewnie w osobnym pliku na komendy
            struct RedoCommand final : ICommand
            {
                UndoStack &stack;
                explicit RedoCommand(UndoStack &s) : stack(s) {}
                Result<void> Execute(CommandContext &ctx) override { return stack.Redo(std::move(ctx)); }
                std::string Description() const override { return "Redo"; }
            };
            return CreateUnique<RedoCommand>(*m_UndoStack);
        });

    // ── app.command_palette ────────────────────────────────────────────────
	//To jest bardzo brzydkie, ma być za pomocą function-binding

    registerCmd(
        CommandMeta{
            CommandID{"app.command_palette"}, "Command Palette", "Application",
            "Open the command palette.", {}, CommandFlags::None},
        [this](CommandContext &) -> Unique<ICommand> {
			// to powinno być gdzieś indziej zdefiniowane, pewnie w osobnym pliku na komendy
            struct OpenPaletteCommand final : ICommand
            {
                Ref<EventBus> bus;
                explicit OpenPaletteCommand(Ref<EventBus> b) : bus(std::move(b)) {}
                Result<void> Execute(CommandContext &) override
                {
                    if (bus) bus->Queue(AppEvents::OpenCommandPaletteRequested{});
                    return {};
                }
                std::string Description() const override { return "Open Command Palette"; }
            };
            return CreateUnique<OpenPaletteCommand>(m_EventBus);
        });

    // ── app.quit ───────────────────────────────────────────────────────────
	//To jest bardzo brzydkie, ma być za pomocą function-binding

    registerCmd(
        CommandMeta{
            CommandID{"app.quit"}, "Quit", "Application",
            "Quit the application.", {}, CommandFlags::None},
        [this](CommandContext &) -> Unique<ICommand> {
			// to powinno być gdzieś indziej zdefiniowane, pewnie w osobnym pliku na komendy
            struct QuitCommand final : ICommand
            {
                Ref<EventBus> bus;
                explicit QuitCommand(Ref<EventBus> b) : bus(std::move(b)) {}
                Result<void> Execute(CommandContext &) override
                {
                    if (bus) bus->Queue(AppEvents::ShutdownRequested{});
                    return {};
                }
                std::string Description() const override { return "Quit Application"; }
            };
            return CreateUnique<QuitCommand>(m_EventBus);
        });

    // ── project.save ───────────────────────────────────────────────────────
	//To jest bardzo brzydkie, ma być za pomocą function-binding
    registerCmd(
        CommandMeta{
            CommandID{"project.save"}, "Save", "Project",
            "Save the current project.", {}, CommandFlags::None},
        [this](CommandContext &) -> Unique<ICommand> {
			// to powinno być gdzieś indziej zdefiniowane, pewnie w osobnym pliku na komendy
            struct SaveCommand final : ICommand
            {
                Ref<EventBus> bus;
                explicit SaveCommand(Ref<EventBus> b) : bus(std::move(b)) {}
                Result<void> Execute(CommandContext &) override
                {
                    if (bus) bus->Queue(AppEvents::ProjectSaveRequested{});
                    return {};
                }
                std::string Description() const override { return "Save Project"; }
            };
            return CreateUnique<SaveCommand>(m_EventBus);
        });

    // ── Bindingi ──────────────────────────────────────────────────────────
    registerBinding({
        "system.undo",
        KeyChord{KeyCode::Z, KeyModifiers::Ctrl},
        CommandID{"edit.undo"}, {}, KeymapLayer::Global, true});

    registerBinding({
        "system.redo",
        KeyChord{KeyCode::Y, KeyModifiers::Ctrl},
        CommandID{"edit.redo"}, {}, KeymapLayer::Global, true});

    registerBinding({
        "system.command_palette",
        KeyChord{KeyCode::P, KeyModifiers::Ctrl | KeyModifiers::Shift},
        CommandID{"app.command_palette"}, {}, KeymapLayer::Global, true});

    registerBinding({
        "system.quit",
        KeyChord{KeyCode::W, KeyModifiers::Ctrl | KeyModifiers::Shift},
        CommandID{"app.quit"}, {}, KeymapLayer::Global, true});

    registerBinding({
        "system.save",
        KeyChord{KeyCode::S, KeyModifiers::Ctrl},
        CommandID{"project.save"}, {}, KeymapLayer::Global, true});

    DS_LOG_INFO("CoreLayer: system commands and bindings registered");
}
```

W `src/App/Events/ApplicationEvents.hpp` (lub nowy plik `src/App/Events/AppEvents.hpp`), dodaj eventy jeśli nie istnieją:

```cpp
namespace DefectStudio::AppEvents
{
    struct OpenCommandPaletteRequested {};
    struct ShutdownRequested           {};
    struct ProjectSaveRequested        {};
}
```

### B.6 Zaktualizuj callsite w `DemoLayer` / `DemoBackendRuntime`

W pliku który tworzy `KeyInputProcessor` (obecnie `DemoBackendRuntime` lub `DemoLayer`), zmień:

```cpp
// Przed:
KeyInputProcessor processor(*m_BackendCommandRegistry, *m_BackendKeymapResolver, *m_BackendContextManager);

// Po (wymaga dostępu do CommandService – tymczasowo utwórz lokalny):
// CommandService dla demo tworzy się z m_BackendCommandRegistry i m_BackendUndoStack
if (!m_BackendCommandService)
{
    m_BackendCommandService = CreateUnique<CommandService>(
        Ref<CommandRegistry>(m_BackendCommandRegistry.get(), [](CommandRegistry*){}),  // non-owning
        CreateWeakRef(m_BackendUndoStack));
}
KeyInputProcessor processor(*m_BackendCommandService, *m_BackendKeymapResolver, *m_BackendContextManager);
```

Lepiej: dodaj `Unique<CommandService> m_BackendCommandService` jako member `DemoBackendRuntime` i stwórz go w `setupBackendRuntimeDemo()` po stworzeniu `m_BackendCommandRegistry` i `m_BackendUndoStack`:

```cpp
m_BackendCommandService = CreateUnique<CommandService>(
    m_BackendCommandRegistry,   // Ref<CommandRegistry>
    CreateWeakRef(m_BackendUndoStack));
```

Zmień wszystkie `m_BackendCommandRegistry->Execute(...)` w `DemoBackendRuntime` na `m_BackendCommandService->Execute(...)`.

---

## SESJA C – Persystencja keybindings przez IOLayer

**Przeczytaj przed rozpoczęciem:**
`src/IO/IOLayer.hpp`, `src/IO/IOLayer.cpp`,
`src/App/Events/ApplicationConfigEvents.hpp`

### C.1 Dodaj eventy keybindings

Utwórz `src/App/Events/KeyBindingEvents.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>
#include "Core/Input/KeyBinding.hpp"

namespace DefectStudio::AppEvents::Keymap
{
    // Emitowany przez CoreLayer gdy stan bindingów się zmieni
    struct BindingsSaveRequested
    {
        std::vector<KeyBinding> bindings;
        Path                    targetPath;
    };

    // Emitowany przez IOLayer po udanym zapisie
    struct BindingsSaved
    {
        Path savedPath;
    };

    // Emitowany przez IOLayer przy błędzie zapisu
    struct BindingsSaveFailed
    {
        std::string error;
    };

    // Emitowany przez Application na starcie – żądanie wczytania
    struct BindingsLoadRequested
    {
        Path sourcePath;
    };

    // Emitowany przez IOLayer po udanym wczytaniu
    struct BindingsLoaded
    {
        std::vector<KeyBinding> bindings;
    };

    // Emitowany przez IOLayer przy błędzie wczytania
    struct BindingsLoadFailed
    {
        std::string error;
    };
}
```

### C.2 Dodaj handler w `IOLayer`

W `src/IO/IOLayer.hpp`, dodaj prywatne metody:

```cpp
void onBindingsSaveRequested(const AppEvents::Keymap::BindingsSaveRequested &event);
void onBindingsLoadRequested(const AppEvents::Keymap::BindingsLoadRequested &event);
```

W `src/IO/IOLayer.cpp`, w `bindEvents()` dodaj subskrypcje (wzorzec identyczny jak istniejące):

```cpp
AddSubscription(subscribeIOLayer<AppEvents::Keymap::BindingsSaveRequested>(
    *m_EventBus, *this, &IOLayer::onBindingsSaveRequested));
AddSubscription(subscribeIOLayer<AppEvents::Keymap::BindingsLoadRequested>(
    *m_EventBus, *this, &IOLayer::onBindingsLoadRequested));
```

Dodaj implementacje:

```cpp
void IOLayer::onBindingsSaveRequested(const AppEvents::Keymap::BindingsSaveRequested &event)
{
    using namespace AppEvents::Keymap;
    DS_LOG_INFO("IOLayer: saving keybindings to '{}'", event.targetPath.String());

    std::string contents;
    std::string error;
    if (!serializeBindings(event.bindings, contents, error))
    {
        m_EventBus->Queue(BindingsSaveFailed{error});
        DS_LOG_ERROR("IOLayer: keybindings serialization failed: {}", error);
        return;
    }

    // Użyj istniejącego wzorca TextFileIO
    auto result = TextFileIO::WriteText(event.targetPath, contents);
    if (!result)
    {
        m_EventBus->Queue(BindingsSaveFailed{result.Error().technicalDetails});
        DS_LOG_ERROR("IOLayer: keybindings write failed: {}", result.Error().technicalDetails);
        return;
    }

    m_EventBus->Queue(BindingsSaved{event.targetPath});
    DS_LOG_INFO("IOLayer: keybindings saved successfully");
}

void IOLayer::onBindingsLoadRequested(const AppEvents::Keymap::BindingsLoadRequested &event)
{
    using namespace AppEvents::Keymap;
    DS_LOG_INFO("IOLayer: loading keybindings from '{}'", event.sourcePath.String());

    auto readResult = TextFileIO::ReadText(event.sourcePath);
    if (!readResult)
    {
        m_EventBus->Queue(BindingsLoadFailed{readResult.Error().technicalDetails});
        DS_LOG_WARN("IOLayer: keybindings file not found or unreadable: {}", event.sourcePath.String());
        return;
    }

    std::vector<KeyBinding> bindings;
    std::string error;
    if (!deserializeBindings(*readResult, bindings, error))
    {
        m_EventBus->Queue(BindingsLoadFailed{error});
        DS_LOG_ERROR("IOLayer: keybindings deserialization failed: {}", error);
        return;
    }

    m_EventBus->Queue(BindingsLoaded{std::move(bindings)});
}
```

### C.3 Dodaj minimalne serializatory YAML

W `src/IO/IOLayer.cpp` (lub nowy `src/IO/KeyBindingSerializer.cpp`), dodaj jako funkcje `static`:

```cpp
static bool serializeBindings(
    const std::vector<KeyBinding> &bindings,
    std::string &outContents,
    std::string &outError)
{
    // Format YAML:
    // bindings:
    //   - id: "system.undo"
    //     chord: "Ctrl+Z"
    //     command: "edit.undo"
    //     context: ""
    //     layer: 0
    //     enabled: true
    try
    {
        YAML::Emitter emit;
        emit << YAML::BeginMap;
        emit << YAML::Key << "bindings" << YAML::Value << YAML::BeginSeq;
        for (const KeyBinding &b : bindings)
        {
            emit << YAML::BeginMap;
            emit << YAML::Key << "id"      << YAML::Value << b.id;
            emit << YAML::Key << "chord"   << YAML::Value << ToString(b.chord);
            emit << YAML::Key << "command" << YAML::Value << b.commandId.value;
            emit << YAML::Key << "context" << YAML::Value << b.when.GetExpression();
            emit << YAML::Key << "layer"   << YAML::Value << static_cast<int>(b.layer);
            emit << YAML::Key << "enabled" << YAML::Value << b.enabled;
            emit << YAML::EndMap;
        }
        emit << YAML::EndSeq;
        emit << YAML::EndMap;
        outContents = emit.c_str();
        return true;
    }
    catch (const std::exception &e)
    {
        outError = e.what();
        return false;
    }
}
```

Deserializację zaimplementuj analogicznie parsując `YAML::Node`. Chord parsuj przez istniejącą lub nową funkcję `ParseKeyChord(std::string_view) -> std::optional<KeyChord>`.

### C.4 Dodaj `ParseKeyChord` do `KeyBinding`

W `src/Core/Input/KeyBinding.hpp` dodaj deklarację:

```cpp
[[nodiscard]] std::optional<KeyChord> ParseKeyChord(std::string_view text);
```

W `src/Core/Input/KeyBinding.cpp` dodaj implementację która parsuje format `"Ctrl+Shift+Z"` odwracając logikę `ToString(KeyChord)`.

### C.5 Wczytaj bindingi na starcie

W `src/App/Application.cpp` (lub `ApplicationBootstrap.cpp`), po inicjalizacji IOLayer i CoreLayer, dodaj:

```cpp
const Path keybindingsPath = ConfigManager::GetKeybindingsPath(m_Config.directory);
m_EventBus->Queue(AppEvents::Keymap::BindingsLoadRequested{keybindingsPath});
m_EventBus->ProcessQueue();
```

W CoreLayer, subskrybuj `BindingsLoaded`:

```cpp 
//To jest bardzo brzydkie, ma być za pomocą function-binding
AddSubscription(m_EventBus->Subscribe<AppEvents::Keymap::BindingsLoaded>(
    [this](const AppEvents::Keymap::BindingsLoaded &event) {
        for (const KeyBinding &binding : event.bindings)
        {
            // Pomiń bindingi systemowe (id zaczyna się od "system.") – nie nadpisuj
            if (binding.id.starts_with("system."))
                continue;
            auto result = m_KeymapResolver->RegisterBinding(binding);
            if (!result)
                DS_LOG_WARN("CoreLayer: loaded binding conflict: {}", result.Error().technicalDetails);
        }
        DS_LOG_INFO("CoreLayer: {} user keybindings loaded", event.bindings.size());
    }));
```

---

## SESJA D – Sekcja KeyBindings w Settings (wyświetlanie i wyszukiwanie)

**Przeczytaj przed rozpoczęciem:**
`src/Presentation/Panels/SettingsPanel.hpp`,
`src/Presentation/Panels/Settings.cpp`

### D.1 Dodaj zależność na `KeymapResolver` do `SettingsPanel`

W `src/Presentation/Panels/SettingsPanel.hpp`:

Dodaj forward declaration:
```cpp
class KeymapResolver;
class CommandRegistry;
```

Rozszerz konstruktor:
```cpp
explicit SettingsPanel(
    Ref<EventBus>          eventBus        = {},
    WeakRef<JobSystem>     jobSystem       = {},
    WeakRef<EditorUiState> uiState         = {},
    WeakRef<KeymapResolver> keymapResolver = {},   // ← nowe
    WeakRef<CommandRegistry> commandRegistry = {},  // ← nowe
    std::string            title           = "SettingsPanel",
    bool                   visibleByDefault = true);
```

Dodaj prywatne pola:
```cpp
WeakRef<KeymapResolver>  m_KeymapResolver;
WeakRef<CommandRegistry> m_CommandRegistry;
```

Dodaj prywatne pola stanu UI dla zakładki Input:
```cpp
std::array<char, 128> m_KeyBindingSearchBuffer{};
```

Dodaj prywatną metodę:
```cpp
void renderInputTab();
```

Zakładka `Input` już istnieje w enum `Tab` – nie dodawaj ponownie.

### D.2 Implementuj `renderInputTab`

W `src/Presentation/Panels/Settings.cpp`, dodaj implementację:

```cpp
void SettingsPanel::renderInputTab()
{
    auto resolver = m_KeymapResolver.lock();
    auto registry = m_CommandRegistry.lock();

    if (!resolver)
    {
        ImGui::TextDisabled("KeymapResolver not available.");
        return;
    }

    ImGui::SeparatorText("Key Bindings");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint(
        "##keybind_search",
        "Search by command name, description or chord (e.g. Ctrl+Z)",
        m_KeyBindingSearchBuffer.data(),
        m_KeyBindingSearchBuffer.size());

    const std::string_view searchText{m_KeyBindingSearchBuffer.data()};
    const std::vector<KeyBinding> allBindings = resolver->ListBindings();

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders       |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_ScrollY       |
        ImGuiTableFlags_SizingStretchProp;

    const float tableHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
    if (!ImGui::BeginTable("##keybindings_table", 4, tableFlags, ImVec2(0.0f, tableHeight)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Shortcut",    ImGuiTableColumnFlags_WidthFixed,   110.0f);
    ImGui::TableSetupColumn("Command",     ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("Context",     ImGuiTableColumnFlags_WidthStretch, 0.2f);
    ImGui::TableHeadersRow();

    for (const KeyBinding &binding : allBindings)
    {
        if (!binding.enabled)
            continue;

        const std::string chordStr = ToString(binding.chord);

        // Pobierz opis komendy jeśli registry dostępne
        std::string commandName = binding.commandId.value;
        std::string commandDesc;
        if (registry)
        {
            auto meta = registry->GetMeta(binding.commandId);
            if (meta)
            {
                commandName = meta->name;
                commandDesc = meta->description;
            }
        }

        // Filtrowanie
        if (!searchText.empty())
        {
            const auto contains = [](std::string_view haystack, std::string_view needle) {
                // case-insensitive contains
                auto it = std::search(
                    haystack.begin(), haystack.end(),
                    needle.begin(), needle.end(),
                    [](unsigned char a, unsigned char b) {
                        return std::tolower(a) == std::tolower(b);
                    });
                return it != haystack.end();
            };

            const bool matches =
                contains(chordStr,                   searchText) ||
                contains(commandName,                searchText) ||
                contains(commandDesc,                searchText) ||
                contains(binding.commandId.value,    searchText) ||
                contains(binding.when.GetExpression(), searchText);

            if (!matches)
                continue;
        }

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(chordStr.c_str());

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(commandName.c_str());
        if (commandName != binding.commandId.value)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", binding.commandId.value.c_str());
        }

        ImGui::TableNextColumn();
        if (!commandDesc.empty())
            ImGui::TextUnformatted(commandDesc.c_str());
        else
            ImGui::TextDisabled("-");

        ImGui::TableNextColumn();
        const std::string &ctx = binding.when.GetExpression();
        ImGui::TextDisabled("%s", ctx.empty() ? "global" : ctx.c_str());
    }

    ImGui::EndTable();

    // Konflikty
    const auto &conflicts = resolver->GetConflicts();
    if (!conflicts.empty())
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
        ImGui::Text("⚠ %zu binding conflict(s) detected", conflicts.size());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            for (const KeyBindingConflict &c : conflicts)
            {
                ImGui::Text("'%s' conflicts with '%s' on %s",
                    c.newBinding.id.c_str(),
                    c.existingBinding.id.c_str(),
                    ToString(c.newBinding.chord).c_str());
            }
            ImGui::EndTooltip();
        }
    }
}
```

### D.3 Podepnij `renderInputTab` do istniejącej struktury renderowania

W `Settings.cpp`, w switch/if który wybiera zakładkę do renderowania, znajdź `case Tab::Input:` lub odpowiedni blok i dodaj:

```cpp
case Tab::Input:
    renderInputTab();
    break;
```

### D.4 Zaktualizuj tworzenie `SettingsPanel` w `EditorLayer` lub `Application`

Znajdź miejsce gdzie tworzony jest `SettingsPanel`. Dodaj przekazanie zależności:

```cpp
CreateRef<SettingsPanel>(
    eventBus,
    jobSystem,
    uiState,
    coreLayer.GetKeymapResolverHandle(),    // WeakRef<KeymapResolver>
    coreLayer.GetCommandRegistryHandle(),   // WeakRef<CommandRegistry>
    "Settings");
```

Dodaj gettery `GetKeymapResolverHandle()` i `GetCommandRegistryHandle()` do `CoreLayer` jeśli jeszcze nie istnieją.

---

## SESJA E – Naprawa `CancelGroup` w `UndoStack`

**Przeczytaj przed rozpoczęciem:**
`src/Core/Undo/UndoStack.hpp`,
`src/Core/Undo/UndoStack.cpp`

### E.1 Dodaj flagę propagacji anulowania

Problem: `m_GroupDepth = 0` niszczy całe zagnieżdżenie naraz. Przy nested groups chcemy żeby `Cancel` inner grupy propagował anulowanie do outer grupy przez `EndGroup`.

W `src/Core/Undo/UndoStack.hpp`, dodaj prywatne pole:

```cpp
bool m_GroupCancelled = false;
```

### E.2 Napraw `CancelGroup`

W `src/Core/Undo/UndoStack.cpp`, zmień implementację `CancelGroup`:

```cpp
Result<void> UndoStack::CancelGroup(CommandContext context)
{
    if (m_GroupDepth == 0 || !m_ActiveGroup)
    {
        return MakeUndoError(
            "undo.group.none",
            "Undo group cannot be cancelled.",
            "CancelGroup was called without a matching BeginGroup.",
            "Use UndoScope or pair BeginGroup/CancelGroup calls.");
    }

    --m_GroupDepth;
    m_GroupCancelled = true;  // propaguje do zewnętrznych EndGroup

    if (m_GroupDepth == 0)
    {
        // Ostatni poziom: cofnij co zostało zgromadzone
        UndoRecord record = std::move(*m_ActiveGroup);
        m_ActiveGroup.reset();
        m_GroupCancelled = false;

        m_IsApplying = true;
        Result<void> result = ApplyUndoRecord(record, context);
        m_IsApplying = false;
        return result;
    }

    // Jesteśmy nadal w zewnętrznej grupie – zostaniemy anulowani gdy
    // ta zewnętrzna grupa wywoła EndGroup lub CancelGroup.
    return {};
}
```

### E.3 Zaktualizuj `EndGroup` – obsługa propagacji

W `src/Core/Undo/UndoStack.cpp`, znajdź implementację `EndGroup`. Dodaj sprawdzenie flagi `m_GroupCancelled`:

```cpp
Result<void> UndoStack::EndGroup()
{
    if (m_GroupDepth == 0 || !m_ActiveGroup)
    {
        return MakeUndoError(
            "undo.group.none",
            "Undo group cannot be committed.",
            "EndGroup was called without a matching BeginGroup.",
            "Use UndoScope or pair BeginGroup/EndGroup calls.");
    }

    --m_GroupDepth;

    if (m_GroupDepth == 0)
    {
        UndoRecord record = std::move(*m_ActiveGroup);
        m_ActiveGroup.reset();

        // Jeśli wewnętrzna grupa została anulowana – cofnij zamiast commitować
        if (m_GroupCancelled)
        {
            m_GroupCancelled = false;
            CommandContext context;  // pusty context dla rollbacku
            m_IsApplying = true;
            Result<void> result = ApplyUndoRecord(record, context);
            m_IsApplying = false;
            DS_LOG_WARN("UndoStack: EndGroup converted to rollback due to inner CancelGroup");
            return result;
        }

        return PushRecord(std::move(record));
    }

    return {};
}
```

### E.4 Zresetuj `m_GroupCancelled` w `Clear()`

W `UndoStack::Clear()`, dodaj:

```cpp
m_GroupCancelled = false;
```

### E.5 Zaktualizuj komentarz dokumentujący semantykę

W `UndoStack.cpp`, bezpośrednio przed `CancelGroup`, zastąp istniejący komentarz (dodany w poprzedniej sesji) na:

```cpp
// CancelGroup semantics:
// - Decrements m_GroupDepth (symmetric with EndGroup)
// - Sets m_GroupCancelled = true so outer EndGroup knows to rollback
// - When depth reaches 0: immediately applies Undo to all accumulated commands
// - When depth > 0: defers rollback to the outermost EndGroup/CancelGroup
```