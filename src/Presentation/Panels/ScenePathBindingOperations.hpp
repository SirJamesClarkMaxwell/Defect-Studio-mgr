#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Renderer/Path/PathTypes.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// Edit Mode binding actions (PathSystem S14). Every one acts on the ACTIVE node of the window's
	// open Edit Mode session - `pathEdit.ActiveElement()` when it names a node of the edited path -
	// and goes through PathCommands' SetScenePathBinding, so each call is one undo entry and a
	// rejected call changes nothing.
	//
	// Errors (StructuredError::code):
	//   path.edit_node_required             no session, or the active element is not a node
	//   path.binding_atoms_required         Bind... with a selected-atom count other than 1 or 2
	//   path.binding_atom_out_of_range      a selected atom index the window's structure lacks
	//   path.binding_buffer_endpoint_only   a non-zero buffer on an interior node (v2 plan C8)
	//   path.binding_object_required        select exactly one other scene object
	//   path.binding_object_unresolved      the object has no available origin
	//   path.object_origin_targets_path     ObjectOrigin may not target a path (runtime rule)

	// The active node's current binding.
	[[nodiscard]] Result<PathBinding> ResolveActiveScenePathNodeBinding(const RendererWindowState &windowState);

	// Binds the active node to the window's selected atoms (`selectedAtomIndices`): one atom ->
	// CopyPosition, two -> BondMidpoint in selection order. Offset and buffer start at zero, so the
	// node lands exactly on the atom / the bond midpoint.
	[[nodiscard]] Result<void> BindActiveScenePathNodeToSelectedAtoms(RendererWindowState &windowState);

	// Exactly one selected scene object, excluding the path being edited. Shared by the button's
	// availability and the action; validates the live origin and rejects path targets.
	[[nodiscard]] Result<SceneObjectId> ResolveSelectedScenePathBindingObject(const RendererWindowState &windowState);
	[[nodiscard]] Result<void> BindActiveScenePathNodeToSelectedObjectOrigin(RendererWindowState &windowState);

	// Replaces the active node's binding wholesale - the Properties panel's offset / buffer edits.
	// Validates as listed above; a Free binding is accepted and is the same as Detach WITHOUT the
	// position write-back (the node returns to its authored fallback position).
	[[nodiscard]] Result<void> SetActiveScenePathNodeBinding(RendererWindowState &windowState, PathBinding binding);

	// Frees the active node where it currently IS: its resolved world position (live binding
	// context) is written back as its authored position - through the inverse of the path's object
	// transform, because authored positions are local - and the binding becomes Free, in one undo
	// entry. The node does not move on screen. On a node that is already Free this is a successful
	// no-op with no undo entry.
	[[nodiscard]] Result<void> DetachActiveScenePathNodeKeepingPosition(RendererWindowState &windowState);
} // namespace DefectStudio
