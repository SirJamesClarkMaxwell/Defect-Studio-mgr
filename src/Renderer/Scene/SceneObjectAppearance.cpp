#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneObjectAppearance.hpp"

#include <algorithm>

namespace DefectStudio
{
	glm::vec3 ApplySceneSelectionHighlight(const glm::vec3 &base, const bool selected, const float strength)
	{
		return selected ? glm::mix(base, SceneSelectionHighlightColor(), strength) : base;
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

	std::vector<SceneArrowShaftSegment> BuildSceneArrowShaftSegments(
		const float shaftLength, const bool dashed, const float dashLength, const float gapLength)
	{
		if (shaftLength <= 0.0001f)
			return {};
		if (!dashed || dashLength <= 0.0001f || gapLength <= 0.0001f)
			return {{0.0f, shaftLength}};

		std::vector<SceneArrowShaftSegment> segments;
		for (float start = 0.0f; start < shaftLength; start += dashLength + gapLength)
		{
			const float end = std::min(start + dashLength, shaftLength);
			if (end > start)
				segments.push_back({start, end});
		}
		return segments;
	}
} // namespace DefectStudio
