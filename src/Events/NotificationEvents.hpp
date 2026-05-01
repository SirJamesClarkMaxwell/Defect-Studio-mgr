#pragma once

#include <utility>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Notifications/Notification.hpp"

namespace DefectStudio
{
	struct NotificationEvent final : public BusEvent
	{
		explicit NotificationEvent(Notification newNotification)
			: notification(std::move(newNotification))
		{
		}

		Notification notification;
	};
} // namespace DefectStudio