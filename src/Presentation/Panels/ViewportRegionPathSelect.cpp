#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

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
}
