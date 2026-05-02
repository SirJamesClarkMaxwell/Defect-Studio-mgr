#include "Core/dspch.hpp"

#include <functional>
#include <imgui.h>

#include "App/Application.hpp"
#include "Core/Assets/AssetManager.hpp"
#include "Core/Commands/CommandPalette.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/Input/KeyBinding.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Demo/DemoLayer.hpp"
#include "Demo/EventDispatcherDemo.hpp"
#include "Demo/EventBusDemo.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127 4267 4458)
#endif
#include "ImGuiNotify.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "Demo/JobSystemDemo.hpp"

namespace DefectStudio::Demo
{
	namespace
	{
		const CommandID kCommandIncrement{"demo.backend.increment"};
		const CommandID kCommandDecrement{"demo.backend.decrement"};
		const CommandID kCommandReset{"demo.backend.reset"};
		const CommandID kCommandUndo{"demo.backend.undo"};
		const CommandID kCommandRedo{"demo.backend.redo"};
		const CommandID kCommandNotify{"demo.backend.notify"};
		const CommandID kCommandMissingCapability{"demo.backend.requires_missing_capability"};

		class DemoValueDeltaCommand final : public ICommand
		{
		public:
			DemoValueDeltaCommand(int &value, int delta, std::string description)
				: m_Value(&value), m_Delta(delta), m_Description(std::move(description))
			{
			}

			Result<void> Execute(CommandContext &context) override
			{
				(void)context;
				*m_Value += m_Delta;
				return {};
			}

			Result<void> Undo(CommandContext &context) override
			{
				(void)context;
				*m_Value -= m_Delta;
				return {};
			}

			std::string Description() const override
			{
				return m_Description + " (" + std::to_string(m_Delta) + ")";
			}

			bool IsUndoable() const noexcept override
			{
				return true;
			}

			bool CanMerge(const ICommand &next) const noexcept override
			{
				const auto *nextDelta = dynamic_cast<const DemoValueDeltaCommand *>(&next);
				return nextDelta != nullptr && nextDelta->m_Value == m_Value && nextDelta->m_Description == m_Description;
			}

			Result<void> Merge(std::unique_ptr<ICommand> next) override
			{
				auto *nextDelta = dynamic_cast<DemoValueDeltaCommand *>(next.get());
				if (nextDelta == nullptr)
				{
					return StructuredError{
						ErrorCategory::Validation,
						Severity::Error,
						"Demo command merge failed.",
						"UndoStack attempted to merge incompatible demo commands.",
						"Keep mergeable command families type-compatible.",
						"DemoLayer",
						"demo.command.merge_mismatch"};
				}

				m_Delta += nextDelta->m_Delta;
				return {};
			}

		private:
			int *m_Value = nullptr;
			int m_Delta = 0;
			std::string m_Description;
		};

		class DemoSetValueCommand final : public ICommand
		{
		public:
			DemoSetValueCommand(int &value, int newValue)
				: m_Value(&value), m_NewValue(newValue)
			{
			}

			Result<void> Execute(CommandContext &context) override
			{
				(void)context;
				m_OldValue = *m_Value;
				*m_Value = m_NewValue;
				return {};
			}

			Result<void> Undo(CommandContext &context) override
			{
				(void)context;
				*m_Value = m_OldValue;
				return {};
			}

			std::string Description() const override
			{
				return "Reset demo value";
			}

			bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			int *m_Value = nullptr;
			int m_NewValue = 0;
			int m_OldValue = 0;
		};

		class DemoCallbackCommand final : public ICommand
		{
		public:
			DemoCallbackCommand(std::string description, std::function<Result<void>(CommandContext &)> callback)
				: m_Description(std::move(description)), m_Callback(std::move(callback))
			{
			}

			Result<void> Execute(CommandContext &context) override
			{
				return m_Callback(context);
			}

			std::string Description() const override
			{
				return m_Description;
			}

		private:
			std::string m_Description;
			std::function<Result<void>(CommandContext &)> m_Callback;
		};

		[[nodiscard]] const char *executionStateName(CommandExecutionState state)
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
	}

	DemoLayer::DemoLayer() : Layer("DemoLayer")
	{
	}

	DemoLayer::~DemoLayer() = default;

	void DemoLayer::OnAttach()
	{
		DS_LOG_INFO("DemoLayer attached");
		m_EventDispatcherDemo = CreateUnique<EventDispatcherDemo>();
		m_DemoEventBus = CreateRef<EventBus>();
		m_EventBusDemo = CreateUnique<EventBusDemo>(m_DemoEventBus);
		m_JobSystemDemo = CreateUnique<JobSystemDemo>();
		setupBackendRuntimeDemo();
	}

	void DemoLayer::OnDetach()
	{
		DS_LOG_INFO("DemoLayer detached");
		m_CommandPalette.reset();
		m_ContextManager.reset();
		m_KeymapResolver.reset();
		m_CommandRegistry.reset();
		m_UndoStack.reset();
		m_JobSystemDemo.reset();
		m_EventDispatcherDemo.reset();
		m_EventBusDemo.reset();
		m_DemoEventBus.reset();
	}

	void DemoLayer::OnEvent(Event &event)
	{
		if (m_EventDispatcherDemo)
			m_EventDispatcherDemo->OnEvent(event);

		if (!event.handled && m_BackendHotkeysEnabled)
		{
			EventDispatcher dispatcher(event);
			dispatcher.Dispatch<KeyPressedEvent>(std::bind(&DemoLayer::onBackendDemoKeyPressed, this, std::placeholders::_1));
		}
	}

	void DemoLayer::OnImGuiRender()
	{
		if (!ImGui::Begin("Demos"))
		{
			ImGui::End();
			return;
		}

		if (ImGui::CollapsingHeader("Event Dispatcher", ImGuiTreeNodeFlags_DefaultOpen) && m_EventDispatcherDemo)
			m_EventDispatcherDemo->Render();

		if (ImGui::CollapsingHeader("Event Bus", ImGuiTreeNodeFlags_DefaultOpen) && m_EventBusDemo)
			m_EventBusDemo->Render();

		if (ImGui::CollapsingHeader("Job System", ImGuiTreeNodeFlags_DefaultOpen) && m_JobSystemDemo)
			m_JobSystemDemo->Render();
		
			if (ImGui::CollapsingHeader("Notifications", ImGuiTreeNodeFlags_DefaultOpen))
				renderNotificationDemo();
			
		if (ImGui::CollapsingHeader("Backend Runtime", ImGuiTreeNodeFlags_DefaultOpen))
			renderBackendRuntimeDemo();


		ImGui::End();
		ImGui::RenderNotifications();
	}

	namespace
	{
		ImGuiToastType toToastType(NotificationSeverity severity)
		{
			switch (severity)
			{
			case NotificationSeverity::Info:
				return ImGuiToastType::Info;
			case NotificationSeverity::Warn:
				return ImGuiToastType::Warning;
			case NotificationSeverity::Error:
			case NotificationSeverity::Critical:
				return ImGuiToastType::Error;
			}

			return ImGuiToastType::Info;
		}

		ImVec4 severityColor(NotificationSeverity severity)
		{
			switch (severity)
			{
			case NotificationSeverity::Info:
				return ImVec4(0.35f, 0.72f, 1.00f, 1.00f);
			case NotificationSeverity::Warn:
				return ImVec4(1.00f, 0.76f, 0.28f, 1.00f);
			case NotificationSeverity::Error:
				return ImVec4(1.00f, 0.38f, 0.38f, 1.00f);
			case NotificationSeverity::Critical:
				return ImVec4(1.00f, 0.18f, 0.18f, 1.00f);
			}

			return ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		}
	}

	void DemoLayer::setupBackendRuntimeDemo()
	{
		m_BackendDemoValue = 0;
		m_BackendRuntimeLog.clear();

		m_UndoStack = CreateUnique<UndoStack>();
		m_CommandRegistry = CreateUnique<CommandRegistry>(&Application::Get().GetCapabilityService());
		m_CommandRegistry->SetUndoStack(m_UndoStack.get());
		m_KeymapResolver = CreateUnique<KeymapResolver>();
		m_ContextManager = CreateUnique<ContextManager>();
		m_CommandPalette = CreateUnique<CommandPaletteIndex>(*m_CommandRegistry);
		m_CommandPalette->SetKeymapResolver(m_KeymapResolver.get(), m_ContextManager.get());

		auto registerCommand = [this](CommandMeta meta, CommandFactory factory) {
			auto result = m_CommandRegistry->Register(std::move(meta), std::move(factory));
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
			[this](CommandContext &) {
				return std::make_unique<DemoValueDeltaCommand>(m_BackendDemoValue, 1, "Increment demo value");
			});

		registerCommand(
			CommandMeta{
				kCommandDecrement,
				"Decrement demo value",
				"Demo",
				"Mutates demo state through CommandRegistry and pushes UndoStack.",
				{},
				CommandFlags::Undoable},
			[this](CommandContext &) {
				return std::make_unique<DemoValueDeltaCommand>(m_BackendDemoValue, -1, "Decrement demo value");
			});

		registerCommand(
			CommandMeta{
				kCommandReset,
				"Reset demo value",
				"Demo",
				"Stores previous state and restores it on undo.",
				{},
				CommandFlags::Undoable},
			[this](CommandContext &) {
				return std::make_unique<DemoSetValueCommand>(m_BackendDemoValue, 0);
			});

		registerCommand(
			CommandMeta{
				kCommandUndo,
				"Undo demo command",
				"Edit",
				"Executes UndoStack::Undo through the command runtime shell.",
				{},
				CommandFlags::None},
			[this](CommandContext &) {
				return std::make_unique<DemoCallbackCommand>("Undo demo command", [this](CommandContext &context) {
					return m_UndoStack->Undo(std::move(context));
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
			[this](CommandContext &) {
				return std::make_unique<DemoCallbackCommand>("Redo demo command", [this](CommandContext &context) {
					return m_UndoStack->Redo(std::move(context));
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
			[this](CommandContext &) {
				return std::make_unique<DemoCallbackCommand>("Send notification", [this](CommandContext &context) {
					(void)context;
					Application::Get().GetNotifier().Info(
						"Command runtime demo",
						"Notification emitted from demo.backend.notify",
						NotificationCategory::UI);
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
			[this](CommandContext &) {
				return std::make_unique<DemoCallbackCommand>("Requires missing capability", [this](CommandContext &context) {
					(void)context;
					m_BackendDemoValue += 1000;
					return Result<void>{};
				});
			});

		(void)m_KeymapResolver->RegisterBinding(KeyBinding{
			"demo.increment.f6",
			KeyChord{KeyCode::F6, KeyModifiers::None},
			kCommandIncrement,
			ContextExpr{"demo.backend"},
			KeymapLayer::WindowLocal,
			true});
		(void)m_KeymapResolver->RegisterBinding(KeyBinding{
			"demo.undo.f7",
			KeyChord{KeyCode::F7, KeyModifiers::None},
			kCommandUndo,
			ContextExpr{"demo.backend"},
			KeymapLayer::WindowLocal,
			true});
		(void)m_KeymapResolver->RegisterBinding(KeyBinding{
			"demo.redo.f8",
			KeyChord{KeyCode::F8, KeyModifiers::None},
			kCommandRedo,
			ContextExpr{"demo.backend"},
			KeymapLayer::WindowLocal,
			true});
		m_ContextManager->SetActive("demo.backend", true);

		(void)m_CommandRegistry->AddObserver([this](const CommandExecutionEvent &event) {
			std::string message = event.name + " -> " + executionStateName(event.state);
			if (event.error)
				message += " (" + event.error->code + ")";
			appendBackendRuntimeLog(std::move(message));
		});

		appendBackendRuntimeLog("backend runtime demo initialized");
	}

	void DemoLayer::renderBackendRuntimeDemo()
	{
		if (!m_CommandRegistry || !m_UndoStack || !m_CommandPalette)
		{
			ImGui::TextUnformatted("Backend runtime demo is not initialized.");
			return;
		}

		ImGui::TextUnformatted("CommandRegistry + UndoStack + KeyBindings + CommandPalette + AssetManager");
		ImGui::Spacing();

		ImGui::SeparatorText("Command runtime");
		ImGui::Text("Demo value: %d", m_BackendDemoValue);
		ImGui::Text("Undo depth: %zu | Redo depth: %zu | Clean: %s",
		            m_UndoStack->GetUndoDepth(),
		            m_UndoStack->GetRedoDepth(),
		            m_UndoStack->IsClean() ? "yes" : "no");
		ImGui::Text("Next undo: %s", m_UndoStack->GetUndoDescription().c_str());
		ImGui::Text("Next redo: %s", m_UndoStack->GetRedoDescription().c_str());

		auto executeCommand = [this](const CommandID &id, const char *source) {
			CommandContext context(ContextID{"demo.backend"});
			context.SetSource(source);
			auto result = m_CommandRegistry->Execute(id, std::move(context));
			if (!result)
			{
				const StructuredError &error = result.Error();
				appendBackendRuntimeLog("error: " + error.userMessage + " [" + error.code + "]");
				Application::Get().GetNotifier().Notify(ToNotification(error));
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
			auto scope = m_UndoStack->ScopedGroup("Grouped +3 demo transaction");
			executeCommand(kCommandIncrement, "DemoLayer grouped transaction");
			executeCommand(kCommandIncrement, "DemoLayer grouped transaction");
			executeCommand(kCommandIncrement, "DemoLayer grouped transaction");
			(void)scope.Commit();
		}
		ImGui::SameLine();
		if (ImGui::Button("Mark clean"))
		{
			m_UndoStack->MarkClean();
			appendBackendRuntimeLog("clean mark set at current undo index");
		}

		ImGui::BeginDisabled(!m_UndoStack->CanUndo());
		if (ImGui::Button("Undo"))
			executeCommand(kCommandUndo, "DemoLayer button");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!m_UndoStack->CanRedo());
		if (ImGui::Button("Redo"))
			executeCommand(kCommandRedo, "DemoLayer button");
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Capability failure"))
			executeCommand(kCommandMissingCapability, "DemoLayer button");

		ImGui::Spacing();
		ImGui::SeparatorText("Hotkeys");
		ImGui::Checkbox("Enable backend demo hotkeys", &m_BackendHotkeysEnabled);
		const bool contextActive = m_ContextManager->IsActive("demo.backend");
		ImGui::Text("Context 'demo.backend': %s", contextActive ? "active" : "inactive");
		if (ImGui::Button("Toggle context"))
			m_ContextManager->SetActive("demo.backend", !contextActive);
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
		const auto items = m_CommandPalette->Search(m_CommandPaletteSearch.data());
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
					auto result = m_CommandPalette->Execute(item.id, std::move(context));
					if (!result)
					{
						const StructuredError &error = result.Error();
						appendBackendRuntimeLog("palette error: " + error.userMessage + " [" + error.code + "]");
						Application::Get().GetNotifier().Notify(ToNotification(error));
					}
				}
				ImGui::EndDisabled();
			}
			ImGui::EndTable();
		}

		const auto recent = m_CommandPalette->GetRecentCommands();
		ImGui::Text("Recent commands: %zu", recent.size());
		for (const CommandID &id : recent)
			ImGui::BulletText("%s", id.value.c_str());

		ImGui::Spacing();
		ImGui::SeparatorText("Asset manager");
		auto &assetManager = Application::Get().GetAssetManager();
		ImGui::Text("Default root: %s", assetManager.GetDefaultRoot().String().c_str());
		ImGui::Text("User override root: %s", assetManager.GetUserOverrideRoot().String().c_str());
		ImGui::Text("Registered descriptors: %zu", assetManager.ListDescriptors().size());
		if (ImGui::Button("Resolve icon.ico"))
		{
			auto result = assetManager.ResolvePath("icon.ico");
			appendBackendRuntimeLog(result ? "asset icon.ico -> " + result->resolvedPath.String() : "asset error: " + result.Error().userMessage);
		}
		ImGui::SameLine();
		if (ImGui::Button("Resolve fonts/segoeui.ttf"))
		{
			auto result = assetManager.ResolvePath("fonts/segoeui.ttf");
			appendBackendRuntimeLog(result ? "asset fonts/segoeui.ttf -> " + result->resolvedPath.String() : "asset error: " + result.Error().userMessage);
		}
		ImGui::SameLine();
		if (ImGui::Button("Validate assets"))
		{
			const AssetValidationReport report = assetManager.ValidateRegisteredAssets();
			appendBackendRuntimeLog(
				"asset validation: resolved=" + std::to_string(report.resolvedAssets.size())
				+ " issues=" + std::to_string(report.issues.size())
				+ " critical=" + (report.HasCriticalFailures() ? "yes" : "no"));
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

	bool DemoLayer::onBackendDemoKeyPressed(KeyPressedEvent &event)
	{
		return executeBackendDemoChord(
			KeyChord{static_cast<KeyCode>(event.GetKeyCode()), KeyModifiers::None},
			"DemoLayer real key event");
	}

	bool DemoLayer::executeBackendDemoChord(const KeyChord &chord, const char *source)
	{
		if (!m_CommandRegistry || !m_KeymapResolver || !m_ContextManager)
			return false;

		KeyInputProcessor processor(*m_CommandRegistry, *m_KeymapResolver, *m_ContextManager);
		CommandContext context(ContextID{"demo.backend"});
		context.SetSource(source == nullptr ? "DemoLayer hotkey" : source);
		auto result = processor.HandleKeyPressed(chord, std::move(context));
		if (!result)
		{
			const StructuredError &error = result.Error();
			appendBackendRuntimeLog("hotkey error: " + error.userMessage + " [" + error.code + "]");
			Application::Get().GetNotifier().Notify(ToNotification(error));
			return false;
		}

		if (result->handled && result->commandId)
			appendBackendRuntimeLog("hotkey " + ToString(chord) + " -> " + result->commandId->value);
		return result->handled;
	}

	void DemoLayer::appendBackendRuntimeLog(std::string message)
	{
		m_BackendRuntimeLog.push_back(std::move(message));
		if (m_BackendRuntimeLog.size() > 24)
			m_BackendRuntimeLog.erase(m_BackendRuntimeLog.begin(), m_BackendRuntimeLog.begin() + static_cast<std::ptrdiff_t>(m_BackendRuntimeLog.size() - 24));
	}

	void DemoLayer::renderNotificationDemo()
	{
		auto &application = Application::Get();
		auto &notifier = application.GetNotifier();
		auto &capabilityRegistry = application.GetCapabilityRegistry();
		auto &capabilityService = application.GetCapabilityService();

		ImGui::TextUnformatted("Toast notifications and runtime diagnostics");
		ImGui::Spacing();

		auto emitNotification = [&](NotificationSeverity severity,
		                            const char *title,
		                            const char *message,
		                            NotificationCategory category = NotificationCategory::General) {
			std::string suffix = " #" + std::to_string(++m_NotificationDemoCounter);
			std::string resolvedTitle = std::string(title) + suffix;
			std::string resolvedMessage = std::string(message) + suffix;

			switch (severity)
			{
			case NotificationSeverity::Info:
				notifier.Info(resolvedTitle, resolvedMessage, category);
				break;
			case NotificationSeverity::Warn:
				notifier.Warning(resolvedTitle, resolvedMessage, category);
				break;
			case NotificationSeverity::Error:
				notifier.Error(resolvedTitle, resolvedMessage, category);
				break;
			case NotificationSeverity::Critical:
				notifier.Critical(resolvedTitle, resolvedMessage, category);
				break;
			}

			ImGui::InsertNotification({toToastType(severity), 3000, "%s", resolvedMessage.c_str()});
		};

		if (ImGui::Button("Info toast"))
			emitNotification(NotificationSeverity::Info, "Info", "ImGuiNotify toast from DemoLayer");
		ImGui::SameLine();
		if (ImGui::Button("Warning toast"))
			emitNotification(NotificationSeverity::Warn, "Warning", "Diagnostic warning surfaced in UI");
		ImGui::SameLine();
		if (ImGui::Button("Error toast"))
			emitNotification(NotificationSeverity::Error, "Error", "Blocking error path demonstrated");
		ImGui::SameLine();
		if (ImGui::Button("Critical toast"))
			emitNotification(NotificationSeverity::Critical, "Critical", "Critical diagnostic surfaced in UI");

		ImGui::Spacing();
		ImGui::SeparatorText("Runtime snapshot");
		ImGui::Text("Capability registry locked: %s", capabilityRegistry.IsLocked() ? "yes" : "no");
		ImGui::Text("Registered capabilities: %zu", capabilityRegistry.GetAll().size());
		ImGui::Text("Capability service reports 'ui.notifications': %s", capabilityService.IsAvailable("ui.notifications") ? "yes" : "no");

		ImGui::Spacing();
		ImGui::SeparatorText("CapabilityService example");
		if (ImGui::Button("Require ui.notifications"))
		{
			try
			{
				capabilityService.Require("ui.notifications");
				emitNotification(
					NotificationSeverity::Info,
					"Capability available",
					"ui.notifications requirement satisfied",
					NotificationCategory::Capability);
			}
			catch (const std::exception &exception)
			{
				emitNotification(
					NotificationSeverity::Error,
					"Capability missing",
					exception.what(),
					NotificationCategory::Capability);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Require demo.missing"))
		{
			try
			{
				capabilityService.Require("demo.missing");
				emitNotification(
					NotificationSeverity::Info,
					"Capability available",
					"demo.missing requirement satisfied",
					NotificationCategory::Capability);
			}
			catch (const std::exception &exception)
			{
				emitNotification(
					NotificationSeverity::Error,
					"Capability missing",
					exception.what(),
					NotificationCategory::Capability);
			}
		}

		ImGui::Spacing();
		ImGui::SeparatorText("StructuredError example");
		if (ImGui::Button("Show blocking StructuredError"))
		{
			StructuredError error{
				ErrorCategory::Runtime,
				Severity::Error,
				"Unable to load required renderer resource.",
				"Resource key='demo.missing.texture' was not found in cache.",
				"Verify assets path and rebuild the cache.",
				"DemoLayer",
				"DEMO_RENDERER_ASSET_MISSING",
				DisplayPolicy::BlockingPopup};
			application.ShowBlockingError(error);
			notifier.Notify(ToNotification(error));
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Recent notifier history");
		const auto &history = notifier.GetHistory();
		ImGui::Text("History entries: %zu", history.size());
		if (ImGui::BeginChild("##notification_history", ImVec2(0.0f, 180.0f), true))
		{
			for (auto it = history.rbegin(); it != history.rend(); ++it)
			{
				const Notification &notification = *it;
				const ImVec4 color = severityColor(notification.severity);
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::BulletText("[%s] %s", notification.title.c_str(), notification.message.c_str());
				ImGui::PopStyleColor();
				if (!notification.source.empty())
				{
					ImGui::Indent();
					ImGui::TextDisabled("Source: %s", notification.source.c_str());
					ImGui::Unindent();
				}
			}
		}
		ImGui::EndChild();
	}
} // namespace DefectStudio::Demo
