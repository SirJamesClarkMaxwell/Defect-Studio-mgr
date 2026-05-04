#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <vector>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Logger.hpp"
#include "Presentation/EditorUiEvents.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"

namespace DefectStudio
{
	namespace
	{
		template <std::size_t N>
		void copyToBuffer(std::array<char, N> &buffer, std::string_view value)
		{
			std::snprintf(buffer.data(), buffer.size(), "%s", std::string(value).c_str());
		}

		void sliderRule(const char *label, float &value, float minValue, float maxValue)
		{
			ImGui::SliderFloat(label, &value, minValue, maxValue, "%.3f");
		}

		std::string resolveFontLabel(const Ref<EditorUiState> &uiState, std::string_view fontPath)
		{
			if (uiState == nullptr)
				return "<editor unavailable>";

			for (const EditorFontOption &option : uiState->fontOptions)
			{
				if (option.path == fontPath)
					return option.label;
			}

			return fontPath.empty() ? "<default font>" : "Custom path";
		}

		constexpr std::array<LogLevel, 6> LogLevels = {
			LogLevel::Trace,
			LogLevel::Debug,
			LogLevel::Info,
			LogLevel::Warn,
			LogLevel::Error,
			LogLevel::Critical,
		};

		template <typename EventType>
		bool queueEvent(const Ref<EventBus> &eventBus, const EventType &event)
		{
			if (eventBus == nullptr)
				return false;

			eventBus->Queue(event);
			return true;
		}

		template <typename EventType>
		SubscriptionHandle subscribeSettings(
			EventBus &bus,
			SettingsPanel &settings,
			void (SettingsPanel::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &settings));
		}
	} // namespace

	SettingsPanel::SettingsPanel(Ref<EventBus> eventBus,
	                   WeakRef<JobSystem> jobSystem,
	                   WeakRef<EditorUiState> uiState,
	                   ApplicationConfig initialConfig,
	                   std::string title,
	                   bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_DraftConfig(std::move(initialConfig)),
		  m_EventBus(std::move(eventBus)),
		  m_JobSystem(std::move(jobSystem)),
		  m_UiState(std::move(uiState))
	{
		bindConfigEvents();
		if (m_DraftInitialized)
			m_ProfileManager.Bind(m_EventBus);
	}

	SettingsPanel::SettingsPanel(const SettingsPanel &other)
		: IPanel(other.GetTitle(), other.IsVisible()),
		  m_DraftInitialized(other.m_DraftInitialized),
		  m_DraftDirty(other.m_DraftDirty),
		  m_LayoutListRequested(other.m_LayoutListRequested),
 		m_SelectedTab(other.m_SelectedTab),
		  m_DraftConfig(other.m_DraftConfig),
		  m_WindowTitleBuffer(other.m_WindowTitleBuffer),
		  m_LogFilePathBuffer(other.m_LogFilePathBuffer),
		  m_FontPathBuffer(other.m_FontPathBuffer),
		  m_ThemeSavePathBuffer(other.m_ThemeSavePathBuffer),
		  m_ThemeLoadPathBuffer(other.m_ThemeLoadPathBuffer),
		  m_LayoutPathBuffer(other.m_LayoutPathBuffer),
		  m_StatusMessage(other.m_StatusMessage),
		  m_EventBus(other.m_EventBus),
		  m_JobSystem(other.m_JobSystem),
		  m_UiState(other.m_UiState)
	{
		bindConfigEvents();
	}

	Ref<IPanel> SettingsPanel::Clone() const
	{
		return CreateRef<SettingsPanel>(*this);
	}

	bool SettingsPanel::IsUrgentWorkerReserved() const
	{
		return m_DraftInitialized ? m_DraftConfig.jobs.reserveUrgentWorker : true;
	}

	float SettingsPanel::GetFontScaleStep() const
	{
		return m_DraftInitialized ? m_DraftConfig.ui.fontScaleStep : 0.1f;
	}

	float SettingsPanel::GetFontScale() const
	{
		return m_DraftInitialized ? m_DraftConfig.ui.fontScale : 1.0f;
	}

	void SettingsPanel::Render()
	{
		ensureDraftInitialized();
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		ImGui::SetNextWindowSize(ImVec2(980.0f, 640.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			SetVisible(visible);
			ImGui::End();
			return;
		}
		SetVisible(visible);

		ImGui::BeginChild("SettingsSidebar", ImVec2(190.0f, 0.0f), true);
		renderSidebar();
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("SettingsContent", ImVec2(0.0f, 0.0f), true);
		renderActionBar();

		switch (m_SelectedTab)
		{
		case SettingsPanel::Tab::System:
			renderSystemTab();
			break;
		case SettingsPanel::Tab::Interface:
			renderDisplayTab();
			break;
		case SettingsPanel::Tab::Profiles:
			renderProfilesTab();
			break;
		case SettingsPanel::Tab::Layout:
			renderLayoutTab();
			break;
		case SettingsPanel::Tab::Viewport:
			renderViewportTab();
			break;
		case SettingsPanel::Tab::FilePaths:
			renderFilePathsTab();
			break;
		default:
			ImGui::TextUnformatted("This section is a placeholder for future YAML-backed settings.");
			break;
		}

		maybeApplyPreview();
		ImGui::EndChild();

		ImGui::End();
	}

	void SettingsPanel::ensureDraftInitialized()
	{
		if (m_DraftInitialized)
			return;

		syncDraftFromCurrentConfig();
		m_ProfileManager.Bind(m_EventBus);
	}

	void SettingsPanel::syncDraftFromCurrentConfig()
	{
		if (auto uiState = m_UiState.lock())
		{
			m_DraftConfig.ui.fontScale = std::clamp(uiState->fontScale, m_DraftConfig.ui.fontScaleMin, m_DraftConfig.ui.fontScaleMax);
			m_DraftConfig.ui.fontScaleStep = std::clamp(uiState->fontScaleStep, m_DraftConfig.ui.fontScaleStepMin, m_DraftConfig.ui.fontScaleStepMax);
			m_DraftConfig.ui.fontPath = uiState->selectedFontPath;
			m_DraftConfig.jobs.defaultWorkerThreadCount = uiState->defaultWorkerThreadCount;
			m_DraftConfig.jobs.reserveUrgentWorker = uiState->reserveUrgentWorkerByDefault;
			m_DraftConfig.appearance = uiState->appearance;
			m_DraftConfig.paths = uiState->paths;
			m_DraftConfig.layout.imGuiIniPath = uiState->layoutPath;
		}
		copyToBuffer(m_WindowTitleBuffer, m_DraftConfig.window.title);
		copyToBuffer(m_LogFilePathBuffer, m_DraftConfig.log.filePath.String());
		copyToBuffer(m_FontPathBuffer, m_DraftConfig.ui.fontPath);
		m_DraftDirty = false;
		m_DraftInitialized = true;
	}

	void SettingsPanel::syncDraftFromBuffers()
	{
		m_DraftConfig.window.title = m_WindowTitleBuffer.data();
		m_DraftConfig.log.filePath = Path::FromResolved(std::string(m_LogFilePathBuffer.data()));
		m_DraftConfig.ui.fontPath = m_FontPathBuffer.data();
	}

	bool SettingsPanel::applyDraft(bool persist)
	{
		using namespace AppEvents::Config;

		syncDraftFromBuffers();

		m_DraftConfig.ui.fontScaleMax = std::max(m_DraftConfig.ui.fontScaleMax, m_DraftConfig.ui.fontScaleMin);
		m_DraftConfig.ui.fontScale = std::clamp(m_DraftConfig.ui.fontScale, m_DraftConfig.ui.fontScaleMin, m_DraftConfig.ui.fontScaleMax);

		m_DraftConfig.ui.fontScaleStepMax = std::max(m_DraftConfig.ui.fontScaleStepMax, m_DraftConfig.ui.fontScaleStepMin);
		m_DraftConfig.ui.fontScaleStep = std::clamp(
			m_DraftConfig.ui.fontScaleStep,
			m_DraftConfig.ui.fontScaleStepMin,
			m_DraftConfig.ui.fontScaleStepMax);
		m_DraftConfig.ui.fontScaleStepSliderMax = std::clamp(
			m_DraftConfig.ui.fontScaleStepSliderMax,
			m_DraftConfig.ui.fontScaleStepMin,
			m_DraftConfig.ui.fontScaleStepMax);

		m_DraftConfig.jobs.defaultWorkerThreadCount = std::max(1, m_DraftConfig.jobs.defaultWorkerThreadCount);
		m_DraftConfig.eventQueue.initialCapacity = std::max<std::size_t>(m_DraftConfig.eventQueue.initialCapacity, 32);
		m_DraftConfig.eventQueue.growthStep = std::max<std::size_t>(m_DraftConfig.eventQueue.growthStep, 32);
		m_DraftConfig.window.width = std::max(320, m_DraftConfig.window.width);
		m_DraftConfig.window.height = std::max(240, m_DraftConfig.window.height);

		if (!queueEvent(m_EventBus, ApplyRequested{m_DraftConfig, persist}))
		{
			m_StatusMessage = "Apply failed: EventBus unavailable.";
			return false;
		}

		m_StatusMessage = persist ? "Apply & save queued." : "Runtime apply queued.";
		return true;
	}

	bool SettingsPanel::saveDraftAsDefaults()
	{
		using namespace AppEvents::Config;

		syncDraftFromBuffers();

		if (!queueEvent(m_EventBus, SaveDefaultsRequested{m_DraftConfig}))
		{
			m_StatusMessage = "Save defaults failed: EventBus unavailable.";
			return false;
		}

		m_StatusMessage = "Default YAML save queued.";
		return true;
	}

	void SettingsPanel::applyRuntimePreview()
	{
		using namespace AppEvents::Config;
		using namespace EditorUiEvents;

		syncDraftFromBuffers();

		if (auto uiState = m_UiState.lock())
		{
			const bool fontPathChanged = uiState->selectedFontPath != m_DraftConfig.ui.fontPath;
			uiState->fontScale = std::clamp(m_DraftConfig.ui.fontScale, m_DraftConfig.ui.fontScaleMin, m_DraftConfig.ui.fontScaleMax);
			uiState->fontScaleStep = std::clamp(m_DraftConfig.ui.fontScaleStep, m_DraftConfig.ui.fontScaleStepMin, m_DraftConfig.ui.fontScaleStepMax);
			uiState->selectedFontPath = m_DraftConfig.ui.fontPath;
			uiState->appearance = m_DraftConfig.appearance;
			(void)queueEvent(m_EventBus, ConfigPreviewRequested{m_DraftConfig.ui});
			(void)queueEvent(m_EventBus, AppearancePreviewRequested{m_DraftConfig.appearance});
			if (fontPathChanged)
				(void)queueEvent(m_EventBus, FontReloadRequested{});
		}

		if (m_DraftConfig.ui.settingsAutoSaveOnPreview)
		{
			if (queueEvent(m_EventBus, SaveUserRequested{m_DraftConfig}))
				m_StatusMessage = "Preview applied; auto-save queued.";
			else
				m_StatusMessage = "Preview applied; auto-save failed: EventBus unavailable.";
		}
	}

	void SettingsPanel::maybeApplyPreview()
	{
		if (!m_DraftDirty || !m_DraftConfig.ui.settingsPreviewEnabled)
			return;

		if (ImGui::IsAnyItemActive())
			return;

		applyRuntimePreview();
		m_DraftDirty = false;
	}

	void SettingsPanel::previewAppearanceIfEnabled()
	{
		using namespace EditorUiEvents;

		if (!m_DraftConfig.ui.settingsPreviewEnabled)
			return;

		if (auto uiState = m_UiState.lock())
		{
			uiState->appearance = m_DraftConfig.appearance;
			(void)queueEvent(m_EventBus, AppearancePreviewRequested{m_DraftConfig.appearance});
		}
	}

	void SettingsPanel::bindConfigEvents()
	{
		if (m_EventBus == nullptr)
			return;

		using namespace AppEvents::Config;

		AddSubscription(subscribeSettings<Applied>(*m_EventBus, *this, &SettingsPanel::onConfigApplied));
		AddSubscription(subscribeSettings<ApplyFailed>(*m_EventBus, *this, &SettingsPanel::onConfigApplyFailed));
		AddSubscription(subscribeSettings<UserSaved>(*m_EventBus, *this, &SettingsPanel::onUserConfigSaved));
		AddSubscription(subscribeSettings<UserSaveFailed>(*m_EventBus, *this, &SettingsPanel::onUserConfigSaveFailed));
		AddSubscription(subscribeSettings<DefaultsSaved>(*m_EventBus, *this, &SettingsPanel::onDefaultsSaved));
		AddSubscription(subscribeSettings<DefaultsSaveFailed>(*m_EventBus, *this, &SettingsPanel::onDefaultsSaveFailed));
		AddSubscription(subscribeSettings<EditorUiEvents::LayoutListLoaded>(*m_EventBus, *this, &SettingsPanel::onLayoutListLoaded));
		AddSubscription(subscribeSettings<EditorUiEvents::LayoutListFailed>(*m_EventBus, *this, &SettingsPanel::onLayoutListFailed));
	}

	void SettingsPanel::onConfigApplied(const AppEvents::Config::Applied &event)
	{
		m_DraftConfig = event.config;
		copyToBuffer(m_WindowTitleBuffer, m_DraftConfig.window.title);
		copyToBuffer(m_LogFilePathBuffer, m_DraftConfig.log.filePath.String());
		copyToBuffer(m_FontPathBuffer, m_DraftConfig.ui.fontPath);
		m_DraftDirty = false;
		m_DraftInitialized = true;
		m_StatusMessage = event.persisted ? "Settings applied and saved to YAML." : "Settings applied.";
		DS_LOG_INFO("SettingsPanel apply complete persisted={} title=\"{}\"", event.persisted, m_DraftConfig.window.title);
	}

	void SettingsPanel::onConfigApplyFailed(const AppEvents::Config::ApplyFailed &event)
	{
		m_StatusMessage = event.persist ? "Apply & save failed: " + event.error : "Apply failed: " + event.error;
		DS_LOG_WARN("SettingsPanel apply failed persist={} error={}", event.persist, event.error);
	}

	void SettingsPanel::onUserConfigSaved(const AppEvents::Config::UserSaved &event)
	{
		m_DraftConfig = event.config;
		copyToBuffer(m_WindowTitleBuffer, m_DraftConfig.window.title);
		copyToBuffer(m_LogFilePathBuffer, m_DraftConfig.log.filePath.String());
		copyToBuffer(m_FontPathBuffer, m_DraftConfig.ui.fontPath);
		m_DraftDirty = false;
		m_StatusMessage = "Settings applied and user YAML saved.";
		DS_LOG_INFO("SettingsPanel user config saved title=\"{}\"", m_DraftConfig.window.title);
	}

	void SettingsPanel::onUserConfigSaveFailed(const AppEvents::Config::UserSaveFailed &event)
	{
		(void)event.config;
		m_StatusMessage = "User YAML save failed: " + event.error;
		DS_LOG_ERROR("SettingsPanel user config save failed error={}", event.error);
	}

	void SettingsPanel::onDefaultsSaved(const AppEvents::Config::DefaultsSaved &event)
	{
		(void)event.config;
		m_StatusMessage = "Default YAML saved.";
		DS_LOG_INFO("SettingsPanel default config saved");
	}

	void SettingsPanel::onDefaultsSaveFailed(const AppEvents::Config::DefaultsSaveFailed &event)
	{
		(void)event.config;
		m_StatusMessage = "Save defaults failed: " + event.error;
		DS_LOG_ERROR("SettingsPanel default config save failed error={}", event.error);
	}

	void SettingsPanel::onLayoutListLoaded(const EditorUiEvents::LayoutListLoaded &event)
	{
		if (auto uiState = m_UiState.lock())
		{
			uiState->availableLayouts = event.layouts;
			uiState->layoutStatusMessage = event.layouts.empty()
				? "No .ini layouts found in " + event.directory.String()
				: "Layouts refreshed: " + std::to_string(event.layouts.size());
		}
		m_LayoutListRequested = false;
	}

	void SettingsPanel::onLayoutListFailed(const EditorUiEvents::LayoutListFailed &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->layoutStatusMessage = "Layout refresh failed: " + event.error;
		m_LayoutListRequested = false;
	}

	void SettingsPanel::renderActionBar()
	{
		ImGui::TextUnformatted("Configuration editor");
		ImGui::TextWrapped("This panel edits YAML-backed application settings. Preview updates runtime without touching disk, while Apply & Save also persists the current snapshot.");

		if (ImGui::Button("Reload current config"))
			syncDraftFromCurrentConfig();

		ImGui::SameLine();
		if (ImGui::Button("Preview now"))
			(void)applyDraft(false);

		ImGui::SameLine();
		if (ImGui::Button("Apply & Save YAML"))
			(void)applyDraft(true);

		ImGui::SameLine();
		if (ImGui::Button("Save as defaults"))
			(void)saveDraftAsDefaults();

		if (ImGui::Checkbox("Application preview", &m_DraftConfig.ui.settingsPreviewEnabled))
			m_DraftDirty = true;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Apply changes to the running app after editing is finished.");

		ImGui::SameLine();
		ImGui::BeginDisabled(!m_DraftConfig.ui.settingsPreviewEnabled);
		if (ImGui::Checkbox("Auto-save with preview", &m_DraftConfig.ui.settingsAutoSaveOnPreview))
			m_DraftDirty = true;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("When preview is enabled, also write YAML automatically after each applied change.");
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (m_DraftDirty)
			ImGui::TextColored(ImVec4(0.94f, 0.54f, 0.14f, 1.0f), "Unsaved changes");
		else
			ImGui::TextDisabled("In sync");

		if (!m_StatusMessage.empty())
			ImGui::TextWrapped("%s", m_StatusMessage.c_str());

		ImGui::Separator();
	}

	void SettingsPanel::renderSidebar()
	{
		const char *tabs[] = {
			"System",
			"Interface",
			"Profiles",
			"Layout",
			"Viewport",
			"Editing",
			"Animation",
			"Input",
			"File Paths",
		};

		const int tabCount = static_cast<int>(SettingsPanel::Tab::Count);
		for (int i = 0; i < tabCount; ++i)
		{
			const auto tab = static_cast<SettingsPanel::Tab>(i);
			const bool selected = (m_SelectedTab == tab);
			if (ImGui::Selectable(tabs[i], selected))
				m_SelectedTab = tab;
		}
	}

	void SettingsPanel::renderSystemTab()
	{
		auto jobSystem = m_JobSystem.lock();

		ImGui::SeparatorText("Logging");
		if (ImGui::BeginTable("SystemLogging", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Log level");
			ImGui::TableSetColumnIndex(1);
			const char *currentLogLevel = ToString(m_DraftConfig.log.level);
			if (ImGui::BeginCombo("##LogLevel", currentLogLevel))
			{
				for (LogLevel level : LogLevels)
				{
					const bool selected = (level == m_DraftConfig.log.level);
					if (ImGui::Selectable(ToString(level), selected))
					{
						m_DraftConfig.log.level = level;
						m_DraftDirty = true;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Log to file");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::Checkbox("##LogToFile", &m_DraftConfig.log.toFile))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Trace events");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::Checkbox("##TraceEvents", &m_DraftConfig.log.traceEvents))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Log file path");
			ImGui::TableSetColumnIndex(1);
			ImGui::PushItemWidth(-1.0f);
			if (ImGui::InputText("##LogFilePath", m_LogFilePathBuffer.data(), m_LogFilePathBuffer.size()))
				m_DraftDirty = true;
			ImGui::PopItemWidth();

			ImGui::EndTable();
		}
		ImGui::TextWrapped("Logger is reinitialized when you click Apply & Save.");

		ImGui::SeparatorText("Job system");
		if (ImGui::BeginTable("SystemJobs", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Default worker threads");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::InputInt("##DefaultWorkerThreads", &m_DraftConfig.jobs.defaultWorkerThreadCount))
			{
				m_DraftConfig.jobs.defaultWorkerThreadCount = std::max(1, m_DraftConfig.jobs.defaultWorkerThreadCount);
				m_DraftDirty = true;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Reserve urgent worker");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::Checkbox("##ReserveUrgentWorker", &m_DraftConfig.jobs.reserveUrgentWorker))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Effective threads after apply");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d",
			            m_DraftConfig.jobs.defaultWorkerThreadCount + (m_DraftConfig.jobs.reserveUrgentWorker ? 1 : 0));

			if (jobSystem != nullptr)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("Current runtime threads");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%llu", static_cast<unsigned long long>(jobSystem->GetThreadCount()));
			}

			ImGui::EndTable();
		}
		ImGui::TextWrapped("Worker pool changes are applied live when saved.");

		ImGui::SeparatorText("Event queue");
		int initialCapacity = static_cast<int>(m_DraftConfig.eventQueue.initialCapacity);
		int growthStep = static_cast<int>(m_DraftConfig.eventQueue.growthStep);
		if (ImGui::BeginTable("SystemEventQueue", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Initial capacity");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::InputInt("##EventQueueInitialCapacity", &initialCapacity))
			{
				m_DraftConfig.eventQueue.initialCapacity = static_cast<std::size_t>(std::max(initialCapacity, 32));
				m_DraftDirty = true;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Growth step");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::InputInt("##EventQueueGrowthStep", &growthStep))
			{
				m_DraftConfig.eventQueue.growthStep = static_cast<std::size_t>(std::max(growthStep, 32));
				m_DraftDirty = true;
			}

			ImGui::EndTable();
		}
		ImGui::TextWrapped("Applying event queue settings reconfigures the queue and clears currently queued platform events.");
	}

	void SettingsPanel::renderDisplayTab()
	{
		using namespace EditorUiEvents;

		auto uiState = m_UiState.lock();

		m_DraftConfig.ui.fontScaleMax = std::max(m_DraftConfig.ui.fontScaleMax, m_DraftConfig.ui.fontScaleMin);
		m_DraftConfig.ui.fontScaleStepMax = std::max(m_DraftConfig.ui.fontScaleStepMax, m_DraftConfig.ui.fontScaleStepMin);
		m_DraftConfig.ui.fontScaleStepSliderMax = std::clamp(
			m_DraftConfig.ui.fontScaleStepSliderMax,
			m_DraftConfig.ui.fontScaleStepMin,
			m_DraftConfig.ui.fontScaleStepMax);

		ImGui::SeparatorText("Display settings");
		if (ImGui::BeginTable("DisplaySettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font family");
			ImGui::TableSetColumnIndex(1);

			if (uiState == nullptr)
			{
				ImGui::TextDisabled("Editor UI state unavailable");
			}
			else if (uiState->fontOptions.empty())
			{
				ImGui::TextDisabled("No fonts discovered");
			}
			else
			{
				const std::string currentFontLabel = resolveFontLabel(uiState, m_DraftConfig.ui.fontPath);
				if (ImGui::BeginCombo("##FontFamily", currentFontLabel.c_str()))
				{
					for (const EditorFontOption &option : uiState->fontOptions)
					{
						const bool selected = option.path == m_DraftConfig.ui.fontPath;
						if (ImGui::Selectable(option.label.c_str(), selected))
						{
							m_DraftConfig.ui.fontPath = option.path;
							copyToBuffer(m_FontPathBuffer, option.path);
							m_DraftDirty = true;
						}

						if (ImGui::IsItemHovered() && !option.path.empty())
							ImGui::SetTooltip("%s", option.path.c_str());

						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font list");
			ImGui::TableSetColumnIndex(1);
			if (uiState == nullptr)
				ImGui::BeginDisabled();
			if (ImGui::Button("Refresh") && uiState != nullptr)
				(void)queueEvent(m_EventBus, FontListRefreshRequested{});
			if (uiState == nullptr)
				ImGui::EndDisabled();

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font path");
			ImGui::TableSetColumnIndex(1);
			ImGui::PushItemWidth(-1.0f);
			if (ImGui::InputText("##FontPath", m_FontPathBuffer.data(), m_FontPathBuffer.size()))
				m_DraftDirty = true;
			ImGui::PopItemWidth();

			if (uiState != nullptr && !uiState->fontStatusMessage.empty())
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("Status");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextWrapped("%s", uiState->fontStatusMessage.c_str());
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font scale");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::SliderFloat("##FontScale", &m_DraftConfig.ui.fontScale, m_DraftConfig.ui.fontScaleMin, m_DraftConfig.ui.fontScaleMax, "%.2f"))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font step (Ctrl +/-)");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::SliderFloat("##FontScaleStep",
			                       &m_DraftConfig.ui.fontScaleStep,
			                       m_DraftConfig.ui.fontScaleStepMin,
			                       m_DraftConfig.ui.fontScaleStepSliderMax,
			                       "%.2f"))
			{
				m_DraftDirty = true;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Shortcut preview");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Ctrl + / Ctrl - adjusts by %.2f", m_DraftConfig.ui.fontScaleStep);

			ImGui::EndTable();
		}

		ImGui::SeparatorText("Font range tuning");
		if (ImGui::BeginTable("DisplayRanges", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font scale min");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::DragFloat("##FontScaleMin", &m_DraftConfig.ui.fontScaleMin, 0.01f, 0.1f, 16.0f, "%.2f"))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font scale max");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::DragFloat("##FontScaleMax", &m_DraftConfig.ui.fontScaleMax, 0.05f, 0.1f, 24.0f, "%.2f"))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font step min");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::DragFloat("##FontStepMin", &m_DraftConfig.ui.fontScaleStepMin, 0.005f, 0.001f, 2.0f, "%.3f"))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font step max");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::DragFloat("##FontStepMax", &m_DraftConfig.ui.fontScaleStepMax, 0.01f, 0.001f, 4.0f, "%.3f"))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Font step slider max");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::DragFloat("##FontStepSliderMax", &m_DraftConfig.ui.fontScaleStepSliderMax, 0.01f, 0.001f, 4.0f, "%.3f"))
				m_DraftDirty = true;

			ImGui::EndTable();
		}

		ImGui::SeparatorText("Appearance");
		ImGui::TextWrapped("Appearance values are also stored in YAML. Theme file import/export is part of this panel.");
		renderAppearanceColors();
		renderAppearanceMetrics();
		renderAppearanceStateRules();
		renderAppearanceFiles();
	}

	void SettingsPanel::renderProfilesTab()
	{
		auto uiState = m_UiState.lock();
		(void)m_ProfileManager.Render(uiState.get(), m_DraftConfig);
	}

	void SettingsPanel::renderAppearanceFiles()
		{
			using namespace EditorUiEvents;

			if (!ImGui::CollapsingHeader("Theme and layout files", ImGuiTreeNodeFlags_DefaultOpen))
				return;

			auto uiState = m_UiState.lock();
			if (uiState == nullptr)
			{
				ImGui::TextDisabled("Editor UI state unavailable.");
				return;
			}

			copyToBuffer(m_ThemeSavePathBuffer, uiState->themeSavePath);
			copyToBuffer(m_ThemeLoadPathBuffer, uiState->themeLoadPath);
			copyToBuffer(m_LayoutPathBuffer, uiState->layoutPath);

			ImGui::InputText("Theme save path", m_ThemeSavePathBuffer.data(), m_ThemeSavePathBuffer.size());
			if (ImGui::Button("Save theme YAML"))
			{
				uiState->themeSavePath = m_ThemeSavePathBuffer.data();
				(void)queueEvent(m_EventBus, ThemeSaveRequested{Path::FromResolved(uiState->themeSavePath), uiState->appearance});
			}

			ImGui::InputText("Theme load path", m_ThemeLoadPathBuffer.data(), m_ThemeLoadPathBuffer.size());
			if (ImGui::Button("Load theme YAML"))
			{
				uiState->themeLoadPath = m_ThemeLoadPathBuffer.data();
				(void)queueEvent(m_EventBus, ThemeLoadRequested{Path::FromResolved(uiState->themeLoadPath)});
			}

			ImGui::Separator();
			ImGui::InputText("Layout .ini path", m_LayoutPathBuffer.data(), m_LayoutPathBuffer.size());
			if (ImGui::Button("Save ImGui layout"))
			{
				uiState->layoutPath = m_LayoutPathBuffer.data();
				(void)queueEvent(m_EventBus, LayoutSaveRequested{Path::FromResolved(uiState->layoutPath)});
			}
			ImGui::SameLine();
			if (ImGui::Button("Load ImGui layout"))
			{
				uiState->layoutPath = m_LayoutPathBuffer.data();
				(void)queueEvent(m_EventBus, LayoutLoadRequested{Path::FromResolved(uiState->layoutPath)});
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset runtime layout"))
			{
				uiState->layoutPath = m_LayoutPathBuffer.data();
				(void)queueEvent(m_EventBus, LayoutResetRequested{Path::FromResolved(uiState->layoutPath)});
			}

			if (!uiState->layoutStatusMessage.empty())
				ImGui::TextWrapped("%s", uiState->layoutStatusMessage.c_str());
			}

	void SettingsPanel::renderLayoutTab()
	{
		using namespace EditorUiEvents;

		auto uiState = m_UiState.lock();
		if (uiState == nullptr)
		{
			ImGui::TextDisabled("Editor UI state unavailable.");
			return;
		}

		ImGui::TextUnformatted("ImGui layout");
		ImGui::TextWrapped("Layout is saved as ImGui .ini data. Requests go through the event bus.");
		ImGui::TextWrapped("Active layout: %s", uiState->layoutPath.empty() ? "<not configured>" : uiState->layoutPath.c_str());
		if (ImGui::Button("Save current layout"))
			(void)queueEvent(m_EventBus, LayoutSaveRequested{Path::FromResolved(uiState->layoutPath)});
		ImGui::SameLine();
		if (ImGui::Button("Load active layout"))
			(void)queueEvent(m_EventBus, LayoutLoadRequested{Path::FromResolved(uiState->layoutPath)});
		ImGui::SameLine();
		if (ImGui::Button("Reset runtime layout"))
			(void)queueEvent(m_EventBus, LayoutResetRequested{Path::FromResolved(uiState->layoutPath)});

		if (!uiState->layoutStatusMessage.empty())
			ImGui::TextWrapped("%s", uiState->layoutStatusMessage.c_str());

		if (!m_LayoutListRequested && uiState->availableLayouts.empty())
		{
			(void)queueEvent(m_EventBus, LayoutListRequested{m_DraftConfig.paths.layoutsDirectory});
			m_LayoutListRequested = true;
		}

		ImGui::SeparatorText("Available layouts");
		if (ImGui::Button("Refresh layouts"))
		{
			(void)queueEvent(m_EventBus, LayoutListRequested{m_DraftConfig.paths.layoutsDirectory});
			m_LayoutListRequested = true;
		}
		if (uiState->availableLayouts.empty())
		{
			ImGui::TextDisabled("No .ini layouts in %s", m_DraftConfig.paths.layoutsDirectory.String().c_str());
			return;
		}

		for (const Path &layoutPath : uiState->availableLayouts)
		{
			if (ImGui::Selectable(layoutPath.Native().filename().string().c_str(), uiState->layoutPath == layoutPath.String()))
				uiState->layoutPath = layoutPath.String();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", layoutPath.String().c_str());
		}
	}

	void SettingsPanel::renderViewportTab()
	{
		ImGui::TextUnformatted("Window settings");
		ImGui::TextWrapped("These values are stored in YAML. Apply & Save updates the current GLFW window placement, size and maximize state, and also persists them for the next launch.");

		if (ImGui::BeginTable("ViewportSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Window maximized");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::Checkbox("##WindowMaximized", &m_DraftConfig.window.maximized))
				m_DraftDirty = true;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Window X");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::InputInt("##WindowX", &m_DraftConfig.window.x))
				m_DraftDirty = true;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("-1 keeps the operating system default placement.");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Window Y");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::InputInt("##WindowY", &m_DraftConfig.window.y))
				m_DraftDirty = true;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("-1 keeps the operating system default placement.");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Window width");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::InputInt("##WindowWidth", &m_DraftConfig.window.width))
			{
				m_DraftConfig.window.width = std::max(320, m_DraftConfig.window.width);
				m_DraftDirty = true;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Window height");
			ImGui::TableSetColumnIndex(1);
			if (ImGui::InputInt("##WindowHeight", &m_DraftConfig.window.height))
			{
				m_DraftConfig.window.height = std::max(240, m_DraftConfig.window.height);
				m_DraftDirty = true;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Window title");
			ImGui::TableSetColumnIndex(1);
			ImGui::PushItemWidth(-1.0f);
			if (ImGui::InputText("##WindowTitle", m_WindowTitleBuffer.data(), m_WindowTitleBuffer.size()))
				m_DraftDirty = true;
			ImGui::PopItemWidth();

			ImGui::EndTable();
		}
	}

	void SettingsPanel::renderFilePathsTab()
	{
		const ApplicationPaths &paths = m_DraftConfig.paths;
		const auto renderPathRow = [](const char *label, const std::string &value) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextWrapped("%s", value.c_str());
		};

		ImGui::TextUnformatted("Portable file layout");
		if (ImGui::BeginTable("PortablePaths", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			renderPathRow("Install root", paths.installRoot.String());
			renderPathRow("App config", paths.appConfigDirectory.String());
			renderPathRow("User config", paths.userConfigDirectory.String());
			renderPathRow("Profiles", paths.profilesDirectory.String());
			renderPathRow("Themes", paths.themesDirectory.String());
			renderPathRow("Layouts", paths.layoutsDirectory.String());
			renderPathRow("Exports", paths.exportsDirectory.String());
			renderPathRow("Assets", paths.assetsDirectory.String());
			renderPathRow("Fonts", paths.fontsDirectory.String());
			renderPathRow("default.yaml", (paths.appConfigDirectory / Path("default.yaml")).String());
			renderPathRow("ui_settings.yaml", (paths.userConfigDirectory / Path("ui_settings.yaml")).String());
			ImGui::EndTable();
		}
	}

	void SettingsPanel::renderAppearanceColors()
	{
		if (!ImGui::CollapsingHeader("Colors", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		AppearanceConfig &appearance = m_DraftConfig.appearance;
		const auto markDirty = [this]() {
			m_DraftDirty = true;
			previewAppearanceIfEnabled();
		};

		if (ImGui::ColorEdit4("Background", appearance.backgroundColor.data()))
			markDirty();
		if (ImGui::ColorEdit4("Surface", appearance.surfaceColor.data()))
			markDirty();
		if (ImGui::ColorEdit4("Raised surface", appearance.raisedSurfaceColor.data()))
			markDirty();
		if (ImGui::ColorEdit4("Border", appearance.borderColor.data()))
			markDirty();
		if (ImGui::ColorEdit4("Collapsible", appearance.collapsibleColor.data()))
			markDirty();
		if (ImGui::ColorEdit4("Text", appearance.textColor.data()))
			markDirty();
		if (ImGui::ColorEdit4("Muted text", appearance.mutedTextColor.data()))
			markDirty();
		if (ImGui::ColorEdit4("Accent controls", appearance.accentColor.data()))
			markDirty();
		if (ImGui::ColorEdit4("Clear color", appearance.clearColor.data()))
			markDirty();
	}

	void SettingsPanel::renderAppearanceMetrics()
	{
		if (!ImGui::CollapsingHeader("Rounding and spacing", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		AppearanceConfig &appearance = m_DraftConfig.appearance;
		const auto markDirty = [this]() {
			m_DraftDirty = true;
			previewAppearanceIfEnabled();
		};

		if (ImGui::SliderFloat("Window rounding", &appearance.windowRounding, 0.0f, 24.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Frame rounding", &appearance.frameRounding, 0.0f, 18.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Grab rounding", &appearance.grabRounding, 0.0f, 18.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Popup rounding", &appearance.popupRounding, 0.0f, 24.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Scrollbar rounding", &appearance.scrollbarRounding, 0.0f, 24.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Tab rounding", &appearance.tabRounding, 0.0f, 18.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Window padding X", &appearance.windowPaddingX, 0.0f, 32.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Window padding Y", &appearance.windowPaddingY, 0.0f, 32.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Frame padding X", &appearance.framePaddingX, 0.0f, 24.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Frame padding Y", &appearance.framePaddingY, 0.0f, 24.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Item spacing X", &appearance.itemSpacingX, 0.0f, 24.0f, "%.1f"))
			markDirty();
		if (ImGui::SliderFloat("Item spacing Y", &appearance.itemSpacingY, 0.0f, 24.0f, "%.1f"))
			markDirty();
	}

	void SettingsPanel::renderAppearanceStateRules()
	{
		if (!ImGui::CollapsingHeader("State rules", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		AppearanceStateRules &rules = m_DraftConfig.appearance.stateRules;
		const auto markDirty = [this]() {
			m_DraftDirty = true;
			previewAppearanceIfEnabled();
		};

		const float beforeNeutralHoverLighten = rules.neutralHoverLighten;
		sliderRule("Neutral hover lighten", rules.neutralHoverLighten, 0.0f, 0.35f);
		if (rules.neutralHoverLighten != beforeNeutralHoverLighten)
			markDirty();

		const float beforeNeutralActiveLighten = rules.neutralActiveLighten;
		sliderRule("Neutral active lighten", rules.neutralActiveLighten, 0.0f, 0.35f);
		if (rules.neutralActiveLighten != beforeNeutralActiveLighten)
			markDirty();

		const float beforeAccentHoverLighten = rules.accentHoverLighten;
		sliderRule("Accent hover lighten", rules.accentHoverLighten, 0.0f, 0.45f);
		if (rules.accentHoverLighten != beforeAccentHoverLighten)
			markDirty();

		const float beforeAccentHoverSaturation = rules.accentHoverSaturation;
		sliderRule("Accent hover saturation", rules.accentHoverSaturation, -0.25f, 0.60f);
		if (rules.accentHoverSaturation != beforeAccentHoverSaturation)
			markDirty();

		const float beforeAccentActiveDarken = rules.accentActiveDarken;
		sliderRule("Accent active darken", rules.accentActiveDarken, 0.0f, 0.45f);
		if (rules.accentActiveDarken != beforeAccentActiveDarken)
			markDirty();

		const float beforeAccentActiveSaturation = rules.accentActiveSaturation;
		sliderRule("Accent active saturation", rules.accentActiveSaturation, -0.25f, 0.60f);
		if (rules.accentActiveSaturation != beforeAccentActiveSaturation)
			markDirty();

		const float beforeSelectedAlpha = rules.selectedAlpha;
		sliderRule("Selected alpha", rules.selectedAlpha, 0.0f, 1.0f);
		if (rules.selectedAlpha != beforeSelectedAlpha)
			markDirty();

		const float beforeSelectedHoverAlpha = rules.selectedHoverAlpha;
		sliderRule("Selected hover alpha", rules.selectedHoverAlpha, 0.0f, 1.0f);
		if (rules.selectedHoverAlpha != beforeSelectedHoverAlpha)
			markDirty();

		const float beforeSelectedActiveAlpha = rules.selectedActiveAlpha;
		sliderRule("Selected active alpha", rules.selectedActiveAlpha, 0.0f, 1.0f);
		if (rules.selectedActiveAlpha != beforeSelectedActiveAlpha)
			markDirty();

		const float beforeDisabledAlpha = rules.disabledAlpha;
		sliderRule("Disabled alpha", rules.disabledAlpha, 0.05f, 1.0f);
		if (rules.disabledAlpha != beforeDisabledAlpha)
			markDirty();

		const float beforeDisabledBackgroundMix = rules.disabledBackgroundMix;
		sliderRule("Disabled background mix", rules.disabledBackgroundMix, 0.0f, 1.0f);
		if (rules.disabledBackgroundMix != beforeDisabledBackgroundMix)
			markDirty();
	}

	void SettingsPanel::SetFontScale(float fontScale)
	{
		ensureDraftInitialized();
		m_DraftConfig.ui.fontScale = std::clamp(fontScale, m_DraftConfig.ui.fontScaleMin, m_DraftConfig.ui.fontScaleMax);
		m_DraftDirty = true;
	}

	void SettingsPanel::SetFontScaleStep(float step)
	{
		ensureDraftInitialized();
		m_DraftConfig.ui.fontScaleStep = std::clamp(step, m_DraftConfig.ui.fontScaleStepMin, m_DraftConfig.ui.fontScaleStepMax);
		m_DraftDirty = true;
	}
} // namespace DefectStudio
