#include "Core/dspch.hpp"

#include "Core/Logging/LogRegistry.hpp"

#include <sstream>

namespace DefectStudio
{
	namespace
	{
		std::string toThreadLabel(const std::thread::id &threadId)
		{
			std::ostringstream stream;
			stream << threadId;
			return stream.str();
		}
	}

	void LogRegistry::Append(LogEntry entry)
	{
		if (entry.threadLabel.empty())
			entry.threadLabel = toThreadLabel(entry.threadId);

		std::scoped_lock lock(m_Mutex);
		m_Entries.push_back(std::move(entry));
		if (m_Entries.size() > MaxEntries)
			m_Entries.erase(m_Entries.begin(), m_Entries.begin() + static_cast<std::ptrdiff_t>(m_Entries.size() - MaxEntries));
	}

	void LogRegistry::Clear()
	{
		std::scoped_lock lock(m_Mutex);
		m_Entries.clear();
	}

	std::vector<LogEntry> LogRegistry::Snapshot() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Entries;
	}

	std::size_t LogRegistry::Size() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Entries.size();
	}
} // namespace DefectStudio
