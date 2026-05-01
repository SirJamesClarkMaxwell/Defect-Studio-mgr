#pragma once

#include <mutex>
#include <vector>

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	class LogRegistry
	{
	public:
		void Append(LogEntry entry);
		void Clear();
		[[nodiscard]] std::vector<LogEntry> Snapshot() const;
		[[nodiscard]] std::size_t Size() const;

	private:
		static constexpr std::size_t MaxEntries = 2000;

		mutable std::mutex m_Mutex;
		std::vector<LogEntry> m_Entries;
	};
} // namespace DefectStudio
