#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include <glm/glm.hpp>

#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathTypes.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	// A narrow view of the scene, injected rather than reached for: the resolver stays pure and is
	// testable without a window, a registry or a structure.
	struct BindingContext
	{
		std::function<std::optional<glm::vec3>(std::size_t)> atomPosition;
		std::function<std::optional<float>(std::size_t)> atomRadius;
		std::function<std::optional<glm::vec3>(SceneObjectId)> objectOrigin;
		std::function<bool(SceneObjectId)> isScenePath;
	};

	// Authored -> resolved. Never mutates the path: a broken binding falls back to the authored
	// position and reports BrokenBinding (plan v2 C8).
	//
	// Endpoint CopyPosition may carry a buffer; it offsets the resolved position toward the sole
	// neighbour's UNBUFFERED position, with the existing non-inversion clamp (SceneSystem.cpp:328
	// semantics). An interior node with buffer != 0 keeps the unbuffered position and reports
	// InteriorNodeBuffer - two neighbours make the direction undefined.
	[[nodiscard]] ResolvedNodes ResolveNodePositions(const ScenePath &path, const BindingContext &context);
}
