#pragma once

#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/CrystalPrimitives.hpp"
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class RendererLayer;

	// task/51: every vacancy edit - add, remove, relabel - is "replace the structure's vacancy list
	// with this one". One command, one undo entry, and the caller (Presentation) computes the new
	// list with MakeVacancySite / a plain erase, so the command needs no per-operation variants.
	//
	// Passed through CommandContext under kSetVacanciesPayloadKey, exactly as GizmoTransformPayload
	// is for "renderer.gizmo.commit_transform".
	struct SetVacanciesPayload
	{
		// Empty = the focused viewport window (ResolveAtomEditTarget's rule).
		std::string windowId;
		std::vector<VacancySite> vacancies;
		std::string description = "Edit vacancies";
	};

	inline constexpr const char *kSetVacanciesCommandId = "renderer.vacancy.set";
	inline constexpr const char *kSetVacanciesPayloadKey = "vacancy.set_payload";

	// Execute: remember the domain structure's current `vacancies`, assign payload.vacancies, mark
	// the structure modified (MarkStructureFileModified is NOT needed - the POSCAR does not change;
	// MarkModified is, so the project saves the vacancy sidecar), and rebuild the window's
	// RendererStructureData from the domain structure the way the atom-edit commands do
	// (RebuildAndSync), so the markers update. Undo: assign the remembered list back and rebuild.
	// Errors: the ResolveAtomEditTarget errors, unchanged.
	[[nodiscard]] Unique<ICommand> CreateSetVacanciesCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		SetVacanciesPayload payload);
} // namespace DefectStudio
