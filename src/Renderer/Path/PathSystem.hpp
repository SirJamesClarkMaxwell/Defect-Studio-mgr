#pragma once

#include "Renderer/Path/PathCaches.hpp"
#include "Renderer/Path/PathStore.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	// One window's paths: the copyable store plus the caches that must not travel with a copy of it.
	// Non-copyable for that reason, and held by RendererWindowState as a Unique so that a window
	// vector reallocation moves one pointer - every reference into the system, and every cached
	// entry, stays exactly where it was.
	//
	// Deliberately thin. It exists to make the two halves inseparable in one direction (a store copy
	// never drags a cache along) and joined in the other (erasing a path also drops its geometry),
	// not to become the place path logic accumulates.
	class PathSystem
	{
	public:
		PathSystem() = default;
		PathSystem(const PathSystem &) = delete;
		PathSystem &operator=(const PathSystem &) = delete;

		[[nodiscard]] PathStore &Store()
		{
			return m_Store;
		}

		[[nodiscard]] const PathStore &Store() const
		{
			return m_Store;
		}

		[[nodiscard]] PathCaches &Caches()
		{
			return m_Caches;
		}

		[[nodiscard]] const PathCaches &Caches() const
		{
			return m_Caches;
		}

		// Erase through here, not through Store(): erasing there leaves the dead id's geometry in the
		// cache until something happens to sweep it.
		bool ErasePath(SceneObjectId id);

		// Replaces the store wholesale - undo restore, export preview fill - and clears the cache.
		// Keeping the matching ids would be tempting and wrong: revisions count per store, so undo
		// down one edit and redo up a different one reaches the same revision number with different
		// content, and the key would match geometry that is no longer that path. One recompute after
		// an undo is the cheaper side of that trade.
		void ReplaceStore(PathStore store);

		void Clear();

	private:
		PathStore m_Store;
		PathCaches m_Caches;
	};
}
