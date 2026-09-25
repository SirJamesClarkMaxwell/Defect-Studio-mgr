#include "Core/dspch.hpp"

#include "Renderer/Path/PathHandleGeometry.hpp"

#include <cmath>
#include <variant>

#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool Finite(const glm::vec3 &v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		[[nodiscard]] bool Finite(const glm::vec2 &v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y);
		}

		[[nodiscard]] bool Finite(const glm::mat4 &m)
		{
			for (int column = 0; column < 4; ++column)
			{
				for (int row = 0; row < 4; ++row)
				{
					if (!std::isfinite(m[column][row]))
						return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool ViewportUsable(const glm::vec2 size)
		{
			return Finite(size) && size.x > 0.0f && size.y > 0.0f;
		}
	} // namespace

	std::vector<PathHandleMarker> BuildPathHandleMarkers(
		const ScenePath &path,
		const ResolvedNodes &resolved,
		const glm::mat4 &viewProjection,
		const glm::vec2 viewportSize,
		const PathElementId activeElement)
	{
		std::vector<PathHandleMarker> markers;
		if (!ViewportUsable(viewportSize) || !Finite(viewProjection))
			return markers;

		// Gathered before anything is projected: a single non-finite coordinate anywhere means the
		// path is malformed, and half a marker set is worse than none - the caller would draw and pick
		// a subset with no way to know it was a subset.
		std::vector<glm::vec3> nodePositions;
		nodePositions.reserve(path.nodes.size());
		for (std::size_t index = 0; index < path.nodes.size(); ++index)
		{
			const glm::vec3 position =
				index < resolved.positions.size() ? resolved.positions[index] : path.nodes[index].position;
			if (!Finite(position))
				return markers;
			nodePositions.push_back(position);
		}
		for (const PathSegment &segment : path.segments)
		{
			if (const auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data))
			{
				if (!Finite(cubic->startHandle.position) || !Finite(cubic->endHandle.position))
					return markers;
			}
		}

		const auto append = [&](const PathMarkerKind kind, const PathElementId element, const PathElementId owner,
								 const glm::vec3 &world) {
			const std::optional<glm::vec2> screen =
				SelectionHitTest::ProjectToScreen(viewProjection, viewportSize, world);
			if (!screen.has_value() || !Finite(*screen))
				return;
			const bool active = activeElement.IsValid() && element == activeElement;
			markers.push_back({kind, element, owner, world, *screen,
				active ? kPathActiveHandleDrawRadius : kPathHandleDrawRadius,
				active ? kPathActiveHandlePickRadius : kPathHandlePickRadius});
		};

		for (std::size_t index = 0; index < path.nodes.size(); ++index)
			append(PathMarkerKind::Node, path.nodes[index].id, path.nodes[index].id, nodePositions[index]);

		for (std::size_t index = 0; index < path.segments.size(); ++index)
		{
			const auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[index].data);
			if (cubic == nullptr)
				continue;
			// A handle belongs to the node on its side of the segment: segment i spans nodes i and i+1.
			const PathElementId startOwner = index < path.nodes.size() ? path.nodes[index].id : PathElementId{};
			const PathElementId endOwner = index + 1 < path.nodes.size() ? path.nodes[index + 1].id : PathElementId{};
			append(PathMarkerKind::BezierHandle, cubic->startHandle.id, startOwner, cubic->startHandle.position);
			append(PathMarkerKind::BezierHandle, cubic->endHandle.id, endOwner, cubic->endHandle.position);
		}

		return markers;
	}

	std::optional<float> ProjectWorldRadiusToPixels(
		const glm::mat4 &viewProjection,
		const glm::vec2 viewportSize,
		const glm::vec3 &worldPosition,
		const glm::vec3 &cameraRight,
		const float worldRadius)
	{
		if (!ViewportUsable(viewportSize) || !Finite(viewProjection) || !Finite(worldPosition) || !Finite(cameraRight))
			return std::nullopt;
		if (!std::isfinite(worldRadius) || worldRadius < 0.0f)
			return std::nullopt;
		if (worldRadius == 0.0f)
			return 0.0f;

		const float rightLength = glm::length(cameraRight);
		if (!std::isfinite(rightLength) || rightLength <= 0.0f)
			return std::nullopt;

		const std::optional<glm::vec2> center =
			SelectionHitTest::ProjectToScreen(viewProjection, viewportSize, worldPosition);
		const std::optional<glm::vec2> offset = SelectionHitTest::ProjectToScreen(
			viewProjection, viewportSize, worldPosition + (cameraRight / rightLength) * worldRadius);
		if (!center.has_value() || !offset.has_value())
			return std::nullopt;

		const float pixels = glm::length(*offset - *center);
		if (!std::isfinite(pixels))
			return std::nullopt;
		return pixels;
	}
}
