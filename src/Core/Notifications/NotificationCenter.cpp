#include "Core/dspch.hpp"

#include "Core/Notifications/NotificationCenter.hpp"

#include "Events/NotificationEvents.hpp"

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

	void NotificationCenter::RegisterListener(Listener listener)
	{
		m_Listeners.push_back(std::move(listener));
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
		for (const auto &listener : m_Listeners)
		{
			if (listener)
				listener(notification);
		}
	}
} // namespace DefectStudio