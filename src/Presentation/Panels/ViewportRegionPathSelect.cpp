#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <variant>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathEditSession.hpp"
#include "Renderer/Path/PathHandleGeometry.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool SegmentInRect(glm::vec2 a, glm::vec2 b, glm::vec2 minimum, glm::vec2 maximum)
		{
			if (SelectionHitTest::PointInRect(a, minimum, maximum) || SelectionHitTest::PointInRect(b, minimum, maximum))
				return true;
			const glm::vec2 delta = b - a;
			float enter = 0.0f;
			float exit = 1.0f;
			for (int axis = 0; axis < 2; ++axis)
			{
				if (std::abs(delta[axis]) <= 1.0e-6f)
				{
					if (a[axis] < minimum[axis] || a[axis] > maximum[axis])
						return false;
					continue;
				}
				float nearValue = (minimum[axis] - a[axis]) / delta[axis];
				float farValue = (maximum[axis] - a[axis]) / delta[axis];
				if (nearValue > farValue)
					std::swap(nearValue, farValue);
				enter = std::max(enter, nearValue);
				exit = std::min(exit, farValue);
				if (enter > exit)
					return false;
			}
			return true;
		}

		[[nodiscard]] bool SegmentInCircle(glm::vec2 a, glm::vec2 b, glm::vec2 center, float radius)
		{
			const glm::vec2 delta = b - a;
			const float denominator = glm::dot(delta, delta);
			const float t = denominator > 0.0f
				? std::clamp(glm::dot(center - a, delta) / denominator, 0.0f, 1.0f) : 0.0f;
			return glm::distance(a + delta * t, center) <= radius;
		}

		template <typename SegmentHit>
		void ForEachPathHit(const RendererWindowState &windowState, SegmentHit &&segmentHit,
			std::vector<SceneObjectId> &hits)
		{
			if (windowState.camera == nullptr || windowState.paths == nullptr)
				return;
			const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
			windowState.paths->Store().Visit([&](const ScenePath &path) {
				if (!path.visible || !path.renderable)
					return;
				const CachedPathGeometry *cached = windowState.paths->Caches().FindLastBuilt(path.id);
				if (cached == nullptr)
					return;
				std::optional<glm::vec2> previous;
				for (const EvaluatedSample &sample : cached->evaluated.samples)
				{
					const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
						viewProjection, windowState.viewportSize, glm::vec3(sample.position));
					if (screen.has_value() && segmentHit(previous, *screen))
					{
						hits.push_back(path.id);
						return;
					}
					if (screen.has_value())
						previous = screen;
				}
			});
		}

		template <typename MarkerHit>
		[[nodiscard]] std::vector<PathElementId> HitTestEditedPathElements(
			const RendererWindowState &windowState, MarkerHit markerHit)
		{
			std::vector<PathElementId> hits;
			if (!windowState.pathEdit.IsActive() || windowState.camera == nullptr || windowState.paths == nullptr)
				return hits;

			const ScenePath *path = windowState.paths->Store().Find(windowState.pathEdit.Path());
			if (path == nullptr || !path->visible || !path->renderable)
				return hits;

			const std::vector<PathHandleMarker> markers = BuildPathHandleMarkers(
				*path,
				ResolveNodePositions(*path, BindingContext{}),
				windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix(),
				windowState.viewportSize,
				windowState.pathEdit.ActiveElement());
			const auto appendUnique = [&hits](const PathElementId id) {
				if (std::find(hits.begin(), hits.end(), id) == hits.end())
					hits.push_back(id);
			};

			if (windowState.pathEdit.ElementMode() == PathElementMode::WholePath)
			{
				if (std::any_of(markers.begin(), markers.end(), markerHit))
					for (const PathHandleMarker &marker : markers)
						appendUnique(marker.element);
				return hits;
			}

			if (windowState.pathEdit.ElementMode() == PathElementMode::NodeHandle)
			{
				for (const PathHandleMarker &marker : markers)
					if (markerHit(marker))
						appendUnique(marker.element);
				return hits;
			}

			for (std::size_t segmentIndex = 0; segmentIndex < path->segments.size(); ++segmentIndex)
			{
				const PathSegment &segment = path->segments[segmentIndex];
				const auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data);
				const bool hit = std::any_of(markers.begin(), markers.end(), [&](const PathHandleMarker &marker) {
					if (!markerHit(marker))
						return false;
					if (marker.kind == PathMarkerKind::BezierHandle)
						return cubic != nullptr &&
							(marker.element == cubic->startHandle.id || marker.element == cubic->endHandle.id);
					return (segmentIndex < path->nodes.size() && marker.element == path->nodes[segmentIndex].id) ||
						(segmentIndex + 1 < path->nodes.size() && marker.element == path->nodes[segmentIndex + 1].id);
				});
				if (hit)
					appendUnique(segment.id);
			}
			return hits;
		}

		template <typename MarkerHit>
		void ApplyEditedPathRegionSelection(
			RendererWindowState &windowState, MarkerHit markerHit,
			const RendererEvents::Viewport::RegionSelectMode mode)
		{
			const std::vector<PathElementId> hits = HitTestEditedPathElements(windowState, markerHit);
			const std::vector<PathElementId> current = windowState.pathEdit.Selection();
			std::vector<PathElementId> selection;
			if (mode == RendererEvents::Viewport::RegionSelectMode::Subtract)
			{
				for (const PathElementId id : current)
					if (std::find(hits.begin(), hits.end(), id) == hits.end())
						selection.push_back(id);
			}
			else
			{
				if (mode == RendererEvents::Viewport::RegionSelectMode::Replace)
					for (const PathElementId id : current)
						if (std::find(hits.begin(), hits.end(), id) != hits.end())
							selection.push_back(id);
				else
					selection = current;

				for (const PathElementId id : hits)
					if (std::find(selection.begin(), selection.end(), id) == selection.end())
						selection.push_back(id);
			}
			windowState.pathEdit.SetSelection(std::move(selection));
		}
	}

	std::vector<SceneObjectId> HitTestRectScenePaths(
		const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax)
	{
		std::vector<SceneObjectId> hits;
		ForEachPathHit(windowState, [&](const std::optional<glm::vec2> &previous, const glm::vec2 screen) {
			return (previous.has_value() && SegmentInRect(*previous, screen, rectMin, rectMax)) ||
				SelectionHitTest::PointInRect(screen, rectMin, rectMax);
		}, hits);
		return hits;
	}

	std::vector<SceneObjectId> HitTestCircleScenePaths(
		const RendererWindowState &windowState, glm::vec2 center, float radius)
	{
		std::vector<SceneObjectId> hits;
		if (radius < 0.0f)
			return hits;
		ForEachPathHit(windowState, [&](const std::optional<glm::vec2> &previous, const glm::vec2 screen) {
			return (previous.has_value() && SegmentInCircle(*previous, screen, center, radius)) ||
				SelectionHitTest::PointInCircle(screen, center, radius);
		}, hits);
		return hits;
	}

	void ApplyPathElementRectSelection(
		RendererWindowState &windowState, const glm::vec2 rectMin, const glm::vec2 rectMax,
		const RendererEvents::Viewport::RegionSelectMode mode)
	{
		ApplyEditedPathRegionSelection(windowState, [&](const PathHandleMarker &marker) {
			return SelectionHitTest::PointInRect(marker.screenPosition, rectMin, rectMax);
		}, mode);
	}

	void ApplyPathElementCircleSelection(
		RendererWindowState &windowState, const glm::vec2 center, const float radius,
		const RendererEvents::Viewport::RegionSelectMode mode)
	{
		if (radius < 0.0f)
			return;
		ApplyEditedPathRegionSelection(windowState, [&](const PathHandleMarker &marker) {
			return SelectionHitTest::PointInCircle(marker.screenPosition, center, radius);
		}, mode);
	}
}
