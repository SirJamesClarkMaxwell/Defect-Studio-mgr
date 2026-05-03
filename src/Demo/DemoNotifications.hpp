#pragma once

#include <cstdint>
#include <deque>

#include "Core/Notifications/NotificationCenter.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
}

namespace DefectStudio::Demo
{
	class DemoNotificationsPanel
	{
	public:
		explicit DemoNotificationsPanel(Ref<EventBus> eventBus);
		void Render();
		void RenderToasts();

	private:
		void onNotification(const Notification &notification);
		void requestNotification(Notification notification);

		Ref<EventBus> m_EventBus;
		Unique<NotificationCenter> m_NotificationCenter;
		std::deque<Notification> m_PendingToasts;
		std::uint32_t m_NotificationDemoCounter = 0;
	};
} // namespace DefectStudio::Demo
