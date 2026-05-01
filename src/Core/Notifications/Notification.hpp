#pragma once

#include <cstdint>
#include <string>

#include "Core/Utils/Time.hpp"

namespace DefectStudio
{
	enum class NotificationSeverity
	{
		Info,
		Warn,
		Error,
		Critical
	};

	enum class NotificationCategory
	{
		General,
		Project,
		Import,
		Export,
		JobSystem,
		Parsing,
		UI,
		Scripting,
		Rendering,
		Capability,
		Config
	};

	struct Notification
	{
		NotificationSeverity severity = NotificationSeverity::Info;
		NotificationCategory category = NotificationCategory::General;
		std::string title;
		std::string message;
		std::string source;
		Time::TimePoint timestamp{};
		std::uint32_t timeoutMs = 4000;
		bool pinned = false;
	};
} // namespace DefectStudio
