#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"

namespace DefectStudio::CoreEvents
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

	struct ProjectSaveAsRequested final : public BusEvent
	{
	};

	struct StructureFileSaveRequested final : public BusEvent
	{
	};
} // namespace DefectStudio::CoreEvents
