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

	// Adds a Manual-origin bond between exactly 2 selected atoms ('J') - undoable. Fails if the
	// selection isn't exactly 2 atoms or the two are already bonded (Auto or Manual).
	[[nodiscard]] Unique<ICommand> CreateConnectSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
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

	// Keyboard nudge (Shift+Arrows) for the current selection - one undoable step per press, moving
	// by a small fixed amount along the focused viewport's own camera-right/camera-up axes (screen-
	// space up/down/left/right, not world axes, so it does the visually expected thing regardless of
	// orbit). screenDirection is (x=right, y=up) in {-1,0,1}. No-op (not an error) if nothing is
	// selected or the focused window has no camera.
	[[nodiscard]] Unique<ICommand> CreateNudgeSelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		glm::vec2 screenDirection);

	// Copies the current selection into an in-process clipboard (shared across every window/command
	// instance - see the anonymous-namespace holder in the .cpp). Read-only, not undoable.
	[[nodiscard]] Unique<ICommand> CreateCopySelectedAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		std::string windowId = {});

	// Inserts the clipboard's atoms (from the most recent Copy) into the target window's structure,
	// offset the same way Duplicate offsets its copies - undoable. Fails if the clipboard is empty.
	[[nodiscard]] Unique<ICommand> CreatePasteAtomsCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		std::string windowId = {});

	// Payload for "renderer.selection.change_type" - species text comes from the context menu's
	// inline input, so (like GizmoTransformPayload) it's passed through CommandContext rather than
	// baked into the factory at registration time.
	struct ChangeAtomTypePayload
	{
		std::string windowId;
		std::string species;
	};

	[[nodiscard]] Unique<ICommand> CreateChangeSelectedAtomTypeCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		ChangeAtomTypePayload payload);
} // namespace DefectStudio
