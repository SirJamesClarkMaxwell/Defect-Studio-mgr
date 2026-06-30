#include "Core/dspch.hpp"

#include <imgui.h>

#include "App/Events/LogExportEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "IconsFontAwesome6.h"

#include "Presentation/Panels/LoggingPanel.hpp"

namespace DefectStudio
{
	namespace
	{
		const char *severityLabel(LogLevel level)
		{
			return ToString(level);
		}

		ImVec4 severityColor(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace:
					return ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
				case LogLevel::Debug:
					return ImVec4(0.65f, 0.65f, 0.95f, 1.0f);
				case LogLevel::Info:
					return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
				case LogLevel::Warn:
					return ImVec4(0.95f, 0.7f, 0.2f, 1.0f);
				case LogLevel::Error:
					return ImVec4(0.95f, 0.2f, 0.2f, 1.0f);
			case LogLevel::Critical:
				return ImVec4(1.0f, 0.15f, 0.15f, 1.0f);
			case LogLevel::Count:
				break;
		}

			return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
		}

	} // namespace

	LoggingPanel::LoggingPanel(Ref<LogRegistry> logRegistry, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_LogRegistry(std::move(logRegistry)),
		  m_ExportPath(Path("logs") / Path("event-log-export.csv"))
	{
		m_ShowLevel.fill(true);
		for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
			m_ShowCategory[static_cast<LogCategory>(i)] = true;
		rebuildFromRegistry();
	}

	LoggingPanel::LoggingPanel(
		Ref<EventBus> eventBus,
		Ref<LogRegistry> logRegistry,
		Path exportPath,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_EventBus(std::move(eventBus)),
		  m_LogRegistry(std::move(logRegistry)),
		  m_ExportPath(std::move(exportPath))
	{
		m_ShowLevel.fill(true);
		for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
			m_ShowCategory[static_cast<LogCategory>(i)] = true;
		bindLogExportEvents();
		rebuildFromRegistry();
	}

	LoggingPanel::LoggingPanel(const LoggingPanel &other)
		: IPanel(other.GetTitle(), other.IsVisible()),
		  m_EventBus(other.m_EventBus),
		  m_LogRegistry(other.m_LogRegistry),
		  m_ExportPath(other.m_ExportPath),
		  m_Entries(other.m_Entries),
		  m_ShowLevel(other.m_ShowLevel),
		  m_ShowCategory(other.m_ShowCategory),
		  m_Paused(other.m_Paused),
		  m_AutoScroll(other.m_AutoScroll),
		  m_LastExportStatus(other.m_LastExportStatus)
	{
		bindLogExportEvents();
	}

	Ref<IPanel> LoggingPanel::Clone() const
	{
		return CreateRef<LoggingPanel>(*this);
	}

	void LoggingPanel::bindLogExportEvents()
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(m_EventBus->Subscribe<AppEvents::Logs::ExportCompleted>(
			[this](const AppEvents::Logs::ExportCompleted &event) { onExportCompleted(event); }));
		AddSubscription(m_EventBus->Subscribe<AppEvents::Logs::ExportFailed>(
			[this](const AppEvents::Logs::ExportFailed &event) { onExportFailed(event); }));
	}

	void LoggingPanel::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		ImGui::SetNextWindowSize(ImVec2(980.0f, 480.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			SetVisible(visible);
			ImGui::End();
			return;
		}
		SetVisible(visible);

		if (!m_Paused)
			rebuildFromRegistry();

		renderControls();
		ImGui::Separator();

		if (ImGui::BeginChild("LoggingSidebar", ImVec2(220.0f, 0.0f), true))
			renderCategoryFilters();
		ImGui::EndChild();

		ImGui::SameLine();
		if (ImGui::BeginChild("LoggingEntries", ImVec2(0.0f, 0.0f), true))
			renderEntries();
		ImGui::EndChild();

		ImGui::End();
	}

	void LoggingPanel::rebuildFromRegistry()
	{
		m_Entries.clear();
		if (m_LogRegistry == nullptr)
			return;

		m_Entries = m_LogRegistry->Snapshot();
	}

	void LoggingPanel::renderControls()
	{
		if (ImGui::Button(m_Paused ? ICON_FA_PLAY " Resume" : ICON_FA_PAUSE " Pause"))
			m_Paused = !m_Paused;

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TRASH " Clear"))
		{
			if (m_LogRegistry != nullptr)
				m_LogRegistry->Clear();
			m_Entries.clear();
		}

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_FILE_EXPORT " Export"))
		{
			requestExport();
		}

		ImGui::SameLine();
		ImGui::TextUnformatted("Sev:");
		ImGui::SameLine();
		for (std::size_t i = 0; i < m_ShowLevel.size(); ++i)
		{
			const auto level = static_cast<LogLevel>(i);
			ImGui::Checkbox(ToString(level), &m_ShowLevel[i]);
			ImGui::SameLine();
		}
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

		ImGui::SameLine();
		ImGui::TextDisabled("Visible entries: %zu", m_Entries.size());

		if (!m_LastExportStatus.empty())
			ImGui::TextUnformatted(m_LastExportStatus.c_str());
	}

	void LoggingPanel::renderCategoryFilters()
	{
		ImGui::TextUnformatted("Log categories");
		ImGui::Separator();

		for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
		{
			const auto category = static_cast<LogCategory>(i);
			bool &enabled = m_ShowCategory[category];
			std::string label = std::string(categoryIcon(category)) + " " + ToString(category);
			ImGui::Checkbox(label.c_str(), &enabled);
		}
	}

	void LoggingPanel::renderEntries()
	{
		if (ImGui::BeginTable("LogTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 130.0f);
			ImGui::TableSetupColumn("Origin", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (auto it = m_Entries.rbegin(); it != m_Entries.rend(); ++it)
			{
				const LogEntry &entry = *it;
				if (!isCategoryEnabled(entry.category))
					continue;
				if (!isSeverityEnabled(entry.level))
					continue;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushStyleColor(ImGuiCol_Text, severityColor(entry.level));
				ImGui::Text("%s %s", severityIcon(entry.level), severityLabel(entry.level));
				ImGui::PopStyleColor();

				ImGui::TableNextColumn();
				ImGui::Text("%s %s", categoryIcon(entry.category), ToString(entry.category));

				ImGui::TableNextColumn();
				const std::string origin = entry.Origin();
				ImGui::TextUnformatted(origin.c_str());

				ImGui::TableNextColumn();
				const std::string timestamp = entry.TimestampString();
				ImGui::TextUnformatted(timestamp.c_str());

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(entry.message.c_str());
			}

			if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f)
				ImGui::SetScrollHereY(1.0f);

			ImGui::EndTable();
		}
	}

	void LoggingPanel::requestExport()
	{
		if (m_EventBus == nullptr)
		{
			m_LastExportStatus = "Export failed: EventBus unavailable";
			return;
		}

		AppEvents::Logs::ExportRequested request;
		request.entries = m_Entries;
		request.targetPath = m_ExportPath;
		m_EventBus->Queue(request);
		m_LastExportStatus = "Export requested: " + m_ExportPath.String();
	}

	void LoggingPanel::onExportCompleted(const AppEvents::Logs::ExportCompleted &event)
	{
		if (event.targetPath.String() != m_ExportPath.String())
			return;
		m_LastExportStatus = "Exported: " + event.targetPath.String();
	}

	void LoggingPanel::onExportFailed(const AppEvents::Logs::ExportFailed &event)
	{
		if (event.targetPath.String() != m_ExportPath.String())
			return;
		m_LastExportStatus = "Export failed: " + event.error;
	}

	bool LoggingPanel::isCategoryEnabled(LogCategory category) const
	{
		if (const auto it = m_ShowCategory.find(category); it != m_ShowCategory.end())
			return it->second;
		return false;
	}

	bool LoggingPanel::isSeverityEnabled(LogLevel level) const
	{
		const auto index = static_cast<std::size_t>(level);
		return index < m_ShowLevel.size() && m_ShowLevel[index];
	}

	const char *LoggingPanel::severityIcon(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace:
				return ICON_FA_BOLT;
			case LogLevel::Debug:
				return ICON_FA_BUG;
			case LogLevel::Info:
				return ICON_FA_CIRCLE_INFO;
			case LogLevel::Warn:
				return ICON_FA_TRIANGLE_EXCLAMATION;
			case LogLevel::Error:
				return ICON_FA_CIRCLE_XMARK;
			case LogLevel::Critical:
				return ICON_FA_SKULL;
			case LogLevel::Count:
				break;
		}

		return ICON_FA_CIRCLE_INFO;
	}

	const char *LoggingPanel::categoryIcon(LogCategory category)
	{
		switch (category)
		{
			case LogCategory::JobSystem:
				return ICON_FA_GEARS;
			case LogCategory::UI:
				return ICON_FA_DESKTOP;
			case LogCategory::Import:
			case LogCategory::Export:
			case LogCategory::Project:
				return ICON_FA_FOLDER_OPEN;
			case LogCategory::Parsing:
				return ICON_FA_CODE;
			case LogCategory::Rendering:
				return ICON_FA_PALETTE;
			case LogCategory::Capability:
				return ICON_FA_KEY;
			case LogCategory::Config:
				return ICON_FA_SLIDERS;
			case LogCategory::Notification:
				return ICON_FA_BELL;
			case LogCategory::Scripting:
				return ICON_FA_SCROLL;
			case LogCategory::General:
			case LogCategory::Count:
				break;
		}

		return ICON_FA_CIRCLE_INFO;
	}
} // namespace DefectStudio
