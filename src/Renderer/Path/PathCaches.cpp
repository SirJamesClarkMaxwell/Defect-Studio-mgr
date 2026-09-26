#include "Core/dspch.hpp"

#include "Renderer/Path/PathCaches.hpp"

#include <algorithm>
#include <utility>

namespace DefectStudio
{
	const CachedPathGeometry *PathCaches::Find(const SceneObjectId id, const PathEvaluationKey &key) const
	{
		const auto found = std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry &entry) { return entry.id == id; });
		return found == m_Entries.end() || found->key != key ? nullptr : &found->geometry;
	}

	const CachedPathGeometry *PathCaches::FindLastBuilt(const SceneObjectId id) const
	{
		const auto found = std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry &entry) { return entry.id == id; });
		return found == m_Entries.end() ? nullptr : &found->geometry;
	}

	const CachedPathGeometry &PathCaches::Store(SceneObjectId id, const PathEvaluationKey &key, CachedPathGeometry geometry)
	{
		static const CachedPathGeometry empty;
		if (!id.IsValid())
			return empty;
		const auto found = std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry &entry) { return entry.id == id; });
		if (found != m_Entries.end())
		{
			found->key = key;
			found->geometry = std::move(geometry);
			return found->geometry;
		}
		m_Entries.push_back({id, key, std::move(geometry)});
		return m_Entries.back().geometry;
	}

	bool PathCaches::Erase(const SceneObjectId id)
	{
		const auto found = std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry &entry) { return entry.id == id; });
		if (found == m_Entries.end())
			return false;
		m_Entries.erase(found);
		return true;
	}

	void PathCaches::Clear()
	{
		m_Entries.clear();
	}

	void PathCaches::RetainOnly(const std::vector<SceneObjectId> &live)
	{
		std::erase_if(m_Entries, [&live](const Entry &entry) {
			return std::find(live.begin(), live.end(), entry.id) == live.end();
		});
	}

	std::size_t PathCaches::Size() const
	{
		return m_Entries.size();
	}
} // namespace DefectStudio
