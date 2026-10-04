#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/RendererWindowState.hpp"

// Scene text that follows an atom or a vacancy (RendererWindowState::FreeLabel::anchorAtom /
// anchorVacancy, at most one set): worldPosition = anchor position + anchorOffset. Refreshed every
// frame before the viewport interaction chain. A stale anchor index (the atom or vacancy is gone)
// leaves worldPosition where it was, like the arrow and orbital anchors.
//
// Moving an anchored label with G or the gizmo rewrites anchorOffset, not the anchor, so the label
// keeps following its atom from the new place (SceneTransform's free-label apply/restore).
namespace DefectStudio
{
	// The anchor's current world position, nullopt when the label is free or the anchor is stale.
	[[nodiscard]] std::optional<glm::vec3> ResolveFreeLabelAnchor(
		const RendererWindowState &window, const RendererWindowState::FreeLabel &label);

	void RefreshAnchoredFreeLabels(RendererWindowState &window);

	// One label per vacancy in `vacancies` (indices into structure.vacancies; empty = every vacancy)
	// that has no label anchored to it yet. Text = the vacancy's label (e.g. "V_B", which the TeX
	// markup draws with a subscript), anchored to the vacancy with a zero offset, so it sits in the
	// marker. Returns the new labels' ids in vacancy order; the caller pushes the undo snapshot and
	// selects them.
	std::vector<SceneObjectId> AddVacancyLabels(RendererWindowState &window, const std::vector<std::size_t> &vacancies);
} // namespace DefectStudio
