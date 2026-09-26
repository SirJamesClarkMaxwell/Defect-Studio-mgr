#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "Renderer/Path/PathTypes.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	// Two counters per path rather than one, because they invalidate different things: moving a node
	// invalidates the tessellation and everything downstream of it, while changing most style fields
	// only invalidates the mesh. Flat's ribbonNormal is the exception - it seeds the frame, so it
	// changes the tessellation too - but PathEvaluationKey carries both counters, so the cached
	// geometry still invalidates correctly. A live path always has both >= 1; {0, 0} means "no such
	// path".
	struct PathRevisions
	{
		std::uint64_t geometry = 0;
		std::uint64_t style = 0;

		[[nodiscard]] bool IsValid() const
		{
			return geometry != 0 && style != 0;
		}

		friend bool operator==(PathRevisions, PathRevisions) = default;
	};

	// The paths of one window, addressed by SceneObjectId. Copyable with plain value semantics - the
	// undo snapshot and the export preview each need a copy that is independent of the live window,
	// and the things that must NOT be copied (the caches) live in PathSystem, not here.
	//
	// Ids come from the window's SceneRegistry and are never allocated here: a path is a scene object
	// and shares the one id space with atoms, arrows and orbitals.
	class PathStore
	{
	public:
		// False when `path.id` is unset or already present. The store never renumbers a caller's id -
		// silently moving a path to a different id would break every reference to it.
		bool Insert(ScenePath path);
		bool Erase(SceneObjectId id);
		void Clear();

		// nullptr for an unknown id. The pointer is invalidated by any Insert or Erase.
		[[nodiscard]] const ScenePath *Find(SceneObjectId id) const;
		[[nodiscard]] bool Contains(SceneObjectId id) const;

		// The only mutable access there is. Both bump the matching revision after `mutate` returns, so
		// editing a path and forgetting to invalidate what was computed from it is not expressible.
		// False for an unknown id, and then `mutate` is not called.
		bool MutateGeometry(SceneObjectId id, const std::function<void(ScenePath &)> &mutate);
		bool MutateStyle(SceneObjectId id, const std::function<void(ScenePath &)> &mutate);

		// {0, 0} for an unknown id.
		[[nodiscard]] PathRevisions RevisionsFor(SceneObjectId id) const;

		// Insertion order, and Erase of one element does not reorder the rest: this order is the
		// source index the scene mirror hands to SceneObjectComponent.
		void Visit(const std::function<void(const ScenePath &)> &visit) const;
		[[nodiscard]] const ScenePath *At(std::size_t index) const;
		[[nodiscard]] std::vector<SceneObjectId> Ids() const;
		[[nodiscard]] std::size_t Size() const;
		[[nodiscard]] bool Empty() const;

	private:
		struct Entry
		{
			ScenePath path;
			PathRevisions revisions;
		};

		// A vector, not a map: a window holds a handful of paths, and a linear scan over contiguous
		// entries beats a hash lookup at that size while keeping the copy deep for free.
		std::vector<Entry> m_Paths;
	};
}
