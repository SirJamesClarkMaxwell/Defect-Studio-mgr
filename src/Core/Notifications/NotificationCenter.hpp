#pragma once

#include <functional>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Notifications/NotificationHistory.hpp"

namespace DefectStudio
{
	class NotificationCenter
	{
	public:
		explicit NotificationCenter(Ref<EventBus> eventBus = {});
		~NotificationCenter();

		NotificationCenter(const NotificationCenter &) = delete;
		NotificationCenter &operator=(const NotificationCenter &) = delete;
		NotificationCenter(NotificationCenter &&) = delete;
		NotificationCenter &operator=(NotificationCenter &&) = delete;

		void BindEventBus(Ref<EventBus> eventBus);
		void UnbindEventBus();

		using Listener = std::function<void(const Notification &)>;

		void RegisterListener(Listener listener);
		void ClearListeners();

		[[nodiscard]] const std::vector<Notification> &GetNotifications() const;

	private:
		void onNotification(const Notification &notification);

	private:
		Ref<EventBus> m_EventBus;
		SubscriptionHandle m_Subscription;
		std::vector<Listener> m_Listeners;
		NotificationHistory m_History;
	};
} // namespace DefectStudio