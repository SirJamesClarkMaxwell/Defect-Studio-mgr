#pragma once

#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notification.hpp"

namespace DefectStudio
{
	class Notifier
	{
	public:
		explicit Notifier(Ref<EventBus> eventBus = {});

		void BindEventBus(Ref<EventBus> eventBus);
		void UnbindEventBus();

		void Notify(Notification notification);
		void Debug(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);
		void Info(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);
		void Success(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);
		void Warning(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);
		void Error(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);

		[[nodiscard]] const std::vector<Notification> &GetHistory() const;
		void ClearHistory();

	private:
		void appendAndPublish(Notification notification);

	private:
		Ref<EventBus> m_EventBus;
		std::vector<Notification> m_History;
	};
} // namespace DefectStudio