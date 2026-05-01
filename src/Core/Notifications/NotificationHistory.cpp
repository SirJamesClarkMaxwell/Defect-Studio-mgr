#include "Core/dspch.hpp"

#include "Core/Notifications/NotificationHistory.hpp"

#include <algorithm>

namespace DefectStudio
{
	void NotificationHistory::Append(Notification notification)
	{
		m_Entries.push_back(std::move(notification));
	}

	void NotificationHistory::Clear()
	{
		m_Entries.clear();
	}

	const std::vector<Notification> &NotificationHistory::GetAll() const
	{
		return m_Entries;
	}

	std::vector<Notification> NotificationHistory::FilterBySeverity(NotificationSeverity severity) const
	{
		std::vector<Notification> filtered;
		for (const auto &entry : m_Entries)
		{
			if (entry.severity == severity)
				filtered.push_back(entry);
		}
		return filtered;
	}

	std::vector<Notification> NotificationHistory::FilterByCategory(NotificationCategory category) const
	{
		std::vector<Notification> filtered;
		for (const auto &entry : m_Entries)
		{
			if (entry.category == category)
				filtered.push_back(entry);
		}
		return filtered;
	}

	std::vector<Notification> NotificationHistory::FilterBySource(const std::string &source) const
	{
		std::vector<Notification> filtered;
		for (const auto &entry : m_Entries)
		{
			if (entry.source == source)
				filtered.push_back(entry);
		}
		return filtered;
	}

	std::vector<Notification> NotificationHistory::FilterByTimeRange(Time::TimePoint start, Time::TimePoint end) const
	{
		std::vector<Notification> filtered;
		for (const auto &entry : m_Entries)
		{
			if (entry.timestamp >= start && entry.timestamp <= end)
				filtered.push_back(entry);
		}
		return filtered;
	}
} // namespace DefectStudio