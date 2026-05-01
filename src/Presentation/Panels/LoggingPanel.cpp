#include "Core/dspch.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>

#include <imgui.h>

#include "IconsFontAwesome6.h"

#include "Core/Logging/LogRegistry.hpp"
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

		std::string originLabelFor(const LogRegistry::Entry &entry)
		{
			if (!entry.notification.source.empty())
				return entry.notification.source;

			if (!entry.threadLabel.empty())
				return entry.threadLabel;

			return "-";
		}
	} // namespace

	LoggingPanel::LoggingPanel(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
		rebuildFromRegistry();
	}

	LoggingPanel::LoggingPanel(const LoggingPanel &other)
		: IPanel(other.GetTitle(), other.IsVisible()),
		  m_Entries(other.m_Entries),
		  m_TypeEnabled(other.m_TypeEnabled),
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
			renderTypeFilters();
		ImGui::EndChild();

		ImGui::SameLine();
		if (ImGui::BeginChild("LoggingEntries", ImVec2(0.0f, 0.0f), true))
			renderEntries();
		ImGui::EndChild();

		ImGui::End();
	}

	void LoggingPanel::appendEntry(Notification notification, std::string originLabel)
	{
		LoggedEntry entry;
		entry.type = categoryToType(notification.category);
		entry.timestampLabel = formatTimestamp(notification.timestamp);
		entry.originLabel = originLabel.empty() ? notification.source : std::move(originLabel);
		entry.notification = std::move(notification);
		m_Entries.push_back(std::move(entry));

		constexpr std::size_t MaxEntries = 2000;
		if (m_Entries.size() > MaxEntries)
			m_Entries.erase(m_Entries.begin(), m_Entries.begin() + static_cast<std::ptrdiff_t>(m_Entries.size() - MaxEntries));
	}

	void LoggingPanel::rebuildFromRegistry()
	{
		m_Entries.clear();
		const auto snapshot = LogRegistry::Get().Snapshot();
		m_Entries.reserve(snapshot.size());
		for (const auto &entry : snapshot)
			appendEntry(entry.notification, originLabelFor(entry));
	}

	void LoggingPanel::renderControls()
	{
		if (ImGui::Button(m_Paused ? ICON_FA_PLAY " Resume" : ICON_FA_PAUSE " Pause"))
			m_Paused = !m_Paused;

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TRASH " Clear"))
		{
			LogRegistry::Get().Clear();
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
		ImGui::Checkbox("Fatal", &m_SeverityEnabled[0]);
		ImGui::SameLine();
		ImGui::Checkbox("Error", &m_SeverityEnabled[1]);
		ImGui::SameLine();
		ImGui::Checkbox("Info", &m_SeverityEnabled[2]);
		ImGui::SameLine();
		ImGui::Checkbox("Trace", &m_SeverityEnabled[3]);
		ImGui::SameLine();
		ImGui::Checkbox("Debug", &m_SeverityEnabled[4]);

		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

		ImGui::SameLine();
		ImGui::TextDisabled("Visible entries: %zu", m_Entries.size());

		if (!m_LastExportStatus.empty())
			ImGui::TextUnformatted(m_LastExportStatus.c_str());
	}

	void LoggingPanel::renderTypeFilters()
	{
		ImGui::TextUnformatted("Event types");
		ImGui::Separator();

		for (int i = 0; i < static_cast<int>(LoggedEventType::Count); ++i)
		{
			const auto type = static_cast<LoggedEventType>(i);
			bool enabled = m_TypeEnabled[static_cast<std::size_t>(type)];
			std::string label = std::string(typeIcon(type)) + " " + typeLabel(type);
			if (ImGui::Checkbox(label.c_str(), &enabled))
				m_TypeEnabled[static_cast<std::size_t>(type)] = enabled;
		}
	}

	void LoggingPanel::renderEntries()
	{
		if (ImGui::BeginTable("EventLogTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("Origin", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (auto it = m_Entries.rbegin(); it != m_Entries.rend(); ++it)
			{
				const LoggedEntry &entry = *it;
				if (!isTypeEnabled(entry.type))
					continue;
				if (!isSeverityEnabled(entry.notification.severity))
					continue;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImVec4 color;
				switch (entry.notification.severity)
				{
				case NotificationSeverity::Fatal: color = ImVec4(1.0f, 0.20f, 0.20f, 1.0f); break;
				case NotificationSeverity::Error: color = ImVec4(0.95f, 0.2f, 0.2f, 1.0f); break;
				case NotificationSeverity::Warning: color = ImVec4(0.95f, 0.7f, 0.2f, 1.0f); break;
				case NotificationSeverity::Info: color = ImVec4(0.4f, 0.9f, 0.4f, 1.0f); break;
				case NotificationSeverity::Trace: color = ImVec4(0.55f, 0.75f, 1.0f, 1.0f); break;
				case NotificationSeverity::Debug: color = ImVec4(0.6f, 0.6f, 0.9f, 1.0f); break;
				case NotificationSeverity::Success: color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); break;
				default: color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); break;
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::Text("%s %s", severityIcon(entry.notification.severity), severityLabel(entry.notification.severity));
				ImGui::PopStyleColor();

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(typeLabel(entry.type));

				ImGui::TableNextColumn();
				// Origin column
				ImGui::TextUnformatted(entry.originLabel.empty() ? "-" : entry.originLabel.c_str());

				ImGui::TableNextColumn();
				// Time column
				ImGui::TextUnformatted(entry.timestampLabel.c_str());

				ImGui::TableNextColumn();
				// Summary column
				if (entry.notification.title.empty())
					ImGui::TextUnformatted(entry.notification.message.c_str());
				else
					ImGui::Text("%s: %s", entry.notification.title.c_str(), entry.notification.message.c_str());
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

		out << "timestamp,type,severity,title,message,source\n";
		for (const LoggedEntry &entry : m_Entries)
		{
			out << csvEscape(entry.timestampLabel) << ','
			    << csvEscape(typeLabel(entry.type)) << ','
			    << csvEscape(severityLabel(entry.notification.severity)) << ','
			    << csvEscape(entry.notification.title) << ','
			    << csvEscape(entry.notification.message) << ','
			    << csvEscape(entry.originLabel) << '\n';
		}

		return true;
	}

	bool LoggingPanel::isTypeEnabled(LoggedEventType type) const
	{
		return m_TypeEnabled[static_cast<std::size_t>(type)];
	}

	bool LoggingPanel::isSeverityEnabled(NotificationSeverity severity) const
	{
		switch (severity)
		{
		case NotificationSeverity::Fatal:
			return m_SeverityEnabled[0];
		case NotificationSeverity::Error:
			return m_SeverityEnabled[1];
		case NotificationSeverity::Warning:
			return m_SeverityEnabled[1];
		case NotificationSeverity::Info:
		case NotificationSeverity::Success:
			return m_SeverityEnabled[2];
		case NotificationSeverity::Trace:
			return m_SeverityEnabled[3];
		case NotificationSeverity::Debug:
			return m_SeverityEnabled[4];
		default:
			return true;
		}
	}

	LoggingPanel::LoggedEventType LoggingPanel::categoryToType(NotificationCategory category)
	{
		switch (category)
		{
		case NotificationCategory::JobSystem:
			return LoggedEventType::JobSystem;
		case NotificationCategory::UI:
			return LoggedEventType::UI;
		case NotificationCategory::Import:
		case NotificationCategory::Export:
			return LoggedEventType::IO;
		case NotificationCategory::Parsing:
			return LoggedEventType::Parsing;
		case NotificationCategory::Rendering:
			return LoggedEventType::Rendering;
		case NotificationCategory::Capability:
			return LoggedEventType::Capability;
		default:
			return LoggedEventType::Notification;
		}
	}

	const char *LoggingPanel::typeLabel(LoggedEventType type)
	{
		switch (type)
		{
		case LoggedEventType::JobSystem:
			return "JobSystem";
		case LoggedEventType::UI:
			return "UI";
		case LoggedEventType::IO:
			return "IO";
		case LoggedEventType::Parsing:
			return "Parsing";
		case LoggedEventType::Rendering:
			return "Rendering";
		case LoggedEventType::Capability:
			return "Capability";
		case LoggedEventType::Notification:
			return "Notification";
		case LoggedEventType::Count:
			break;
		}

		return "Unknown";
	}

	const char *LoggingPanel::severityLabel(NotificationSeverity severity)
	{
		switch (severity)
		{
		case NotificationSeverity::Fatal:
			return "Fatal";
		case NotificationSeverity::Trace:
			return "Trace";
		case NotificationSeverity::Debug:
			return "Debug";
		case NotificationSeverity::Info:
			return "Info";
		case NotificationSeverity::Success:
			return "Success";
		case NotificationSeverity::Warning:
			return "Warning";
		case NotificationSeverity::Error:
			return "Error";
		}

		return "Unknown";
	}

	const char *LoggingPanel::severityIcon(NotificationSeverity severity)
	{
		switch (severity)
		{
		case NotificationSeverity::Fatal:
			return ICON_FA_SKULL;
		case NotificationSeverity::Trace:
			return ICON_FA_BOLT;
		case NotificationSeverity::Debug:
			return ICON_FA_BUG;
		case NotificationSeverity::Info:
			return ICON_FA_CIRCLE_INFO;
		case NotificationSeverity::Success:
			return ICON_FA_CIRCLE_CHECK;
		case NotificationSeverity::Warning:
			return ICON_FA_TRIANGLE_EXCLAMATION;
		case NotificationSeverity::Error:
			return ICON_FA_CIRCLE_XMARK;
		}

		return ICON_FA_BELL;
	}

	const char *LoggingPanel::typeIcon(LoggedEventType type)
	{
		switch (type)
		{
		case LoggedEventType::JobSystem:
			return ICON_FA_GEARS;
		case LoggedEventType::UI:
			return ICON_FA_DESKTOP;
		case LoggedEventType::IO:
			return ICON_FA_FOLDER_OPEN;
		case LoggedEventType::Parsing:
			return ICON_FA_CODE;
		case LoggedEventType::Rendering:
			return ICON_FA_PALETTE;
		case LoggedEventType::Capability:
			return ICON_FA_KEY;
		case LoggedEventType::Notification:
			return ICON_FA_BELL;
		case LoggedEventType::Count:
			break;
		}

		return ICON_FA_BELL;
	}

	std::string LoggingPanel::formatTimestamp(Time::TimePoint timestamp)
	{
		if (timestamp == Time::TimePoint{})
			return "-";

		const std::time_t time = Time::Clock::to_time_t(timestamp);
		std::tm localTime{};
#if defined(_WIN32)
		localtime_s(&localTime, &time);
#else
		localtime_r(&time, &localTime);
#endif

		char buffer[32] = {};
		std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &localTime);
		return std::string(buffer);
	}
} // namespace DefectStudio
