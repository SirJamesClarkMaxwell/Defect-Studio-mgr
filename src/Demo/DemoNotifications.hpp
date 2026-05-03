#pragma once

#include <cstdint>
#include <deque>

#include "Core/EventSystem/BusEventSystem/SubscriptionHandle.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
	struct NotificationEvent;
	class Notifier;
}

namespace DefectStudio::Demo
{
	class DemoNotificationsPanel
	{
	public:
		DemoNotificationsPanel(Ref<Notifier> notifier, Ref<EventBus> eventBus);
		void Render();
		void RenderToasts();

	private:
		void onNotificationEvent(const NotificationEvent &event);

		Ref<Notifier> m_Notifier;
		Ref<EventBus> m_EventBus;
		SubscriptionHandle m_NotificationSubscription;
		std::deque<Notification> m_PendingToasts;
		std::uint32_t m_NotificationDemoCounter = 0;
	};
} // namespace DefectStudio::Demo
