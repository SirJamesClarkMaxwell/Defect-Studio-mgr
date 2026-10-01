#pragma once

#include <cstddef>
#include <cstdint>
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
	// task/41 transform-1: this is where `path.transform` is applied, and the only place. Everything
	// downstream - tessellation, stroke meshing, picking, the caches - consumes what this returns
	// and never reads `node.position`, so nothing below learns that a path can be transformed.
	//
	// The two spaces, and this is the rule the whole transform rests on:
	//
	//     Free node   -> world = transform * node.position      (authored, local)
	//     Bound node  -> world = whatever the binding resolves to, UNTRANSFORMED
	//
	// A bound node is pinned to an atom, a bond midpoint or another object; those are world
	// positions by nature, and that is the entire point of binding it. So one path can hold nodes in
	// two spaces at once, and a path whose nodes are all bound does not travel when it is
	// translated - it deforms, because the bindings win. That looks like a bug the first time and it
	// is the correct behaviour.
	//
	// A handle is an offset from its own node (PathTypes.hpp). Its offset is rotated and scaled by
	// the transform - it is authored geometry - but it is anchored at whatever world position its
	// node resolved to. So a handle on a bound node follows the atom while keeping the shape the
	// author gave it.
	//
	// The identity transform leaves every resolved position exactly where it was before paths had
	// one, which is what makes existing files load unchanged.
	[[nodiscard]] ResolvedNodes ResolveNodePositions(const ScenePath &path, const BindingContext &context);

	// S14: the cache-key component for what a path's BOUND nodes resolved to
	// (PathEvaluationKey::bindingSourceRevision).
	//
	// 0 for a path with no bound node, so a free path keeps the key it has always had. Otherwise a
	// hash of the resolved positions of the bound nodes (bit patterns, in node order): it changes
	// exactly when an atom or object a node follows moves - or when a binding breaks and its node
	// falls back to the authored position - and is equal for equal positions. That is what lets the
	// render cache follow atom edits without a structure revision counter threaded through the
	// window. A non-zero hash is forced for bound paths so it can never collide with "no bindings".
	[[nodiscard]] std::uint64_t BindingSourceRevision(const ScenePath &path, const ResolvedNodes &resolved);
}
