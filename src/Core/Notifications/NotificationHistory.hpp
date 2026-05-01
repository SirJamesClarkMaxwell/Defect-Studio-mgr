#pragma once

#include <string>
#include <vector>

#include "Core/Notifications/Notification.hpp"
#include "Core/Utils/Time.hpp"

namespace DefectStudio
{
	class NotificationHistory
	{
	public:
		void Append(Notification notification);
		void Clear();

		[[nodiscard]] const std::vector<Notification> &GetAll() const;
		[[nodiscard]] std::vector<Notification> FilterBySeverity(NotificationSeverity severity) const;
		[[nodiscard]] std::vector<Notification> FilterByCategory(NotificationCategory category) const;
		[[nodiscard]] std::vector<Notification> FilterBySource(const std::string &source) const;
		[[nodiscard]] std::vector<Notification> FilterByTimeRange(Time::TimePoint start, Time::TimePoint end) const;

	private:
		std::vector<Notification> m_Entries;
	};
} // namespace DefectStudio