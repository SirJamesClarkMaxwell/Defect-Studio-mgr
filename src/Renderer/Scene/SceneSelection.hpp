#pragma once
#include <glm/glm.hpp>

namespace DefectStudio
{
	struct RendererWindowState;
	// Ctrl+A selects visible kinds enabled by the current pick mask; repeating it clears everything.
	void SelectAllVisibleSceneObjects(RendererWindowState &window, bool deselect = false);
	void ClearAllSceneSelection(RendererWindowState &window);
	// Orbital surfaces and plane quads overlap the region; vacancies and axes use their centres.
	// radius > 0: circle around minimum; otherwise rectangle [minimum, maximum].
	void ApplySceneDrawingRegionSelection(RendererWindowState &window, glm::vec2 minimum,
		glm::vec2 maximum, float radius, bool replace, bool subtract);
}
