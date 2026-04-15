#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace DefectStudio
{
	struct ProgressEntry
	{
		std::uint64_t id = 0;
		std::string label;
		std::size_t totalWork = 0;
		std::size_t completedWork = 0;
		bool finished = false;
	};

	class ProgressTracker
	{
	public:
		using ProgressId = std::uint64_t;

		[[nodiscard]] ProgressId Register(std::string label, std::size_t totalWork);
		void Report(ProgressId id, std::size_t completedWork);
		void Finish(ProgressId id);
		[[nodiscard]] std::vector<ProgressEntry> Snapshot() const;
		[[nodiscard]] bool Has(ProgressId id) const;

	private:
		mutable std::mutex m_Mutex;
		std::map<ProgressId, ProgressEntry> m_Entries;
		ProgressId m_NextId = 0;
	};
} // namespace DefectStudio