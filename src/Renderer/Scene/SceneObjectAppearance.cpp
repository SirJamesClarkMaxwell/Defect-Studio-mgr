#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneObjectAppearance.hpp"

namespace DefectStudio
{
	glm::vec3 ApplySceneSelectionHighlight(const glm::vec3 &base, const bool selected)
	{
		return selected ? glm::mix(base, SceneSelectionHighlightColor(), 0.55f) : base;
	}

	SceneArrowRenderColors ResolveSceneArrowRenderColors(
		const RendererWindowState::ArrowStyle &style, const bool selected)
	{
		const glm::vec3 start = style.useGradient ? style.gradient.start : style.color;
		const glm::vec3 finish = style.useGradient ? style.gradient.finish : style.color;
		return {
			glm::vec4(ApplySceneSelectionHighlight(start, selected), style.alpha),
			glm::vec4(ApplySceneSelectionHighlight(finish, selected), style.alpha)};
	}
} // namespace DefectStudio
