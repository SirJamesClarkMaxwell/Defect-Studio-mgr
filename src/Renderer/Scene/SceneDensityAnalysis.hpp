#pragma once

#include <optional>
#include <string>

#include <glm/glm.hpp>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// task/85: the non-UI half of a density's localisation analysis - which centre it uses, the
	// cached radial profile around it, and the sxdefectalign spelling of that centre. Renderer layer
	// (no ImGui) so both the N panel and the viewport overlay of the analysis sphere can use it.

	// The user's override if set, else the loaded grid's |value| centroid, else nullopt (nothing
	// loaded yet, or an all-zero grid). Cartesian, Angstrom, same frame as the atoms.
	[[nodiscard]] std::optional<glm::vec3> SceneDensityAnalysisCentre(const RendererWindowState::SceneDensity &density);

	// The same centre in fractional coordinates of the density's own cell, each component wrapped
	// into [0, 1). nullopt without loaded data (no cell to divide by) or a singular cell.
	[[nodiscard]] std::optional<glm::vec3> SceneDensityAnalysisCentreFractional(
		const RendererWindowState::SceneDensity &density);

	// The radial |value| profile around the analysis centre, computed on first use and cached in the
	// density (`radialProfile*`); recomputed only when the grid or the centre changed. nullptr while
	// there is no centre. ~5.8 M points for a 180^3 CHGCAR, a few tens of ms - the panel calls this
	// every frame, which is fine only because of the cache.
	[[nodiscard]] const DensityRadialProfile *SceneDensityRadialProfile(RendererWindowState::SceneDensity &density);

	// Centroid of the selected atoms' positions, for "centre on the selected atoms". nullopt with no
	// atom selected.
	[[nodiscard]] std::optional<glm::vec3> SelectedAtomsCentroid(const RendererWindowState &windowState);

	// "x,y,z" with six decimals, each wrapped into [0, 1) - pasted straight after sxdefectalign's
	// `--center` (with `--relative`), which is how the thesis runs were made.
	[[nodiscard]] std::string FormatSxdefectalignCentre(const glm::vec3 &fractional);
} // namespace DefectStudio
