#include "Core/dspch.hpp"

#include "Core/Notifications/Notifier.hpp"

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Logging/LogRegistry.hpp"
#include "Core/Utils/Logger.hpp"
#include <thread>
#include "Events/NotificationEvents.hpp"

namespace DefectStudio
{
	Notifier::Notifier(Ref<EventBus> eventBus)
		: m_EventBus(std::move(eventBus))
	{
	}

	void Notifier::BindEventBus(Ref<EventBus> eventBus)
	{
		m_EventBus = std::move(eventBus);
	}

	void Notifier::UnbindEventBus()
	{
		m_EventBus.reset();
	}

	void Notifier::Notify(Notification notification)
	{
		appendAndPublish(std::move(notification));
	}

	void Notifier::Debug(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Debug, category, std::move(title), std::move(message), "Notifier", Time::Now(), 4000, false});
	}

	void Notifier::Info(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Info, category, std::move(title), std::move(message), "Notifier", Time::Now(), 4000, false});
	}

	void Notifier::Success(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Success, category, std::move(title), std::move(message), "Notifier", Time::Now(), 4000, false});
	}

	void Notifier::Warning(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Warning, category, std::move(title), std::move(message), "Notifier", Time::Now(), 8000, false});
	}

	void Notifier::Error(std::string title, std::string message, NotificationCategory category)
	{
		appendAndPublish(Notification{NotificationSeverity::Error, category, std::move(title), std::move(message), "Notifier", Time::Now(), 12000, false});
	}

	const std::vector<Notification> &Notifier::GetHistory() const
	{
		return m_History;
	}

	void Notifier::ClearHistory()
	{
		m_History.clear();
	}

	void Notifier::appendAndPublish(Notification notification)
	{
		if (notification.timestamp == Time::TimePoint{})
			notification.timestamp = Time::Now();

		m_History.push_back(notification);
		// LogRegistry::Get().Append(LogRegistry::Entry{notification, std::this_thread::get_id(), {}});

		// Log with appropriate severity level
		const std::string prefix = notification.title.empty() ? "Notification" : notification.title;
		switch (notification.severity)
		{
		case NotificationSeverity::Fatal:
			DS_LOG_CRITICAL("Notification [{}] {}", prefix, notification.message);
			break;
		case NotificationSeverity::Trace:
			DS_LOG_TRACE("Notification [{}] {}", prefix, notification.message);
			break;
		case NotificationSeverity::Debug:
			DS_LOG_DEBUG("Notification [{}] {}", prefix, notification.message);
			break;
		case NotificationSeverity::Error:
			DS_LOG_ERROR("Notification [{}] {}", prefix, notification.message);
			break;
		case NotificationSeverity::Warning:
			DS_LOG_WARN("Notification [{}] {}", prefix, notification.message);
			break;
		default: // Info, Success
			DS_LOG_INFO("Notification [{}] {}", prefix, notification.message);
			break;
		}

		if (m_EventBus)
			m_EventBus->Publish(NotificationEvent{std::move(notification)});
	}
} // namespace DefectStudio