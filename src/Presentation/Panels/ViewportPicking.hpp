#pragma once

#include <cstddef>
#include <optional>

#include <glm/glm.hpp>
#include <imgui.h>

namespace DefectStudio
{
	class RendererLayer;
	struct RendererWindowState;
	struct ViewportPickRay
	{
		glm::vec3 origin;
		glm::vec3 direction;
	};
	// Displayed-image coordinates; origin is in scene space (viewOffset removed).
	[[nodiscard]] std::optional<ViewportPickRay> ComputeViewportPickRay(
		const RendererWindowState &window, glm::vec2 position, glm::vec2 imageSize);
	[[nodiscard]] std::optional<std::size_t> PickBondAlongRay(
		const RendererWindowState &window, const glm::vec3 &origin, const glm::vec3 &direction);

	// Nearest visible atom whose (padded) sphere the world-space ray enters, or nullopt. Ignores
	// pickAtoms - callers decide whether atoms are pickable. The scene-object click handlers use it
	// to give way to an atom under the cursor: their own hit volumes (an orbital's bounding sphere,
	// a plane's quad) usually swallow the atom they sit on.
	[[nodiscard]] std::optional<std::size_t> PickAtomAlongRay(
		const RendererWindowState &windowState, const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection);

	[[nodiscard]] glm::vec3 ComputeViewportWorldPosition(const RendererWindowState &windowState, float relX, float relY);

	// Ray-cast atom pick for one viewport, publishing AtomSelectionRequested (a miss publishes an
	// empty selection, which is how a click on background clears it). Free functions, not
	// RendererPanel members, for the same reason ViewportInput's are: the three-pane creation window
	// draws its own viewports and would otherwise have no way to select anything at all.
	void HandleAtomPick(
		RendererWindowState &windowState, float relX, float relY, bool additive, RendererLayer &layer);

	// Plain-click entry point for a viewport (unlike HandleAtomPick, which stays atom-only for the
	// measure tool's pair/triple picking) - tries atoms first, then bonds, so an atom under the
	// cursor always wins a bond behind/near it.
	void HandleViewportPick(
		RendererWindowState &windowState, float relX, float relY, bool additive, RendererLayer &layer);
} // namespace DefectStudio
