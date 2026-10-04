#include "Core/dspch.hpp"

#include "Renderer/Path/PathEditSession.hpp"

#include <algorithm>
#include <utility>
#include <variant>

namespace DefectStudio
{
	void PathEditSession::RequestInsert() noexcept
	{
		if (m_Active && !m_Insert)
			m_InsertRequested = true;
	}

	void PathEditSession::BeginInsert(const PathElementId segment) noexcept
	{
		m_InsertRequested = false;
		if (m_Active && segment.IsValid())
			m_Insert = InsertState{segment, 1};
	}

	void PathEditSession::ChangeInsertCount(const int delta) noexcept
	{
		if (m_Insert)
			m_Insert->count = static_cast<std::size_t>(std::clamp(static_cast<long long>(m_Insert->count) + delta, 1LL, 32LL));
	}

	void PathEditSession::CancelInsert() noexcept
	{
		m_InsertRequested = false;
		m_Insert.reset();
	}

	void PathEditSession::Enter(const SceneObjectId path)
	{
		if (!m_Active || m_Path != path)
		{
			m_Selection.clear();
			CancelInsert();
		}
		m_Active = true;
		m_Path = path;
	}

	void PathEditSession::Leave()
	{
		CancelInsert();
		m_Active = false;
		m_Path = {};
		m_Selection.clear();
	}

	bool PathEditSession::IsActive() const noexcept
	{
		return m_Active;
	}

	SceneObjectId PathEditSession::Path() const noexcept
	{
		return m_Path;
	}

	void PathEditSession::SetElementMode(const PathElementMode mode) noexcept
	{
		if (m_ElementMode == mode)
			return;
		m_ElementMode = mode;
		m_Selection.clear();
	}

	PathElementMode PathEditSession::ElementMode() const noexcept
	{
		return m_ElementMode;
	}

	void PathEditSession::SetSelection(std::vector<PathElementId> selection)
	{
		if (!m_Active)
			return;
		m_Selection = std::move(selection);
	}

	const std::vector<PathElementId> &PathEditSession::Selection() const noexcept
	{
		return m_Selection;
	}

	bool PathEditSession::IsSelected(const PathElementId element) const noexcept
	{
		return std::find(m_Selection.begin(), m_Selection.end(), element) != m_Selection.end();
	}

	PathElementId PathEditSession::ActiveElement() const noexcept
	{
		return m_Selection.empty() ? PathElementId{} : m_Selection.back();
	}

	void PathEditSession::ClearSelection() noexcept
	{
		m_Selection.clear();
	}

	void PathEditSession::PruneSelection(const ScenePath &path)
	{
		if (path.id != m_Path)
			return;

		const auto exists = [&path](const PathElementId id) {
			if (std::any_of(path.nodes.begin(), path.nodes.end(), [id](const PathNode &node) { return node.id == id; }))
				return true;
			if (std::any_of(path.segments.begin(), path.segments.end(), [id](const PathSegment &segment) { return segment.id == id; }))
				return true;
			return std::any_of(path.segments.begin(), path.segments.end(), [id](const PathSegment &segment) {
				const auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data);
				return cubic != nullptr && (cubic->startHandle.id == id || cubic->endHandle.id == id);
			});
		};

		std::erase_if(m_Selection, [&exists](const PathElementId id) { return !exists(id); });
	}
} // namespace DefectStudio
