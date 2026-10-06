#pragma once

#include <optional>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathPicking.hpp"
#include "Renderer/Path/PathSystem.hpp"

namespace DefectStudio
{
	// One window's worth of path picking. S10's PickPath answers "did this click hit THIS path";
	// this answers "which path did it hit", which is the question a viewport click actually asks.
	struct ScenePathPick
	{
		SceneObjectId path;
		PathPickResult result;
		// NDC depth of result.worldPosition: -1 at the near plane, +1 at the far one. Smaller is
		// nearer the camera. Reported so a caller arbitrating paths against atoms, bonds and planes
		// has the same number they do, rather than having to re-project the hit itself.
		float depth = 0.0f;
	};

	// nullopt when the cursor hit no path.
	//
	// Node positions are resolved against `bindings` - the caller's live context
	// (SceneSystem::MakePathBindingContext), the same one the render pass gets - so a bound node's
	// hitbox and pixels come from the same position: at its atom, not at its authored fallback.
	//
	// The evaluated polyline comes from PathCaches::FindLastBuilt, i.e. whatever the render pass
	// last built for that path, and is deliberately NOT re-tessellated here: a hitbox has to agree
	// with what is on screen, and a fresh tessellation at a guessed LOD would agree with neither
	// the cache nor the pixels.
	//   ponytail: a path the render pass has never reached has no cached polyline and therefore no
	//   shaft to hit. It still offers its endpoint decorations (which fall back to the node-to-node
	//   direction) and, in edit mode, its nodes and handles. In practice the gap lasts one frame
	//   after a path is created. Upgrade path if it ever matters: tessellate on a cache miss at the
	//   LOD bucket the cache entry would have had.
	//
	// Arbitration between paths is by `depth` - the frontmost wins. Not by distance to the cursor:
	// two paths crossing under the cursor are both at distance zero, and the one drawn in front is
	// the one the user is pointing at. An exact tie goes to the earlier path in store order.
	[[nodiscard]] std::optional<ScenePathPick> PickFrontmostScenePath(
		const PathSystem &paths, const PathPickSettings &settings, const BindingContext &bindings);
} // namespace DefectStudio
