#include "Core/dspch.hpp"

#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

namespace DefectStudio
{
	ProgressTracker::ProgressId ProgressTracker::Register(std::string label, std::size_t totalWork)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		const ProgressId id = ++m_NextId;
		m_Entries[id] = ProgressEntry{id, std::move(label), totalWork, 0, false};
		return id;
	}

	void ProgressTracker::Report(ProgressId id, std::size_t completedWork)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Entries.find(id);
		if (it == m_Entries.end())
			return;

		it->second.completedWork = completedWork;
	}

	void ProgressTracker::Finish(ProgressId id)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Entries.find(id);
		if (it == m_Entries.end())
			return;

		it->second.completedWork = it->second.totalWork;
		it->second.finished = true;
	}

	[[nodiscard]] std::vector<ProgressEntry> ProgressTracker::Snapshot() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		std::vector<ProgressEntry> entries;
		entries.reserve(m_Entries.size());
		for (const auto &[id, entry] : m_Entries)
			entries.push_back(entry);
		return entries;
	}

	[[nodiscard]] bool ProgressTracker::Has(ProgressId id) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Entries.find(id) != m_Entries.end();
	}
} // namespace DefectStudio