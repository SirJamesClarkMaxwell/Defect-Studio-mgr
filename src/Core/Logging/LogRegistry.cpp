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

	LogRegistry &LogRegistry::Get()
	{
		static LogRegistry s_Registry;
		return s_Registry;
	}

	void LogRegistry::Append(Entry entry)
	{
		if (entry.threadLabel.empty())
			entry.threadLabel = toThreadLabel(entry.threadId);

		std::scoped_lock lock(m_Mutex);
		m_Entries.push_back(std::move(entry));
	}

	void LogRegistry::Clear()
	{
		std::scoped_lock lock(m_Mutex);
		m_Entries.clear();
	}

	std::vector<LogRegistry::Entry> LogRegistry::Snapshot() const
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