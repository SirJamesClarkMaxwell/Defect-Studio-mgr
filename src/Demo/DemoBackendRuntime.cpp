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
#include "Core/Commands/CommandService.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Input.hpp"
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

	[[nodiscard]] static KeyModifiers currentKeyModifiers()
	{
		KeyModifiers modifiers = KeyModifiers::None;
		if (Input::IsKeyDown(KeyCode::LeftControl) || Input::IsKeyDown(KeyCode::RightControl))
			modifiers = modifiers | KeyModifiers::Ctrl;
		if (Input::IsKeyDown(KeyCode::LeftShift) || Input::IsKeyDown(KeyCode::RightShift))
			modifiers = modifiers | KeyModifiers::Shift;
		if (Input::IsKeyDown(KeyCode::LeftAlt) || Input::IsKeyDown(KeyCode::RightAlt))
			modifiers = modifiers | KeyModifiers::Alt;
		if (Input::IsKeyDown(KeyCode::LeftSuper) || Input::IsKeyDown(KeyCode::RightSuper))
			modifiers = modifiers | KeyModifiers::Super;
		return modifiers;
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
		if (!m_BackendCommandRegistry || !m_BackendCommandService || !m_BackendUndoStack || !m_BackendCommandPalette)
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
			auto result = m_BackendCommandService->Execute(id, std::move(context));
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

		renderUndoRedoExamples();

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
		ImGui::TextDisabled("Real keys: F6 increment, F7 undo, F8 redo, Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo.");

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
		m_BackendSliderValue = 50;
		m_PendingSliderValue = m_BackendSliderValue;
		m_BackendTokenCounter = 0;
		m_BackendFlag = false;
		m_PendingFlagValue = false;
		m_BackendSliderGroupActive = false;
		m_PendingToken.clear();
		m_BackendTokens.clear();
		m_BackendRuntimeLog.clear();

		m_BackendUndoStack = CreateRef<UndoStack>();
		m_BackendCommandRegistry = CreateRef<CommandRegistry>(CreateWeakRef(m_CapabilityService));
		m_BackendCommandRegistry->SetUndoStack(CreateWeakRef(m_BackendUndoStack));
		m_BackendCommandService = CreateUnique<CommandService>(
			m_BackendCommandRegistry,
			CreateWeakRef(m_BackendUndoStack),
			CreateWeakRef(m_CapabilityService));
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
			std::bind_front(&DemoBackendRuntime::createIncrementCommand, this));

		registerCommand(
			CommandMeta{
				kCommandDecrement,
				"Decrement demo value",
				"Demo",
				"Mutates demo state through CommandRegistry and pushes UndoStack.",
				{},
				CommandFlags::Undoable},
			std::bind_front(&DemoBackendRuntime::createDecrementCommand, this));

		registerCommand(
			CommandMeta{
				kCommandReset,
				"Reset demo value",
				"Demo",
				"Stores previous state and restores it on undo.",
				{},
				CommandFlags::Undoable},
			std::bind_front(&DemoBackendRuntime::createResetCommand, this));

		registerCommand(
			CommandMeta{
				kCommandUndo,
				"Undo demo command",
				"Edit",
				"Executes UndoStack::Undo through the command runtime shell.",
				{},
				CommandFlags::None},
			std::bind_front(&DemoBackendRuntime::createUndoCommand, this));

		registerCommand(
			CommandMeta{
				kCommandRedo,
				"Redo demo command",
				"Edit",
				"Executes UndoStack::Redo through the command runtime shell.",
				{},
				CommandFlags::None},
			std::bind_front(&DemoBackendRuntime::createRedoCommand, this));

		registerCommand(
			CommandMeta{
				kCommandNotify,
				"Send notification",
				"Diagnostics",
				"Capability-gated command that emits a notification.",
				{CapabilityID{"ui.notifications"}},
				CommandFlags::None},
			std::bind_front(&DemoBackendRuntime::createNotifyCommand, this));

		registerCommand(
			CommandMeta{
				kCommandMissingCapability,
				"Requires missing capability",
				"Diagnostics",
				"Shows capability gating and disabled palette entries.",
				{CapabilityID{"demo.missing"}},
				CommandFlags::None},
			std::bind_front(&DemoBackendRuntime::createMissingCapabilityCommand, this));

		registerCommand(
			CommandMeta{
				kCommandSetSlider,
				"Set grouped slider",
				"Demo",
				"Uses an explicit UndoStack group while the slider is dragged.",
				{},
				CommandFlags::Undoable},
			std::bind_front(&DemoBackendRuntime::createSetSliderCommand, this));

		registerCommand(
			CommandMeta{
				kCommandSetFlag,
				"Set demo flag",
				"Demo",
				"Undoable boolean state change.",
				{},
				CommandFlags::Undoable},
			std::bind_front(&DemoBackendRuntime::createSetFlagCommand, this));

		registerCommand(
			CommandMeta{
				kCommandAppendToken,
				"Append token",
				"Demo",
				"Undoable append operation for checking ordered undo.",
				{},
				CommandFlags::Undoable},
			std::bind_front(&DemoBackendRuntime::createAppendTokenCommand, this));

		registerCommand(
			CommandMeta{
				kCommandClearTokens,
				"Clear tokens",
				"Demo",
				"Undoable bulk state replacement.",
				{},
				CommandFlags::Undoable},
			std::bind_front(&DemoBackendRuntime::createClearTokensCommand, this));

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
		(void)m_BackendKeymapResolver->RegisterBinding(KeyBinding{
			"demo.undo.ctrl_z",
			KeyChord{KeyCode::Z, KeyModifiers::Ctrl},
			kCommandUndo,
			ContextExpr{"demo.backend"},
			KeymapLayer::WindowLocal,
			true});
		(void)m_BackendKeymapResolver->RegisterBinding(KeyBinding{
			"demo.redo.ctrl_y",
			KeyChord{KeyCode::Y, KeyModifiers::Ctrl},
			kCommandRedo,
			ContextExpr{"demo.backend"},
			KeymapLayer::WindowLocal,
			true});
		(void)m_BackendKeymapResolver->RegisterBinding(KeyBinding{
			"demo.redo.ctrl_shift_z",
			KeyChord{KeyCode::Z, KeyModifiers::Ctrl | KeyModifiers::Shift},
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

	Unique<ICommand> DemoBackendRuntime::createIncrementCommand(CommandContext &)
	{
		return CreateUnique<DemoValueDeltaCommand>(m_BackendDemoValue, 1, "Increment demo value");
	}

	Unique<ICommand> DemoBackendRuntime::createDecrementCommand(CommandContext &)
	{
		return CreateUnique<DemoValueDeltaCommand>(m_BackendDemoValue, -1, "Decrement demo value");
	}

	Unique<ICommand> DemoBackendRuntime::createResetCommand(CommandContext &)
	{
		return CreateUnique<DemoSetValueCommand>(m_BackendDemoValue, 0, "Reset demo value");
	}

	Unique<ICommand> DemoBackendRuntime::createUndoCommand(CommandContext &)
	{
		return CreateUnique<DemoCallbackCommand>("Undo demo command", [this](CommandContext &context) {
			return m_BackendUndoStack->Undo(std::move(context));
		});
	}

	Unique<ICommand> DemoBackendRuntime::createRedoCommand(CommandContext &)
	{
		return CreateUnique<DemoCallbackCommand>("Redo demo command", [this](CommandContext &context) {
			return m_BackendUndoStack->Redo(std::move(context));
		});
	}

	Unique<ICommand> DemoBackendRuntime::createNotifyCommand(CommandContext &)
	{
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
	}

	Unique<ICommand> DemoBackendRuntime::createMissingCapabilityCommand(CommandContext &)
	{
		return CreateUnique<DemoCallbackCommand>("Requires missing capability", [this](CommandContext &context) {
			(void)context;
			m_BackendDemoValue += 1000;
			return Result<void>{};
		});
	}

	Unique<ICommand> DemoBackendRuntime::createSetSliderCommand(CommandContext &)
	{
		return CreateUnique<DemoSetValueCommand>(m_BackendSliderValue, m_PendingSliderValue, "Set grouped slider");
	}

	Unique<ICommand> DemoBackendRuntime::createSetFlagCommand(CommandContext &)
	{
		return CreateUnique<DemoSetBoolCommand>(m_BackendFlag, m_PendingFlagValue, "Set demo flag");
	}

	Unique<ICommand> DemoBackendRuntime::createAppendTokenCommand(CommandContext &)
	{
		return CreateUnique<DemoAppendTokenCommand>(m_BackendTokens, m_PendingToken);
	}

	Unique<ICommand> DemoBackendRuntime::createClearTokensCommand(CommandContext &)
	{
		return CreateUnique<DemoClearTokensCommand>(m_BackendTokens);
	}

	bool DemoBackendRuntime::onBackendDemoKeyPressed(KeyPressedEvent &event)
	{
		return executeBackendDemoChord(
			KeyChord{static_cast<KeyCode>(event.GetKeyCode()), currentKeyModifiers()},
			"DemoLayer real key event");
	}

	void DemoBackendRuntime::renderUndoRedoExamples()
	{
		auto executeCommand = [this](const CommandID &id, const char *source) {
			CommandContext context(ContextID{"demo.backend"});
			context.SetSource(source);
			auto result = m_BackendCommandService->Execute(id, std::move(context));
			if (!result)
			{
				const StructuredError &error = result.Error();
				appendBackendRuntimeLog("error: " + error.userMessage + " [" + error.code + "]");
				requestNotification(ToNotification(error));
				return false;
			}
			return true;
		};

		ImGui::Spacing();
		ImGui::SeparatorText("Undo/redo examples");

		int sliderValue = m_BackendSliderValue;
		ImGui::SetNextItemWidth(280.0f);
		const bool sliderChanged = ImGui::SliderInt("Grouped slider", &sliderValue, 0, 100);
		if (ImGui::IsItemActivated() && !m_BackendSliderGroupActive)
		{
			auto result = m_BackendUndoStack->BeginGroup("Grouped slider edit");
			if (result)
				m_BackendSliderGroupActive = true;
			else
				appendBackendRuntimeLog("slider group begin failed: " + result.Error().code);
		}

		if (sliderChanged && sliderValue != m_BackendSliderValue)
		{
			m_PendingSliderValue = sliderValue;
			(void)executeCommand(kCommandSetSlider, "DemoLayer grouped slider");
		}

		if (m_BackendSliderGroupActive && ImGui::IsItemDeactivatedAfterEdit())
		{
			auto result = m_BackendUndoStack->EndGroup();
			if (!result)
				appendBackendRuntimeLog("slider group end failed: " + result.Error().code);
			m_BackendSliderGroupActive = false;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("value=%d", m_BackendSliderValue);

		const char *flagLabel = m_BackendFlag ? "Set flag off" : "Set flag on";
		if (ImGui::Button(flagLabel))
		{
			m_PendingFlagValue = !m_BackendFlag;
			(void)executeCommand(kCommandSetFlag, "DemoLayer flag button");
		}
		ImGui::SameLine();
		ImGui::Text("Flag: %s", m_BackendFlag ? "true" : "false");

		if (ImGui::Button("Append token"))
		{
			++m_BackendTokenCounter;
			m_PendingToken = "token-" + std::to_string(m_BackendTokenCounter);
			(void)executeCommand(kCommandAppendToken, "DemoLayer token button");
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(m_BackendTokens.empty());
		if (ImGui::Button("Clear tokens"))
			(void)executeCommand(kCommandClearTokens, "DemoLayer clear tokens button");
		ImGui::EndDisabled();

		if (m_BackendTokens.empty())
		{
			ImGui::TextDisabled("Tokens: <empty>");
		}
		else
		{
			ImGui::TextUnformatted("Tokens:");
			ImGui::SameLine();
			for (std::size_t index = 0; index < m_BackendTokens.size(); ++index)
			{
				if (index > 0)
					ImGui::SameLine();
				ImGui::TextUnformatted(m_BackendTokens[index].c_str());
			}
		}
	}

	bool DemoBackendRuntime::executeBackendDemoChord(const KeyChord &chord, const char *source)
	{
		if (!m_BackendCommandService || !m_BackendKeymapResolver || !m_BackendContextManager)
			return false;

		KeyInputProcessor processor(*m_BackendKeymapResolver, *m_BackendContextManager);
		auto result = processor.HandleKeyPressed(chord);
		if (!result)
		{
			const StructuredError &error = result.Error();
			appendBackendRuntimeLog("hotkey error: " + error.userMessage + " [" + error.code + "]");
			requestNotification(ToNotification(error));
			return false;
		}

		if (result->handled && result->commandId)
		{
			CommandContext context(ContextID{"demo.backend"});
			context.SetSource(source == nullptr ? "DemoLayer hotkey" : source);
			auto commandResult = m_BackendCommandService->Execute(*result->commandId, std::move(context));
			if (!commandResult)
			{
				const StructuredError &error = commandResult.Error();
				appendBackendRuntimeLog("hotkey command error: " + error.userMessage + " [" + error.code + "]");
				requestNotification(ToNotification(error));
				return false;
			}
			appendBackendRuntimeLog("hotkey " + ToString(chord) + " -> " + result->commandId->value);
		}
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
