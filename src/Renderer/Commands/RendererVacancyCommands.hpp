#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
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
		// When set, `vacancies` is ignored and the new list is the domain's current one passed
		// through this (e.g. a gizmo drag that moves two markers and leaves the rest alone).
		std::function<void(std::vector<VacancySite> &, const CrystalStructure &)> edit;
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

	// The defect frame twin of renderer.vacancy.set: replace CrystalStructure::defectFrame (nullopt
	// removes it), one undo entry, MarkModified + RebuildAndSync exactly as above.
	struct SetDefectFramePayload
	{
		std::string windowId; // empty = the focused viewport window
		std::optional<DefectFrame> frame;
		std::string description = "Set defect axes";
		std::function<void(std::optional<DefectFrame> &, const CrystalStructure &)> edit; // as SetVacanciesPayload::edit
	};

	inline constexpr const char *kSetDefectFrameCommandId = "renderer.defect_frame.set";
	inline constexpr const char *kSetDefectFramePayloadKey = "defect_frame.set_payload";

	[[nodiscard]] Unique<ICommand> CreateSetDefectFrameCommand(
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		SetDefectFramePayload payload);
	// Uses the existing SetVacancies command inside H/Alt+H's shared visibility undo group.
	void BindRendererVacancyVisibilityEditor(WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer, AtomStyleTable atomStyleTable);
} // namespace DefectStudio
