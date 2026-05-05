#pragma once

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/JobSystem/JobEvents.hpp"

namespace DefectStudio::AppEvents
{
	struct OpenCommandPaletteRequested final : public BusEvent
	{
	};

	struct ShutdownRequested final : public BusEvent
	{
	};

	struct ProjectSaveRequested final : public BusEvent
	{
	};
} // namespace DefectStudio::AppEvents
