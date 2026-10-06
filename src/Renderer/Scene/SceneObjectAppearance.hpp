#pragma once

#include <glm/glm.hpp>

namespace DefectStudio
{
	[[nodiscard]] constexpr glm::vec3 SceneSelectionHighlightColor()
	{
		return glm::vec3(0.91f, 0.52f, 0.02f);
	}

	// How far a selected object's colour is pulled towards the highlight. The default suits a thin
	// object - an arrow, a line - where the tint is the only thing that reads at that size. A big
	// translucent surface needs far less, because the highlight is spread over the whole lobe: at
	// the default an orbital's own red or blue was gone entirely, which made the colour pickers in
	// the properties panel look broken.
	inline constexpr float kSceneSelectionHighlightStrength = 0.55f;
	inline constexpr float kSceneSurfaceSelectionHighlightStrength = 0.18f;

	[[nodiscard]] glm::vec3 ApplySceneSelectionHighlight(
		const glm::vec3 &base, bool selected, float strength = kSceneSelectionHighlightStrength);
} // namespace DefectStudio
