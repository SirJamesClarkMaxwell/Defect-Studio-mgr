#include "Core/dspch.hpp"

#include <fstream>

#include <imgui.h>

#include "IconsFontAwesome6.h"

#include "Presentation/Panels/LoggingPanel.hpp"

namespace DefectStudio
{
	namespace
	{
		std::string csvEscape(const std::string &value)
		{
			std::string escaped;
			escaped.reserve(value.size() + 8);
			escaped.push_back('"');
			for (const char ch : value)
			{
				if (ch == '"')
					escaped += "\"\"";
				else
					escaped.push_back(ch);
			}
			escaped.push_back('"');
			return escaped;
		}

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
			}

			return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
		}

		std::size_t severityIndex(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace:
					return 0;
				case LogLevel::Debug:
					return 1;
				case LogLevel::Info:
					return 2;
				case LogLevel::Warn:
					return 3;
				case LogLevel::Error:
					return 4;
				case LogLevel::Critical:
					return 5;
			}

			return 2;
		}
	} // namespace

	LoggingPanel::LoggingPanel(Ref<LogRegistry> logRegistry, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_LogRegistry(std::move(logRegistry))
	{
		rebuildFromRegistry();
	}

	LoggingPanel::LoggingPanel(const LoggingPanel &other)
		: IPanel(other.GetTitle(), other.IsVisible()),
		  m_LogRegistry(other.m_LogRegistry),
		  m_Entries(other.m_Entries),
		  m_CategoryEnabled(other.m_CategoryEnabled),
		  m_SeverityEnabled(other.m_SeverityEnabled),
		  m_Paused(other.m_Paused),
		  m_AutoScroll(other.m_AutoScroll),
		  m_LastExportStatus(other.m_LastExportStatus)
	{
	}

	Ref<IPanel> LoggingPanel::Clone() const
	{
		return CreateRef<LoggingPanel>(*this);
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
			const bool ok = exportEntriesToCsv("logs/event-log-export.csv");
			m_LastExportStatus = ok ? "Exported: logs/event-log-export.csv" : "Export failed";
		}

		ImGui::SameLine();
		ImGui::TextUnformatted("Sev:");
		ImGui::SameLine();
		ImGui::Checkbox("Trace", &m_SeverityEnabled[0]);
		ImGui::SameLine();
		ImGui::Checkbox("Debug", &m_SeverityEnabled[1]);
		ImGui::SameLine();
		ImGui::Checkbox("Info", &m_SeverityEnabled[2]);
		ImGui::SameLine();
		ImGui::Checkbox("Warn", &m_SeverityEnabled[3]);
		ImGui::SameLine();
		ImGui::Checkbox("Error", &m_SeverityEnabled[4]);
		ImGui::SameLine();
		ImGui::Checkbox("Critical", &m_SeverityEnabled[5]);

		ImGui::SameLine();
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
			bool enabled = m_CategoryEnabled[static_cast<std::size_t>(category)];
			std::string label = std::string(categoryIcon(category)) + " " + ToString(category);
			if (ImGui::Checkbox(label.c_str(), &enabled))
				m_CategoryEnabled[static_cast<std::size_t>(category)] = enabled;
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

	bool LoggingPanel::exportEntriesToCsv(const std::string &path) const
	{
		std::ofstream out(path, std::ios::out | std::ios::trunc);
		if (!out)
			return false;

		out << "timestamp,category,severity,origin,logger,message,formatted\n";
		for (const LogEntry &entry : m_Entries)
		{
			out << csvEscape(entry.TimestampString()) << ','
			    << csvEscape(ToString(entry.category)) << ','
			    << csvEscape(severityLabel(entry.level)) << ','
			    << csvEscape(entry.Origin()) << ','
			    << csvEscape(entry.loggerName) << ','
			    << csvEscape(entry.message) << ','
			    << csvEscape(entry.ToString()) << '\n';
		}

		return true;
	}

	bool LoggingPanel::isCategoryEnabled(LogCategory category) const
	{
		const auto index = static_cast<std::size_t>(category);
		return index < m_CategoryEnabled.size() && m_CategoryEnabled[index];
	}

	bool LoggingPanel::isSeverityEnabled(LogLevel level) const
	{
		const auto index = severityIndex(level);
		return index < m_SeverityEnabled.size() && m_SeverityEnabled[index];
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
