#include "Core/dspch.hpp"

#include "Core/Diagnostics/StructuredError.hpp"

#include "Core/Notifications/Notification.hpp"

namespace DefectStudio
{
	namespace
	{
		NotificationSeverity toNotificationSeverity(Severity severity)
		{
			switch (severity)
			{
			case Severity::Info:
				return NotificationSeverity::Info;
			case Severity::Warning:
				return NotificationSeverity::Warn;
			case Severity::Error:
				return NotificationSeverity::Error;
			case Severity::Fatal:
				return NotificationSeverity::Critical;
			}
			return NotificationSeverity::Error;
		}

		NotificationCategory toNotificationCategory(ErrorCategory category)
		{
			switch (category)
			{
			case ErrorCategory::Config:
			case ErrorCategory::Configuration:
				return NotificationCategory::Config;
			case ErrorCategory::Job:
			case ErrorCategory::Cancelled:
				return NotificationCategory::JobSystem;
			case ErrorCategory::IO:
				return NotificationCategory::General;
			case ErrorCategory::Python:
				return NotificationCategory::Scripting;
			case ErrorCategory::Validation:
				return NotificationCategory::General;
			case ErrorCategory::Capability:
				return NotificationCategory::Capability;
			case ErrorCategory::Runtime:
			case ErrorCategory::Internal:
			default:
				return NotificationCategory::General;
			}
		}
	}

	Notification ToNotification(const StructuredError &error)
	{
		Notification notification;
		notification.severity = toNotificationSeverity(error.severity);
		notification.category = toNotificationCategory(error.category);
		notification.title = error.code.empty() ? std::string("Error") : error.code;
		notification.message = error.userMessage;
		notification.source = error.source;
		notification.timestamp = Time::Now();
		notification.timeoutMs = error.severity == Severity::Warning ? 8000u : 12000u;
		return notification;
	}
} // namespace DefectStudio
