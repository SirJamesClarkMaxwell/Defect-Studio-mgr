#pragma once

#include <functional>
#include <cstddef>
#include <unordered_map>
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

		using NotificationHandler = std::function<void(const Notification &)>;

		[[nodiscard]] std::size_t RegisterListener(NotificationHandler listener);
		void RemoveListener(std::size_t listenerId);
		void ClearListeners();

		[[nodiscard]] const std::vector<Notification> &GetNotifications() const;

	private:
		void onNotification(const Notification &notification);

	private:
		Ref<EventBus> m_EventBus;
		SubscriptionHandle m_Subscription;
		std::unordered_map<std::size_t, NotificationHandler> m_Listeners;
		std::size_t m_NextListenerId = 1;
		NotificationHistory m_History;
	};
} // namespace DefectStudio
