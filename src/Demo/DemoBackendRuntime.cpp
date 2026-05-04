#include "Core/dspch.hpp"

#include <cstddef>
#include <functional>
#include <imgui.h>
#include <memory>
#include <string>
#include <utility>

#include "Core/Assets/AssetManager.hpp"
#include "Core/Commands/CommandPalette.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Demo/DemoBackendRuntime.hpp"
#include "Demo/DemoCommands.hpp"
#include "Events/NotificationEvents.hpp"

namespace DefectStudio::Demo
{
	[[nodiscard]] static const char *executionStateName(CommandExecutionState state)
	{
		switch (state)
		{
		case CommandExecutionState::Started:
			return "started";
		case CommandExecutionState::Succeeded:
			return "succeeded";
		case CommandExecutionState::Failed:
			return "failed";
		case CommandExecutionState::Rejected:
			return "rejected";
		}
		return "unknown";
	}

	DemoBackendRuntime::DemoBackendRuntime(Ref<CapabilityService> capabilityService, Ref<EventBus> eventBus, WeakRef<AssetManager> assetManager)
		: m_CapabilityService(std::move(capabilityService)),
		  m_EventBus(std::move(eventBus)),
		  m_AssetManager(std::move(assetManager))
	{
		setupBackendRuntimeDemo();
	}

	DemoBackendRuntime::~DemoBackendRuntime() = default;

	void DemoBackendRuntime::OnEvent(Event &event)
	{
		if (!event.handled && m_BackendHotkeysEnabled)
		{
			EventDispatcher dispatcher(event);
			dispatcher.Dispatch<KeyPressedEvent>(std::bind(&DemoBackendRuntime::onBackendDemoKeyPressed, this, std::placeholders::_1));
		}
	}

	void DemoBackendRuntime::Render()
	{
		if (!m_BackendCommandRegistry || !m_BackendUndoStack || !m_BackendCommandPalette)
		{
			ImGui::TextUnformatted("Backend runtime demo is not initialized.");
			return;
		}

		ImGui::TextUnformatted("CommandRegistry + UndoStack + KeyBindings + CommandPalette + AssetManager");
		ImGui::Spacing();

		ImGui::SeparatorText("Command runtime");
		ImGui::Text("Demo value: %d", m_BackendDemoValue);
		ImGui::Text("Undo depth: %zu | Redo depth: %zu | Clean: %s",
		            m_BackendUndoStack->GetUndoDepth(),
		            m_BackendUndoStack->GetRedoDepth(),
		            m_BackendUndoStack->IsClean() ? "yes" : "no");
		ImGui::Text("Next undo: %s", m_BackendUndoStack->GetUndoDescription().c_str());
		ImGui::Text("Next redo: %s", m_BackendUndoStack->GetRedoDescription().c_str());

		auto executeCommand = [this](const CommandID &id, const char *source) {
			CommandContext context(ContextID{"demo.backend"});
			context.SetSource(source);
			auto result = m_BackendCommandRegistry->Execute(id, std::move(context));
			if (!result)
			{
				const StructuredError &error = result.Error();
				appendBackendRuntimeLog("error: " + error.userMessage + " [" + error.code + "]");
				requestNotification(ToNotification(error));
			}
		};

		if (ImGui::Button("+1 command"))
			executeCommand(kCommandIncrement, "DemoLayer button");
		ImGui::SameLine();
		if (ImGui::Button("-1 command"))
			executeCommand(kCommandDecrement, "DemoLayer button");
		ImGui::SameLine();
		if (ImGui::Button("Reset command"))
			executeCommand(kCommandReset, "DemoLayer button");

		if (ImGui::Button("Grouped +3 transaction"))
		{
			auto scope = m_BackendUndoStack->ScopedGroup("Grouped +3 demo transaction");
			executeCommand(kCommandIncrement, "DemoLayer grouped transaction");
			executeCommand(kCommandIncrement, "DemoLayer grouped transaction");
			executeCommand(kCommandIncrement, "DemoLayer grouped transaction");
			(void)scope.Commit();
		}
		ImGui::SameLine();
		if (ImGui::Button("Mark clean"))
		{
			m_BackendUndoStack->MarkClean();
			appendBackendRuntimeLog("clean mark set at current undo index");
		}

		ImGui::BeginDisabled(!m_BackendUndoStack->CanUndo());
		if (ImGui::Button("Undo"))
			executeCommand(kCommandUndo, "DemoLayer button");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!m_BackendUndoStack->CanRedo());
		if (ImGui::Button("Redo"))
			executeCommand(kCommandRedo, "DemoLayer button");
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Capability failure"))
			executeCommand(kCommandMissingCapability, "DemoLayer button");

		ImGui::Spacing();
		ImGui::SeparatorText("Hotkeys");
		ImGui::Checkbox("Enable backend demo hotkeys", &m_BackendHotkeysEnabled);
		const bool contextActive = m_BackendContextManager->IsActive("demo.backend");
		ImGui::Text("Context 'demo.backend': %s", contextActive ? "active" : "inactive");
		if (ImGui::Button("Toggle context"))
			m_BackendContextManager->SetActive("demo.backend", !contextActive);
		ImGui::SameLine();
		if (ImGui::Button("Simulate F6"))
			(void)executeBackendDemoChord(KeyChord{KeyCode::F6, KeyModifiers::None}, "DemoLayer simulated hotkey");
		ImGui::SameLine();
		if (ImGui::Button("Simulate F7"))
			(void)executeBackendDemoChord(KeyChord{KeyCode::F7, KeyModifiers::None}, "DemoLayer simulated hotkey");
		ImGui::SameLine();
		if (ImGui::Button("Simulate F8"))
			(void)executeBackendDemoChord(KeyChord{KeyCode::F8, KeyModifiers::None}, "DemoLayer simulated hotkey");
		ImGui::TextDisabled("Real keys: F6 increment, F7 undo, F8 redo. They only resolve when demo.backend context is active.");

		ImGui::Spacing();
		ImGui::SeparatorText("Command palette backend");
		ImGui::InputText("Search", m_CommandPaletteSearch.data(), m_CommandPaletteSearch.size());
		const auto items = m_BackendCommandPalette->Search(m_CommandPaletteSearch.data());
		if (ImGui::BeginTable("##backend_command_palette_table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Command");
			ImGui::TableSetupColumn("Category");
			ImGui::TableSetupColumn("Shortcut");
			ImGui::TableSetupColumn("Enabled");
			ImGui::TableSetupColumn("Action");
			ImGui::TableHeadersRow();
			for (const auto &item : items)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(item.name.c_str());
				if (!item.description.empty())
					ImGui::TextDisabled("%s", item.description.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(item.category.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(item.shortcut.empty() ? "-" : item.shortcut.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(item.enabled ? "yes" : "no");
				ImGui::TableNextColumn();
				ImGui::BeginDisabled(!item.enabled);
				const std::string buttonId = "Run##" + item.id.value;
				if (ImGui::SmallButton(buttonId.c_str()))
				{
					CommandContext context(ContextID{"demo.backend"});
					context.SetSource("DemoLayer command palette");
					auto result = m_BackendCommandPalette->Execute(item.id, std::move(context));
					if (!result)
					{
						const StructuredError &error = result.Error();
						appendBackendRuntimeLog("palette error: " + error.userMessage + " [" + error.code + "]");
						requestNotification(ToNotification(error));
					}
				}
				ImGui::EndDisabled();
			}
			ImGui::EndTable();
		}

		const auto recent = m_BackendCommandPalette->GetRecentCommands();
		ImGui::Text("Recent commands: %zu", recent.size());
		for (const CommandID &id : recent)
			ImGui::BulletText("%s", id.value.c_str());

		ImGui::Spacing();
		ImGui::SeparatorText("Asset manager");
		auto assetManager = m_AssetManager.lock();
		if (assetManager == nullptr)
		{
			ImGui::TextDisabled("AssetManager unavailable.");
		}
		else
		{
			ImGui::Text("Default root: %s", assetManager->GetDefaultRoot().String().c_str());
			ImGui::Text("User override root: %s", assetManager->GetUserOverrideRoot().String().c_str());
			ImGui::Text("Registered descriptors: %zu", assetManager->ListDescriptors().size());
			if (ImGui::Button("Resolve icon.ico"))
			{
				auto result = assetManager->ResolvePath("icon.ico");
				appendBackendRuntimeLog(result ? "asset icon.ico -> " + result->resolvedPath.String() : "asset error: " + result.Error().userMessage);
			}
			ImGui::SameLine();
			if (ImGui::Button("Resolve fonts/segoeui.ttf"))
			{
				auto result = assetManager->ResolvePath("fonts/segoeui.ttf");
				appendBackendRuntimeLog(result ? "asset fonts/segoeui.ttf -> " + result->resolvedPath.String() : "asset error: " + result.Error().userMessage);
			}
			ImGui::SameLine();
			if (ImGui::Button("Validate assets"))
			{
				const AssetValidationReport report = assetManager->ValidateRegisteredAssets();
				appendBackendRuntimeLog(
					"asset validation: resolved=" + std::to_string(report.resolvedAssets.size())
					+ " issues=" + std::to_string(report.issues.size())
					+ " critical=" + (report.HasCriticalFailures() ? "yes" : "no"));
			}
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Execution log");
		if (ImGui::BeginChild("##backend_runtime_log", ImVec2(0.0f, 180.0f), true))
		{
			for (auto it = m_BackendRuntimeLog.rbegin(); it != m_BackendRuntimeLog.rend(); ++it)
				ImGui::BulletText("%s", it->c_str());
		}
		ImGui::EndChild();
	}

	void DemoBackendRuntime::setupBackendRuntimeDemo()
	{
		m_BackendDemoValue = 0;
		m_BackendRuntimeLog.clear();

		m_BackendUndoStack = CreateRef<UndoStack>();
		m_BackendCommandRegistry = CreateUnique<CommandRegistry>(CreateWeakRef(m_CapabilityService));
		m_BackendCommandRegistry->SetUndoStack(CreateWeakRef(m_BackendUndoStack));
		m_BackendKeymapResolver = CreateUnique<KeymapResolver>();
		m_BackendContextManager = CreateUnique<ContextManager>();
		m_BackendCommandPalette = CreateUnique<CommandPaletteIndex>(*m_BackendCommandRegistry);
		m_BackendCommandPalette->SetKeymapResolver(m_BackendKeymapResolver.get(), m_BackendContextManager.get());

		auto registerCommand = [this](CommandMeta meta, CommandFactory factory) {
			auto result = m_BackendCommandRegistry->Register(std::move(meta), std::move(factory));
			if (!result)
				appendBackendRuntimeLog("register failed: " + result.Error().technicalDetails);
		};

		registerCommand(
			CommandMeta{
				kCommandIncrement,
				"Increment demo value",
				"Demo",
				"Mutates demo state through CommandRegistry and pushes UndoStack.",
				{},
				CommandFlags::Undoable},
			[this](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<DemoValueDeltaCommand>(m_BackendDemoValue, 1, "Increment demo value");
			});

		registerCommand(
			CommandMeta{
				kCommandDecrement,
				"Decrement demo value",
				"Demo",
				"Mutates demo state through CommandRegistry and pushes UndoStack.",
				{},
				CommandFlags::Undoable},
			[this](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<DemoValueDeltaCommand>(m_BackendDemoValue, -1, "Decrement demo value");
			});

		registerCommand(
			CommandMeta{
				kCommandReset,
				"Reset demo value",
				"Demo",
				"Stores previous state and restores it on undo.",
				{},
				CommandFlags::Undoable},
			[this](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<DemoSetValueCommand>(m_BackendDemoValue, 0);
			});

		registerCommand(
			CommandMeta{
				kCommandUndo,
				"Undo demo command",
				"Edit",
				"Executes UndoStack::Undo through the command runtime shell.",
				{},
				CommandFlags::None},
			[this](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<DemoCallbackCommand>("Undo demo command", [this](CommandContext &context) {
					return m_BackendUndoStack->Undo(std::move(context));
				});
			});

		registerCommand(
			CommandMeta{
				kCommandRedo,
				"Redo demo command",
				"Edit",
				"Executes UndoStack::Redo through the command runtime shell.",
				{},
				CommandFlags::None},
			[this](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<DemoCallbackCommand>("Redo demo command", [this](CommandContext &context) {
					return m_BackendUndoStack->Redo(std::move(context));
				});
			});

		registerCommand(
			CommandMeta{
				kCommandNotify,
				"Send notification",
				"Diagnostics",
				"Capability-gated command that emits a notification.",
				{CapabilityID{"ui.notifications"}},
				CommandFlags::None},
			[this](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<DemoCallbackCommand>("Send notification", [this](CommandContext &context) {
					(void)context;
					requestNotification(Notification{
						NotificationSeverity::Info,
						NotificationCategory::UI,
						"Command runtime demo",
						"Notification emitted from demo.backend.notify",
						"DemoBackendRuntime",
						Time::Now(),
						4000,
						false});
					appendBackendRuntimeLog("notification command emitted ui.notifications");
					return Result<void>{};
				});
			});

		registerCommand(
			CommandMeta{
				kCommandMissingCapability,
				"Requires missing capability",
				"Diagnostics",
				"Shows capability gating and disabled palette entries.",
				{CapabilityID{"demo.missing"}},
				CommandFlags::None},
			[this](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<DemoCallbackCommand>("Requires missing capability", [this](CommandContext &context) {
					(void)context;
					m_BackendDemoValue += 1000;
					return Result<void>{};
				});
			});

		(void)m_BackendKeymapResolver->RegisterBinding(KeyBinding{
			"demo.increment.f6",
			KeyChord{KeyCode::F6, KeyModifiers::None},
			kCommandIncrement,
			ContextExpr{"demo.backend"},
			KeymapLayer::WindowLocal,
			true});
		(void)m_BackendKeymapResolver->RegisterBinding(KeyBinding{
			"demo.undo.f7",
			KeyChord{KeyCode::F7, KeyModifiers::None},
			kCommandUndo,
			ContextExpr{"demo.backend"},
			KeymapLayer::WindowLocal,
			true});
		(void)m_BackendKeymapResolver->RegisterBinding(KeyBinding{
			"demo.redo.f8",
			KeyChord{KeyCode::F8, KeyModifiers::None},
			kCommandRedo,
			ContextExpr{"demo.backend"},
			KeymapLayer::WindowLocal,
			true});
		m_BackendContextManager->SetActive("demo.backend", true);

		(void)m_BackendCommandRegistry->AddObserver([this](const CommandExecutionEvent &event) {
			std::string message = event.name + " -> " + executionStateName(event.state);
			if (event.error)
				message += " (" + event.error->code + ")";
			appendBackendRuntimeLog(std::move(message));
		});

		appendBackendRuntimeLog("backend runtime demo initialized");
	}

	bool DemoBackendRuntime::onBackendDemoKeyPressed(KeyPressedEvent &event)
	{
		return executeBackendDemoChord(
			KeyChord{static_cast<KeyCode>(event.GetKeyCode()), KeyModifiers::None},
			"DemoLayer real key event");
	}

	bool DemoBackendRuntime::executeBackendDemoChord(const KeyChord &chord, const char *source)
	{
		if (!m_BackendCommandRegistry || !m_BackendKeymapResolver || !m_BackendContextManager)
			return false;

		KeyInputProcessor processor(*m_BackendCommandRegistry, *m_BackendKeymapResolver, *m_BackendContextManager);
		CommandContext context(ContextID{"demo.backend"});
		context.SetSource(source == nullptr ? "DemoLayer hotkey" : source);
		auto result = processor.HandleKeyPressed(chord, std::move(context));
		if (!result)
		{
			const StructuredError &error = result.Error();
			appendBackendRuntimeLog("hotkey error: " + error.userMessage + " [" + error.code + "]");
			requestNotification(ToNotification(error));
			return false;
		}

		if (result->handled && result->commandId)
			appendBackendRuntimeLog("hotkey " + ToString(chord) + " -> " + result->commandId->value);
		return result->handled;
	}

	void DemoBackendRuntime::appendBackendRuntimeLog(std::string message)
	{
		m_BackendRuntimeLog.push_back(std::move(message));
		if (m_BackendRuntimeLog.size() > 24)
			m_BackendRuntimeLog.erase(
				m_BackendRuntimeLog.begin(),
				m_BackendRuntimeLog.begin() + static_cast<std::ptrdiff_t>(m_BackendRuntimeLog.size() - 24));
	}

	void DemoBackendRuntime::requestNotification(Notification notification)
	{
		if (m_EventBus)
			m_EventBus->Queue(NotificationRequestedEvent{std::move(notification)});
	}
} // namespace DefectStudio::Demo
