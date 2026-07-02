#pragma once

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class EventBus;

	void RegisterRendererCommands(CommandRegistry &registry, Ref<EventBus> eventBus);
} // namespace DefectStudio
