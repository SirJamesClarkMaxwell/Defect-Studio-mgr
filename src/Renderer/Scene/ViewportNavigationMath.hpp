#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include <glm/glm.hpp>

#include "Renderer/Scene/ModalTransform.hpp"

namespace DefectStudio
{
	using OrientationAxes = std::array<glm::vec3, 3>;

	struct NavigationAxisMarker
	{
		glm::vec2 center = glm::vec2(0.0f);
		float depth = 0.0f;
		int axis = 0;
		int sign = 1;
	};

	[[nodiscard]] OrientationAxes ResolveNormalizedOrientationAxes(
		TransformOrientation orientation, const TransformBases &bases);
	[[nodiscard]] std::array<NavigationAxisMarker, 6> ProjectNavigationAxisMarkers(
		const glm::mat4 &view, const OrientationAxes &axes, const glm::vec2 &center, float axisLength);
	[[nodiscard]] std::array<std::size_t, 6> SortNavigationMarkersBackToFront(
		const std::array<NavigationAxisMarker, 6> &markers);
	[[nodiscard]] std::optional<std::size_t> HitTestNavigationAxisMarkers(
		const std::array<NavigationAxisMarker, 6> &markers,
		const glm::vec2 &mouse,
		float positiveRadius,
		float negativeRadius);
	[[nodiscard]] glm::vec3 ResolveNavigationViewSide(
		const glm::vec3 &clickedSide, const glm::vec3 &currentEyeDirection, float alignmentThreshold = 0.98f);
} // namespace DefectStudio
