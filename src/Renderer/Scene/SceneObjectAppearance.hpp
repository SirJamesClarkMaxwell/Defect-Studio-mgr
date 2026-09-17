#pragma once

#include <glm/glm.hpp>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	struct SceneArrowRenderColors
	{
		glm::vec4 start = glm::vec4(1.0f);
		glm::vec4 finish = glm::vec4(1.0f);
	};

	[[nodiscard]] constexpr glm::vec3 SceneSelectionHighlightColor()
	{
		return glm::vec3(0.91f, 0.52f, 0.02f);
	}

	[[nodiscard]] glm::vec3 ApplySceneSelectionHighlight(const glm::vec3 &base, bool selected);
	[[nodiscard]] SceneArrowRenderColors ResolveSceneArrowRenderColors(
		const RendererWindowState::ArrowStyle &style, bool selected);
} // namespace DefectStudio
