#include "Core/dspch.hpp"

#include "Renderer/Path/ScenePathPicking.hpp"

#include <cmath>

#include "Renderer/Path/PathBindingResolver.hpp"

namespace DefectStudio
{
	namespace
	{
		// NDC z of a world point, or nullopt behind the camera. PickPath already refuses to report a
		// hit that is behind the camera, so this should never fail on a real hit - but dividing by a
		// w that reached zero anyway would put a NaN into the arbitration and silently win every
		// comparison against it.
		[[nodiscard]] std::optional<float> NdcDepth(const glm::mat4 &viewProjection, const glm::vec3 &world)
		{
			const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
			if (!(clip.w > 0.0f) || !std::isfinite(clip.w) || !std::isfinite(clip.z))
				return std::nullopt;
			const float depth = clip.z / clip.w;
			return std::isfinite(depth) ? std::optional<float>(depth) : std::nullopt;
		}
	} // namespace

	std::optional<ScenePathPick> PickFrontmostScenePath(const PathSystem &paths, const PathPickSettings &settings)
	{
		std::optional<ScenePathPick> best;
		paths.Store().Visit([&](const ScenePath &path) {
			// An empty EvaluatedPath is the "never rendered" case the header documents: the shaft
			// drops out and only the decorations and, in edit mode, the markers remain pickable.
			const CachedPathGeometry *cached = paths.Caches().FindLastBuilt(path.id);
			static const EvaluatedPath empty;
			const ResolvedNodes resolved = ResolveNodePositions(path, BindingContext{});
			const PathPickResult result = PickPath(path, resolved, cached == nullptr ? empty : cached->evaluated, settings);
			if (!result.Hit())
				return;
			const std::optional<float> depth = NdcDepth(settings.viewProjection, result.worldPosition);
			if (!depth.has_value())
				return;
			// Strictly less: an exact tie keeps the earlier path, which is store order.
			if (best.has_value() && !(*depth < best->depth))
				return;
			best = ScenePathPick{path.id, result, *depth};
		});
		return best;
	}
} // namespace DefectStudio
