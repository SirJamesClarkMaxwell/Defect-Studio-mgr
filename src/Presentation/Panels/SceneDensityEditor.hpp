#pragma once

#include <string>
#include <vector>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// task/83: everything the UI does with RendererWindowState::SceneDensity. Loading itself is
	// SceneDensityLoader's job - these only create, edit and remove the objects.

	// A new, not yet loaded density for `chgcarPath`, named after its folder ("q-1/CHGCAR").
	[[nodiscard]] RendererWindowState::SceneDensity MakeSceneDensity(
		const Path &chgcarPath, DensityComponent component = DensityComponent::Magnetization);

	// Iso value a freshly loaded grid starts at: 10 % of its peak |value|, the "where is the charge"
	// picture. Zero for an all-zero grid (nothing to draw anyway).
	[[nodiscard]] float DefaultDensityIsoValue(const DensityGridStatistics &statistics);

	// Marks the density for reload after its file, reference or component changed: drops the old
	// grid and re-picks the iso value on the next load, since the scale differs by orders of
	// magnitude between total and spin density.
	void InvalidateSceneDensity(RendererWindowState::SceneDensity &density);

	// Native file dialog, then a new selected density in `windowState` (one undo step).
	void AddSceneDensityFromFileDialog(RendererWindowState &windowState);

	void EraseSceneDensities(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);
	// Copies share the loaded grid, so a duplicate shows at once and can take its own iso/colours.
	void DuplicateSelectedSceneDensities(RendererWindowState &windowState);

	// The N panel / Object Properties section for the first selected density.
	void DrawSelectedSceneDensitySection(RendererWindowState &windowState);
} // namespace DefectStudio
