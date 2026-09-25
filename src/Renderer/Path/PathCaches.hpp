#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Renderer/Path/PathLod.hpp"
#include "Renderer/Path/PathStore.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	// Everything that decides whether what was computed last frame is still correct. The binding
	// source revision is part of the key because a path bound to an atom moves when that atom moves
	// without the path itself being touched - resolving never writes back into the path (plan v2 C8),
	// so nothing on the path would record that anything happened.
	struct PathEvaluationKey
	{
		PathRevisions revisions;
		std::uint64_t bindingSourceRevision = 0;
		int lodBucket = kNoLodBucket;

		friend bool operator==(const PathEvaluationKey &, const PathEvaluationKey &) = default;
	};

	struct CachedPathGeometry
	{
		EvaluatedPath evaluated;
		StrokeGeometry stroke;
	};

	// One slot per path id: storing under a new key overwrites that id's entry instead of growing a
	// history, so a slow zoom across LOD buckets cannot make this unbounded.
	//
	// Correctness does not depend on eviction. A stale entry is one whose key no longer matches, and
	// a key mismatch is a miss - Erase, RetainOnly and Clear are about memory and dead ids only.
	//
	// Move-only: a cache that silently came along with a copy of its store would hand the copy
	// geometry it never computed, keyed by revisions that mean something else in that copy.
	class PathCaches
	{
	public:
		PathCaches() = default;
		PathCaches(const PathCaches &) = delete;
		PathCaches &operator=(const PathCaches &) = delete;
		PathCaches(PathCaches &&) = default;
		PathCaches &operator=(PathCaches &&) = default;

		// nullptr on a miss: unknown id, or a key that differs in any field. The pointer is
		// invalidated by the next Store, Erase, RetainOnly or Clear.
		[[nodiscard]] const CachedPathGeometry *Find(SceneObjectId id, const PathEvaluationKey &key) const;

		// Replaces whatever that id held. Returns the stored entry so a caller that just missed can
		// use the result without a second lookup. An invalid id is not stored, and then the returned
		// reference is to a shared empty entry.
		const CachedPathGeometry &Store(SceneObjectId id, const PathEvaluationKey &key, CachedPathGeometry geometry);

		bool Erase(SceneObjectId id);
		void Clear();

		// Drops every entry whose id is not in `live`. The sweep for the cases where the store lost
		// paths while the cache was not looking: undo restore, window close, preview teardown.
		void RetainOnly(const std::vector<SceneObjectId> &live);

		[[nodiscard]] std::size_t Size() const;

	private:
		struct Entry
		{
			SceneObjectId id;
			PathEvaluationKey key;
			CachedPathGeometry geometry;
		};

		std::vector<Entry> m_Entries;
	};
}
