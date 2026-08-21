#pragma once

#include <string>

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class RendererLayer;

	// windowId empty = the currently focused renderer viewport (RendererLayer::
	// GetFocusedViewportWindowId()). Both mutate the live domain CrystalStructure behind the
	// window (found via RendererStructureData::domainStructureId in DomainLayer's
	// ProjectWorkspace), then rebuild the window's RendererStructureData/ECS scene from it - not
	// the renderer-side data directly, per this repo's domain-owns-truth boundary.
	[[nodiscard]] Unique<ICommand> CreateDeleteSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		std::string windowId = {});

	[[nodiscard]] Unique<ICommand> CreateDuplicateSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		std::string windowId = {});
} // namespace DefectStudio
