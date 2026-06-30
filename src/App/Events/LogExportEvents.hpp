#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::AppEvents::Logs
{
	struct ExportRequested final : public BusEvent
	{
		std::vector<LogEntry> entries;
		Path targetPath;
	};

	struct ExportCompleted final : public BusEvent
	{
		Path targetPath;
		std::size_t bytes = 0;
	};

	struct ExportFailed final : public BusEvent
	{
		Path targetPath;
		std::string error;
	};
} // namespace DefectStudio::AppEvents::Logs
