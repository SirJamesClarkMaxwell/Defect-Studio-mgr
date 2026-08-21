#pragma once

#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class EventBus;
	class DomainLayer;
	class RendererLayer;

	// atomStyleTable/elementPropertiesTable are needed by the atom-edit commands (delete/
	// duplicate), which rebuild RendererStructureData after mutating the domain structure - same
	// tables RendererRuntimeOpenCoordinator already carries for the same purpose.
	void RegisterRendererCommands(
		CommandRegistry &registry,
		Ref<EventBus> eventBus,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable);
} // namespace DefectStudio
