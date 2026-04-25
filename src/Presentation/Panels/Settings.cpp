#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <vector>

#include <imgui.h>

#include "App/Application.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Logger.hpp"
#include "Presentation/EditorUiEvents.hpp"
#include "Presentation/Panels/Settings.hpp"

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
			Settings &settings,
			void (Settings::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &settings));
		}
	} // namespace

	Settings::Settings(Ref<EventBus> eventBus,
	                   WeakRef<JobSystem> jobSystem,
	                   WeakRef<EditorUiState> uiState,
	                   std::string title,
	                   bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_EventBus(std::move(eventBus)),
		  m_JobSystem(std::move(jobSystem)),
		  m_UiState(std::move(uiState))
	{
		bindConfigEvents();
	}

	Settings::Settings(const Settings &other)
		: IPanel(other.GetTitle(), other.IsVisible()),
		  m_DraftInitialized(other.m_DraftInitialized),
		  m_DraftDirty(other.m_DraftDirty),
		  m_SelectedTabIndex(other.m_SelectedTabIndex),
		  m_DraftConfig(other.m_DraftConfig),
		  m_WindowTitleBuffer(other.m_WindowTitleBuffer),
		  m_LogFilePathBuffer(other.m_LogFilePathBuffer),
		  m_FontPathBuffer(other.m_FontPathBuffer),
		  m_StatusMessage(other.m_StatusMessage),
		  m_EventBus(other.m_EventBus),
		  m_JobSystem(other.m_JobSystem),
		  m_UiState(other.m_UiState),
		  m_ProfileManager(other.m_ProfileManager)
	{
		bindConfigEvents();
	}

	Ref<IPanel> Settings::Clone() const
	{
		return CreateRef<Settings>(*this);
	}

	bool Settings::IsUrgentWorkerReserved() const
	{
		return m_DraftInitialized ? m_DraftConfig.jobs.reserveUrgentWorker : true;
	}

	float Settings::GetFontScaleStep() const
	{
		return m_DraftInitialized ? m_DraftConfig.ui.fontScaleStep : 0.1f;
	}

	float Settings::GetFontScale() const
	{
		return m_DraftInitialized ? m_DraftConfig.ui.fontScale : 1.0f;
	}

	void Settings::Render()
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

		switch (m_SelectedTabIndex)
		{
		case 0:
			renderSystemTab();
			break;
		case 1:
			renderDisplayTab();
			break;
		case 2:
			renderProfilesTab();
			break;
		case 3:
			renderLayoutTab();
			break;
		case 4:
			renderViewportTab();
			break;
		case 8:
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

	void Settings::ensureDraftInitialized()
	{
		if (m_DraftInitialized)
			return;

		syncDraftFromApplication();
		m_ProfileManager.Bind(Application::Get().GetConfigManager(), m_EventBus);
	}

	void Settings::syncDraftFromApplication()
	{
		m_DraftConfig = Application::Get().GetConfig();
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

	void Settings::syncDraftFromBuffers()
	{
		m_DraftConfig.window.title = m_WindowTitleBuffer.data();
		m_DraftConfig.log.filePath = Path::FromResolved(std::string(m_LogFilePathBuffer.data()));
		m_DraftConfig.ui.fontPath = m_FontPathBuffer.data();
	}

	bool Settings::applyDraft(bool persist)
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

	bool Settings::saveDraftAsDefaults()
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

	void Settings::applyRuntimePreview()
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

	void Settings::maybeApplyPreview()
	{
		if (!m_DraftDirty || !m_DraftConfig.ui.settingsPreviewEnabled)
			return;

		if (ImGui::IsAnyItemActive())
			return;

		applyRuntimePreview();
		m_DraftDirty = false;
	}

	void Settings::previewAppearanceIfEnabled()
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

	void Settings::bindConfigEvents()
	{
		if (m_EventBus == nullptr)
			return;

		using namespace AppEvents::Config;

		AddSubscription(subscribeSettings<Applied>(*m_EventBus, *this, &Settings::onConfigApplied));
		AddSubscription(subscribeSettings<ApplyFailed>(*m_EventBus, *this, &Settings::onConfigApplyFailed));
		AddSubscription(subscribeSettings<UserSaved>(*m_EventBus, *this, &Settings::onUserConfigSaved));
		AddSubscription(subscribeSettings<UserSaveFailed>(*m_EventBus, *this, &Settings::onUserConfigSaveFailed));
		AddSubscription(subscribeSettings<DefaultsSaved>(*m_EventBus, *this, &Settings::onDefaultsSaved));
		AddSubscription(subscribeSettings<DefaultsSaveFailed>(*m_EventBus, *this, &Settings::onDefaultsSaveFailed));
	}

	void Settings::onConfigApplied(const AppEvents::Config::Applied &event)
	{
		m_DraftConfig = event.config;
		copyToBuffer(m_WindowTitleBuffer, m_DraftConfig.window.title);
		copyToBuffer(m_LogFilePathBuffer, m_DraftConfig.log.filePath.String());
		copyToBuffer(m_FontPathBuffer, m_DraftConfig.ui.fontPath);
		m_DraftDirty = false;
		m_DraftInitialized = true;
		m_StatusMessage = event.persisted ? "Settings applied and saved to YAML." : "Settings applied.";
	}

	void Settings::onConfigApplyFailed(const AppEvents::Config::ApplyFailed &event)
	{
		m_StatusMessage = event.persist ? "Apply & save failed: " + event.error : "Apply failed: " + event.error;
	}

	void Settings::onUserConfigSaved(const AppEvents::Config::UserSaved &event)
	{
		m_DraftConfig = event.config;
		copyToBuffer(m_WindowTitleBuffer, m_DraftConfig.window.title);
		copyToBuffer(m_LogFilePathBuffer, m_DraftConfig.log.filePath.String());
		copyToBuffer(m_FontPathBuffer, m_DraftConfig.ui.fontPath);
		m_DraftDirty = false;
		m_StatusMessage = "Preview applied and user YAML saved.";
	}

	void Settings::onUserConfigSaveFailed(const AppEvents::Config::UserSaveFailed &event)
	{
		(void)event.config;
		m_StatusMessage = "Preview applied, auto-save failed: " + event.error;
	}

	void Settings::onDefaultsSaved(const AppEvents::Config::DefaultsSaved &event)
	{
		(void)event.config;
		m_StatusMessage = "Default YAML saved.";
	}

	void Settings::onDefaultsSaveFailed(const AppEvents::Config::DefaultsSaveFailed &event)
	{
		(void)event.config;
		m_StatusMessage = "Save defaults failed: " + event.error;
	}

	void Settings::renderActionBar()
	{
		ImGui::TextUnformatted("Configuration editor");
		ImGui::TextWrapped("This panel edits YAML-backed application settings. Preview updates runtime without touching disk, while Apply & Save also persists the current snapshot.");

		if (ImGui::Button("Reload current config"))
			syncDraftFromApplication();

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

	void Settings::renderSidebar()
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

		for (int i = 0; i < IM_ARRAYSIZE(tabs); ++i)
		{
			const bool selected = (m_SelectedTabIndex == i);
			if (ImGui::Selectable(tabs[i], selected))
				m_SelectedTabIndex = i;
		}
	}

	void Settings::renderSystemTab()
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

	void Settings::renderDisplayTab()
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
		ImGui::TextWrapped("Appearance values are also stored in YAML. Theme file import/export remains available in the dedicated Appearance Editor panel.");
		renderAppearanceColors();
		renderAppearanceMetrics();
		renderAppearanceStateRules();
	}

	void Settings::renderProfilesTab()
	{
		auto uiState = m_UiState.lock();
		(void)m_ProfileManager.Render(uiState.get(), m_DraftConfig);
	}

	void Settings::renderLayoutTab()
	{
		using namespace EditorUiEvents;

		auto uiState = m_UiState.lock();
		if (uiState == nullptr)
		{
			ImGui::TextDisabled("Editor UI state unavailable.");
			return;
		}

		ImGui::TextUnformatted("ImGui layout");
		ImGui::TextWrapped("Layout is saved as ImGui .ini data. Requests go to ImGuiLayer; file access stays behind ConfigManager.");
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

		auto configManager = Application::Get().GetConfigManager().lock();
		if (configManager == nullptr)
			return;

		const std::vector<Path> layouts = configManager->ListIniFiles(configManager->GetPaths().layoutsDirectory);
		ImGui::SeparatorText("Available layouts");
		if (layouts.empty())
		{
			ImGui::TextDisabled("No .ini layouts in %s", configManager->GetPaths().layoutsDirectory.String().c_str());
			return;
		}

		for (const Path &layoutPath : layouts)
		{
			if (ImGui::Selectable(layoutPath.Native().filename().string().c_str(), uiState->layoutPath == layoutPath.String()))
				uiState->layoutPath = layoutPath.String();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", layoutPath.String().c_str());
		}
	}

	void Settings::renderViewportTab()
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

	void Settings::renderFilePathsTab()
	{
		auto configManager = Application::Get().GetConfigManager().lock();
		if (configManager == nullptr)
		{
			ImGui::TextDisabled("ConfigManager unavailable.");
			return;
		}

		const ApplicationPaths &paths = configManager->GetPaths();
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
			renderPathRow("default.yaml", ConfigManager::GetDefaultConfigPath(configManager->GetConfigDirectory()).String());
			renderPathRow("ui_settings.yaml", ConfigManager::GetUserSettingsPath(configManager->GetConfigDirectory()).String());
			ImGui::EndTable();
		}
	}

	void Settings::renderAppearanceColors()
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

	void Settings::renderAppearanceMetrics()
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

	void Settings::renderAppearanceStateRules()
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

	void Settings::SetFontScale(float fontScale)
	{
		ensureDraftInitialized();
		m_DraftConfig.ui.fontScale = std::clamp(fontScale, m_DraftConfig.ui.fontScaleMin, m_DraftConfig.ui.fontScaleMax);
		m_DraftDirty = true;
	}

	void Settings::SetFontScaleStep(float step)
	{
		ensureDraftInitialized();
		m_DraftConfig.ui.fontScaleStep = std::clamp(step, m_DraftConfig.ui.fontScaleStepMin, m_DraftConfig.ui.fontScaleStepMax);
		m_DraftDirty = true;
	}
} // namespace DefectStudio
