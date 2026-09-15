#include "Core/dspch.hpp"

#include "Renderer/Scene/ViewportNavigationMath.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace DefectStudio
{
	OrientationAxes ResolveNormalizedOrientationAxes(
		TransformOrientation orientation, const TransformBases &bases)
	{
		const glm::mat3 basis = ResolveBasis(orientation, bases);
		OrientationAxes axes = {glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)};
		for (int axis = 0; axis < 3; ++axis)
		{
			const float lengthSquared = glm::dot(basis[axis], basis[axis]);
			if (lengthSquared > 1.0e-8f)
				axes[axis] = basis[axis] / std::sqrt(lengthSquared);
		}
		return axes;
	}

	std::array<NavigationAxisMarker, 6> ProjectNavigationAxisMarkers(
		const glm::mat4 &view, const OrientationAxes &axes, const glm::vec2 &center, float axisLength)
	{
		std::array<NavigationAxisMarker, 6> markers{};
		const glm::mat3 cameraRotation(view);
		for (int axis = 0; axis < 3; ++axis)
		{
			const glm::vec3 cameraDirection = cameraRotation * axes[axis];
			const glm::vec2 screenDirection(cameraDirection.x, -cameraDirection.y);
			markers[static_cast<std::size_t>(axis)] = {
				center + screenDirection * axisLength, cameraDirection.z, axis, 1};
			markers[static_cast<std::size_t>(axis + 3)] = {
				center - screenDirection * axisLength, -cameraDirection.z, axis, -1};
		}
		return markers;
	}

	std::array<std::size_t, 6> SortNavigationMarkersBackToFront(
		const std::array<NavigationAxisMarker, 6> &markers)
	{
		std::array<std::size_t, 6> order{};
		std::iota(order.begin(), order.end(), 0u);
		std::stable_sort(order.begin(), order.end(), [&markers](std::size_t left, std::size_t right) {
			return markers[left].depth < markers[right].depth;
		});
		return order;
	}

	std::optional<std::size_t> HitTestNavigationAxisMarkers(
		const std::array<NavigationAxisMarker, 6> &markers,
		const glm::vec2 &mouse,
		float positiveRadius,
		float negativeRadius)
	{
		std::optional<std::size_t> best;
		float bestDepth = -std::numeric_limits<float>::infinity();
		float bestDistance = std::numeric_limits<float>::infinity();
		for (std::size_t index = 0; index < markers.size(); ++index)
		{
			const NavigationAxisMarker &marker = markers[index];
			const float radius = marker.sign > 0 ? positiveRadius : negativeRadius;
			const float distance = glm::distance(mouse, marker.center);
			if (distance > radius)
				continue;
			if (marker.depth > bestDepth || (marker.depth == bestDepth && distance < bestDistance))
			{
				best = index;
				bestDepth = marker.depth;
				bestDistance = distance;
			}
		}
		return best;
	}

	glm::vec3 ResolveNavigationViewSide(
		const glm::vec3 &clickedSide, const glm::vec3 &currentEyeDirection, float alignmentThreshold)
	{
		if (glm::dot(clickedSide, clickedSide) <= 1.0e-8f)
			return glm::vec3(0.0f);
		const glm::vec3 side = glm::normalize(clickedSide);
		if (glm::dot(currentEyeDirection, currentEyeDirection) > 1.0e-8f &&
			glm::dot(glm::normalize(currentEyeDirection), side) >= alignmentThreshold)
		{
			return -side;
		}
		return side;
	}
} // namespace DefectStudio
