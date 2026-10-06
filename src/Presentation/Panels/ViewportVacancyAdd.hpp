#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace DefectStudio
{
	class CommandRegistry;
	struct RendererWindowState;

	// Adds a vacancy marker at `position` (renderer.vacancy.set, one undo step).
	void AddVacancyAt(RendererWindowState &windowState, CommandRegistry *registry, const glm::vec3 &position);

	// "Bonds" from atoms to a vacancy: one Line scene object per pair, both ends bound to their sources,
	// coloured atom -> vacancy (two-colour gradient), bond thickness. Each is an ordinary scene path,
	// so its style is edited on its own. Pairs:
	//   atoms selected  -> each atom to the one selected vacancy, else to its nearest vacancy
	//   otherwise       -> each selected vacancy (none selected: every vacancy) to its
	//                      nearest-neighbour shell of shown atoms
	// Returns how many were added (one undo step for all).
	std::size_t AddVacancyBonds(RendererWindowState &windowState, float bondRadiusMultiplier = 1.0f);

	// Atoms within 1.15x the nearest shown atom's distance from `position` (the first shell).
	[[nodiscard]] std::vector<std::size_t> NeighbourShell(const RendererWindowState &windowState, const glm::vec3 &position);

	// Entries shared by the Shift+A menu and the right-click Add submenu: Vacancy, vacancy bonds and
	// the Defect axes (empty) submenu. `position` is where the menu was opened (click point, or the
	// 3D cursor for Shift+A); a vacancy goes to the selected atoms' centroid when there is one.
	void DrawDefectAddItems(RendererWindowState &windowState, CommandRegistry *registry, const glm::vec3 &position, float bondRadiusMultiplier = 1.0f);
} // namespace DefectStudio
