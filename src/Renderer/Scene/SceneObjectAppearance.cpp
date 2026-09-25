#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneObjectAppearance.hpp"

#include <algorithm>

#include "Renderer/Path/PathDash.hpp"

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
		const PathDashStyle style{true, dashLength, gapLength, 0.0f};
		const std::vector<DashInterval> intervals = BuildDashIntervals(0.0, shaftLength, style);
		std::vector<SceneArrowShaftSegment> segments;
		segments.reserve(intervals.size());
		for (const DashInterval &interval : intervals)
			segments.push_back({static_cast<float>(interval.start), static_cast<float>(interval.end)});
		return segments;
	}
} // namespace DefectStudio
