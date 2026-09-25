#include "Core/dspch.hpp"

#include "Renderer/Path/PathPicking.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "Renderer/Path/PathDecoration.hpp"
#include "Renderer/Path/PathHandleGeometry.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool Finite(const glm::vec2 &v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y);
		}

		[[nodiscard]] bool Finite(const glm::vec3 &v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
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

		[[nodiscard]] bool SettingsUsable(const PathPickSettings &settings)
		{
			return Finite(settings.viewportSize) && settings.viewportSize.x > 0.0f && settings.viewportSize.y > 0.0f &&
				Finite(settings.cursor) && Finite(settings.cameraRight) && Finite(settings.viewProjection) &&
				glm::length(settings.cameraRight) > 0.0f;
		}

		// SelectionHitTest::DistancePointToSegment gives the distance but not where on the segment it
		// landed, and criterion 22 wants the world point the cursor actually picked. Same clamped
		// projection, one extra return value.
		struct ClosestOnSegment
		{
			float t = 0.0f;
			float distance = 0.0f;
		};

		[[nodiscard]] ClosestOnSegment ClosestPointOnSegment2D(
			const glm::vec2 point, const glm::vec2 a, const glm::vec2 b)
		{
			const glm::vec2 ab = b - a;
			const float lengthSquared = glm::dot(ab, ab);
			const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
			return {t, glm::length(point - (a + ab * t))};
		}

		struct Candidate
		{
			bool hit = false;
			float distance = std::numeric_limits<float>::max(); // ranking only: cursor to geometry centre
			float screenDistance = 0.0f;                        // reported: 0 inside the drawn shape
			glm::vec3 worldPosition{0.0f};
			PathElementId element;
		};

		// The world point `distance` of arc length in from one end of the evaluated polyline. Used for
		// the far end of a decoration's axis, so the hitbox follows a curve instead of running straight
		// off it.
		[[nodiscard]] glm::vec3 PointAtArcLengthFromStart(const EvaluatedPath &evaluated, const double rawDistance)
		{
			if (evaluated.samples.empty())
				return glm::vec3(0.0f);
			// A decoration longer than the path is the mesher's DecorationsExceedPathLength case, not
			// this function's problem: clamp and let the hitbox be the whole path.
			const double distance = std::clamp(rawDistance, 0.0, std::max(0.0, evaluated.totalLength));
			for (std::size_t index = 1; index < evaluated.samples.size(); ++index)
			{
				const EvaluatedSample &previous = evaluated.samples[index - 1];
				const EvaluatedSample &current = evaluated.samples[index];
				if (current.arcLength < distance)
					continue;
				const double span = current.arcLength - previous.arcLength;
				const double t = span > 0.0 ? (distance - previous.arcLength) / span : 0.0;
				return glm::vec3(previous.position + (current.position - previous.position) * t);
			}
			return glm::vec3(evaluated.samples.back().position);
		}

		[[nodiscard]] glm::vec3 PointAtArcLengthFromEnd(const EvaluatedPath &evaluated, const double distance)
		{
			return PointAtArcLengthFromStart(evaluated, evaluated.totalLength - distance);
		}

		struct DecorationHitbox
		{
			bool valid = false;
			glm::vec3 tip{0.0f};
			glm::vec3 back{0.0f};
			float worldHalfWidth = 0.0f;
		};

		// The decoration occupies `extent` of arc length back from the endpoint and reaches
		// `worldHalfWidth` off the axis - both read off the contour the mesher built, not guessed.
		[[nodiscard]] DecorationHitbox BuildDecorationHitbox(
			const PathEndpointDecoration &decoration, const PathStrokeStyle &style, const EvaluatedPath &evaluated,
			const glm::vec3 &endpoint, const glm::vec3 &inwardNeighbour, const bool atStart)
		{
			DecorationHitbox box;
			if (decoration.kind == PathDecorationKind::None)
				return box;
			const DecorationContour contour = BuildDecorationContour(decoration, style.width);
			if (contour.points.empty())
				return box;

			double extent = 0.0;
			double halfWidth = 0.0;
			for (const DecorationContourPoint &point : contour.points)
			{
				extent = std::max(extent, point.s);
				halfWidth = std::max(halfWidth, point.halfWidth);
			}
			if (!std::isfinite(extent) || !std::isfinite(halfWidth) || halfWidth <= 0.0)
				return box;

			box.valid = true;
			box.tip = endpoint;
			box.worldHalfWidth = static_cast<float>(halfWidth);
			if (evaluated.samples.size() >= 2 && evaluated.totalLength > 0.0)
			{
				box.back = atStart ? PointAtArcLengthFromStart(evaluated, extent)
								   : PointAtArcLengthFromEnd(evaluated, extent);
			}
			else
			{
				// No tessellation to follow: the neighbouring node is the only direction available.
				const glm::vec3 inward = inwardNeighbour - endpoint;
				const float length = glm::length(inward);
				box.back = length > 0.0f ? endpoint + (inward / length) * static_cast<float>(extent) : endpoint;
			}
			if (!Finite(box.back))
				box.valid = false;
			return box;
		}

		[[nodiscard]] Candidate TestCapsule(
			const PathPickSettings &settings, const glm::vec3 &a, const glm::vec3 &b, const float worldHalfWidth,
			const PathElementId element)
		{
			Candidate candidate;
			const std::optional<glm::vec2> screenA =
				SelectionHitTest::ProjectToScreen(settings.viewProjection, settings.viewportSize, a);
			const std::optional<glm::vec2> screenB =
				SelectionHitTest::ProjectToScreen(settings.viewProjection, settings.viewportSize, b);
			const std::optional<float> radius = ProjectWorldRadiusToPixels(
				settings.viewProjection, settings.viewportSize, a, settings.cameraRight, worldHalfWidth);
			if (!screenA.has_value() || !screenB.has_value() || !radius.has_value())
				return candidate;

			const ClosestOnSegment closest = ClosestPointOnSegment2D(settings.cursor, *screenA, *screenB);
			if (!std::isfinite(closest.distance) || closest.distance > *radius + kPathStrokePickTolerance)
				return candidate;

			candidate.hit = true;
			candidate.distance = closest.distance;
			candidate.screenDistance = std::max(0.0f, closest.distance - *radius);
			candidate.worldPosition = a + (b - a) * closest.t;
			candidate.element = element;
			return candidate;
		}

		[[nodiscard]] Candidate PickMarker(
			const std::vector<PathHandleMarker> &markers, const PathMarkerKind kind, const glm::vec2 cursor)
		{
			Candidate best;
			for (const PathHandleMarker &marker : markers)
			{
				if (marker.kind != kind)
					continue;
				const float distance = glm::length(cursor - marker.screenPosition);
				if (!std::isfinite(distance) || distance > marker.pickRadius || distance >= best.distance)
					continue;
				best.hit = true;
				best.distance = distance;
				best.screenDistance = std::max(0.0f, distance - marker.drawRadius);
				best.worldPosition = marker.worldPosition;
				best.element = marker.element;
			}
			return best;
		}

	} // namespace

	PathPickResult PickPath(
		const ScenePath &path, const ResolvedNodes &resolved, const EvaluatedPath &evaluated,
		const PathPickSettings &settings)
	{
		PathPickResult result;
		result.path = path.id;
		if (!path.visible || !path.renderable || !SettingsUsable(settings))
			return result;

		const std::vector<PathHandleMarker> markers = BuildPathHandleMarkers(
			path, resolved, settings.viewProjection, settings.viewportSize, settings.activeElement);

		const auto emit = [&](const PathPickKind kind, const Candidate &candidate) {
			result.kind = settings.editMode ? kind : PathPickKind::WholePath;
			result.element = settings.editMode ? candidate.element : PathElementId{};
			result.worldPosition = candidate.worldPosition;
			result.screenDistance = candidate.screenDistance;
		};

		if (settings.editMode)
		{
			if (const Candidate handle = PickMarker(markers, PathMarkerKind::BezierHandle, settings.cursor); handle.hit)
			{
				emit(PathPickKind::Handle, handle);
				return result;
			}
			if (const Candidate node = PickMarker(markers, PathMarkerKind::Node, settings.cursor); node.hit)
			{
				emit(PathPickKind::Node, node);
				return result;
			}
		}

		// Decorations before the shaft: a tip is drawn on top of the stroke it trims, so a click inside
		// one means the tip even when the shaft's tolerance also reaches that pixel.
		if (!path.nodes.empty())
		{
			const std::size_t lastNode = path.nodes.size() - 1;
			const auto nodePosition = [&](const std::size_t index) {
				return index < resolved.positions.size() ? resolved.positions[index] : path.nodes[index].position;
			};
			struct End
			{
				const PathEndpointDecoration &decoration;
				glm::vec3 endpoint;
				glm::vec3 neighbour;
				PathElementId element;
				bool atStart;
			};
			const std::array<End, 2> ends = {{
				End{path.style.startDecoration, nodePosition(0), nodePosition(lastNode > 0 ? 1 : 0),
					path.nodes.front().id, true},
				End{path.style.endDecoration, nodePosition(lastNode), nodePosition(lastNode > 0 ? lastNode - 1 : 0),
					path.nodes.back().id, false}}};

			Candidate best;
			for (const End &end : ends)
			{
				const DecorationHitbox box = BuildDecorationHitbox(
					end.decoration, path.style, evaluated, end.endpoint, end.neighbour, end.atStart);
				if (!box.valid)
					continue;
				const Candidate candidate = TestCapsule(settings, box.tip, box.back, box.worldHalfWidth, end.element);
				if (candidate.hit && candidate.distance < best.distance)
					best = candidate;
			}
			if (best.hit)
			{
				emit(PathPickKind::Decoration, best);
				return result;
			}
		}

		// The shaft, as the screen-space polyline the tessellator produced. The dash pattern is not
		// consulted: a gap that swallows clicks is worse than a dashed line that picks as one object.
		if (evaluated.samples.size() >= 2 && path.style.width > 0.0f && std::isfinite(path.style.width))
		{
			const float worldHalfWidth = 0.5f * path.style.width;
			Candidate best;
			for (std::size_t index = 1; index < evaluated.samples.size(); ++index)
			{
				const EvaluatedSample &previous = evaluated.samples[index - 1];
				const EvaluatedSample &current = evaluated.samples[index];
				const glm::vec3 a(previous.position);
				const glm::vec3 b(current.position);
				if (!Finite(a) || !Finite(b))
					continue;
				const Candidate candidate = TestCapsule(settings, a, b, worldHalfWidth, current.segment);
				if (candidate.hit && candidate.distance < best.distance)
					best = candidate;
			}
			if (best.hit)
			{
				emit(PathPickKind::Segment, best);
				return result;
			}
		}

		return result;
	}
}
