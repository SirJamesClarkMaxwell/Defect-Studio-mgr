#include "Core/dspch.hpp"

#include "Renderer/Path/PathSystem.hpp"

#include <utility>

namespace DefectStudio
{
	bool PathSystem::ErasePath(const SceneObjectId id)
	{
		if (!m_Store.Erase(id))
			return false;
		m_Caches.Erase(id);
		return true;
	}

	void PathSystem::ReplaceStore(PathStore store)
	{
		m_Store = std::move(store);
		m_Caches.Clear();
	}

	void PathSystem::Clear()
	{
		m_Store.Clear();
		m_Caches.Clear();
	}
} // namespace DefectStudio
