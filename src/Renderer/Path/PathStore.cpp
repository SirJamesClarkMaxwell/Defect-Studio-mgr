#include "Core/dspch.hpp"

#include "Renderer/Path/PathStore.hpp"

#include <algorithm>
#include <utility>

namespace DefectStudio
{
	bool PathStore::Insert(ScenePath path)
	{
		if (!path.id.IsValid() || Contains(path.id))
			return false;
		m_Paths.push_back({std::move(path), {1, 1}});
		return true;
	}

	bool PathStore::Erase(const SceneObjectId id)
	{
		const auto found = std::find_if(m_Paths.begin(), m_Paths.end(), [id](const Entry &entry) { return entry.path.id == id; });
		if (found == m_Paths.end())
			return false;
		m_Paths.erase(found);
		return true;
	}

	void PathStore::Clear()
	{
		m_Paths.clear();
	}

	const ScenePath *PathStore::Find(const SceneObjectId id) const
	{
		const auto found = std::find_if(m_Paths.begin(), m_Paths.end(), [id](const Entry &entry) { return entry.path.id == id; });
		return found == m_Paths.end() ? nullptr : &found->path;
	}

	bool PathStore::Contains(const SceneObjectId id) const
	{
		return Find(id) != nullptr;
	}

	bool PathStore::MutateGeometry(const SceneObjectId id, const std::function<void(ScenePath &)> &mutate)
	{
		const auto found = std::find_if(m_Paths.begin(), m_Paths.end(), [id](const Entry &entry) { return entry.path.id == id; });
		if (found == m_Paths.end())
			return false;
		mutate(found->path);
		++found->revisions.geometry;
		return true;
	}

	bool PathStore::MutateStyle(const SceneObjectId id, const std::function<void(ScenePath &)> &mutate)
	{
		const auto found = std::find_if(m_Paths.begin(), m_Paths.end(), [id](const Entry &entry) { return entry.path.id == id; });
		if (found == m_Paths.end())
			return false;
		mutate(found->path);
		++found->revisions.style;
		return true;
	}

	PathRevisions PathStore::RevisionsFor(const SceneObjectId id) const
	{
		const auto found = std::find_if(m_Paths.begin(), m_Paths.end(), [id](const Entry &entry) { return entry.path.id == id; });
		return found == m_Paths.end() ? PathRevisions{} : found->revisions;
	}

	void PathStore::Visit(const std::function<void(const ScenePath &)> &visit) const
	{
		for (const Entry &entry : m_Paths)
			visit(entry.path);
	}

	const ScenePath *PathStore::At(const std::size_t index) const
	{
		return index < m_Paths.size() ? &m_Paths[index].path : nullptr;
	}

	std::vector<SceneObjectId> PathStore::Ids() const
	{
		std::vector<SceneObjectId> ids;
		ids.reserve(m_Paths.size());
		for (const Entry &entry : m_Paths)
			ids.push_back(entry.path.id);
		return ids;
	}

	std::size_t PathStore::Size() const
	{
		return m_Paths.size();
	}

	bool PathStore::Empty() const
	{
		return m_Paths.empty();
	}
} // namespace DefectStudio
