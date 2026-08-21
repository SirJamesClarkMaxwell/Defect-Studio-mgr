#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

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

	// Result of a completed ImGuizmo drag (RendererPanel::renderTransformGizmo), passed through
	// CommandContext to "renderer.gizmo.commit_transform" - atomIndices[i] moves to
	// afterPositions[i] (cartesian). The live drag itself mutates RendererStructureData directly
	// for immediate visual feedback; this only runs once, on mouse release, to commit the result
	// to the domain structure as a single undoable step.
	struct GizmoTransformPayload
	{
		std::string windowId;
		std::vector<std::size_t> atomIndices;
		std::vector<glm::vec3> afterPositions;
		std::string description = "Transform selected atoms";
	};

	[[nodiscard]] Unique<ICommand> CreateTransformSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		GizmoTransformPayload payload);
} // namespace DefectStudio
