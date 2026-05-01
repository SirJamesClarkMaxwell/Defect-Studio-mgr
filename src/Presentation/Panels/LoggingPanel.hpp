#pragma once

#include <array>
#include <string>
#include <vector>

#include "Core/Logging/LogRegistry.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	enum class LoggedEventType : int
	{
		JobSystem = 0,
		UI,
		IO,
		Parsing,
		Rendering,
		Capability,
		Notification,
		Count
	};
	
	struct LoggedEntry
	{
		Notification notification;
		std::string originLabel;
		LoggedEventType type = LoggedEventType::Notification;
		std::string timestampLabel;
	};
	
	class LoggingPanel final : public IPanel
	{
	public:

		explicit LoggingPanel(std::string title = "Logging Panel", bool visibleByDefault = true);
		LoggingPanel(const LoggingPanel &other);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:

		void appendEntry(Notification notification, std::string originLabel = {});
		void rebuildFromRegistry();
		void renderControls();
		void renderTypeFilters();
		void renderEntries();
		[[nodiscard]] bool exportEntriesToCsv(const std::string &path) const;

		[[nodiscard]] bool isTypeEnabled(LoggedEventType type) const;
		[[nodiscard]] static LoggedEventType categoryToType(NotificationCategory category);
		[[nodiscard]] static const char *typeLabel(LoggedEventType type);
		[[nodiscard]] static const char *severityLabel(NotificationSeverity severity);
		[[nodiscard]] static const char *severityIcon(NotificationSeverity severity);
		[[nodiscard]] static const char *typeIcon(LoggedEventType type);
		[[nodiscard]] static std::string formatTimestamp(Time::TimePoint timestamp);
		[[nodiscard]] bool isSeverityEnabled(NotificationSeverity severity) const;

	private:
		std::vector<LoggedEntry> m_Entries;
		std::array<bool, static_cast<std::size_t>(LoggedEventType::Count)> m_TypeEnabled = {
			true, true, true, true, true, true, true
		};
		// Severity filters: Fatal, Error, Info, Trace, Debug
		std::array<bool, 5> m_SeverityEnabled = {true, true, true, true, true};
		bool m_Paused = false;
		bool m_AutoScroll = true;
		std::string m_LastExportStatus;
	};
} // namespace DefectStudio
