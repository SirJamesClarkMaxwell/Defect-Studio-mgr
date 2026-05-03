#pragma once

#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/SubscriptionHandle.hpp"
#include "Core/Notifications/Notification.hpp"

namespace DefectStudio
{
	struct NotificationRequestedEvent;

	class Notifier
	{
	public:
		explicit Notifier(Ref<EventBus> eventBus = {});

		void BindEventBus(Ref<EventBus> eventBus);
		void UnbindEventBus();

		void Notify(Notification notification);
		void Info(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);
		void Warning(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);
		void Error(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);
		void Critical(std::string title, std::string message, NotificationCategory category = NotificationCategory::General);

		[[nodiscard]] const std::vector<Notification> &GetHistory() const;
		void ClearHistory();

	private:
		void onNotificationRequested(const NotificationRequestedEvent &event);
		void appendAndPublish(Notification notification);

	private:
		Ref<EventBus> m_EventBus;
		SubscriptionHandle m_NotificationRequestSubscription;
		std::vector<Notification> m_History;
	};
} // namespace DefectStudio
