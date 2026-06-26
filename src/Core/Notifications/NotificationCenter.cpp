#include "Core/dspch.hpp"

#include "Core/Notifications/NotificationCenter.hpp"

#include "Core/Notifications/NotificationEvents.hpp"

namespace DefectStudio
{
	NotificationCenter::NotificationCenter(Ref<EventBus> eventBus)
	{
		BindEventBus(std::move(eventBus));
	}

	NotificationCenter::~NotificationCenter()
	{
		UnbindEventBus();
	}

	void NotificationCenter::BindEventBus(Ref<EventBus> eventBus)
	{
		UnbindEventBus();
		m_EventBus = std::move(eventBus);
		if (!m_EventBus)
			return;

		m_Subscription = m_EventBus->Subscribe<NotificationEvent>([this](const NotificationEvent &event) {
			onNotification(event.notification);
		});
	}

	void NotificationCenter::UnbindEventBus()
	{
		m_Subscription.Reset();
		m_EventBus.reset();
	}

	std::size_t NotificationCenter::RegisterListener(NotificationHandler listener)
	{
		if (!listener)
			return 0;

		const std::size_t id = m_NextListenerId++;
		m_Listeners.emplace(id, std::move(listener));
		return id;
	}

	void NotificationCenter::RemoveListener(std::size_t listenerId)
	{
		m_Listeners.erase(listenerId);
	}

	void NotificationCenter::ClearListeners()
	{
		m_Listeners.clear();
	}

	const std::vector<Notification> &NotificationCenter::GetNotifications() const
	{
		return m_History.GetAll();
	}

	void NotificationCenter::onNotification(const Notification &notification)
	{
		m_History.Append(notification);
		for (const auto &[id, handler] : m_Listeners)
		{
			(void)id;
			if (handler)
				handler(notification);
		}
	}
} // namespace DefectStudio
