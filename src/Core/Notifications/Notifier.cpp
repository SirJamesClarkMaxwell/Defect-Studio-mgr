#include "Core/dspch.hpp"

#include "Core/Notifications/Notifier.hpp"

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Logger.hpp"
#include "Events/NotificationEvents.hpp"

namespace DefectStudio
{
	Notifier::Notifier(Ref<EventBus> eventBus)
	{
		BindEventBus(std::move(eventBus));
	}

	void Notifier::BindEventBus(Ref<EventBus> eventBus)
	{
		UnbindEventBus();
		m_EventBus = std::move(eventBus);
		if (!m_EventBus)
			return;

		m_NotificationRequestSubscription = m_EventBus->Subscribe<NotificationRequestedEvent>(
			[this](const NotificationRequestedEvent &event) {
				onNotificationRequested(event);
			});
	}

	void Notifier::UnbindEventBus()
	{
		m_NotificationRequestSubscription.Reset();
		m_EventBus.reset();
	}

	void Notifier::Notify(Notification notification)
	{
		appendAndPublish(std::move(notification));
	}

	void Notifier::Info(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Info, category, std::move(title), std::move(message), "Notifier", Time::Now(), 4000, false});
	}

	void Notifier::Warning(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Warn, category, std::move(title), std::move(message), "Notifier", Time::Now(), 8000, false});
	}

	void Notifier::Error(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Error, category, std::move(title), std::move(message), "Notifier", Time::Now(), 12000, false});
	}

	void Notifier::Critical(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Critical, category, std::move(title), std::move(message), "Notifier", Time::Now(), 12000, true});
	}

	const std::vector<Notification> &Notifier::GetHistory() const
	{
		return m_History;
	}

	void Notifier::ClearHistory()
	{
		m_History.clear();
	}

	void Notifier::onNotificationRequested(const NotificationRequestedEvent &event)
	{
		Notify(event.notification);
	}

	void Notifier::appendAndPublish(Notification notification)
	{
		if (notification.timestamp == Time::TimePoint{})
			notification.timestamp = Time::Now();

		m_History.push_back(notification);

		const std::string prefix = notification.title.empty() ? "Notification" : notification.title;
		switch (notification.severity)
		{
			case NotificationSeverity::Info:
				DS_LOG_INFO("Notification [{}] {}", prefix, notification.message);
				break;
			case NotificationSeverity::Warn:
				DS_LOG_WARN("Notification [{}] {}", prefix, notification.message);
				break;
			case NotificationSeverity::Error:
				DS_LOG_ERROR("Notification [{}] {}", prefix, notification.message);
				break;
			case NotificationSeverity::Critical:
				DS_LOG_CRITICAL("Notification [{}] {}", prefix, notification.message);
				break;
		}

		if (m_EventBus)
			m_EventBus->Queue(NotificationEvent{std::move(notification)});
	}
} // namespace DefectStudio
