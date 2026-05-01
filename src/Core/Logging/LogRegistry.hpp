#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Core/Notifications/Notification.hpp"

namespace DefectStudio
{
	struct Entry
	{
		Notification notification;
		std::thread::id threadId{};
		std::string threadLabel;
	};
	class LogRegistry
	{
	public:

		static LogRegistry &Get();

		void Append(Entry entry);
		void Clear();
		[[nodiscard]] std::vector<Entry> Snapshot() const;
		[[nodiscard]] std::size_t Size() const;

	private:
		mutable std::mutex m_Mutex;
		std::vector<Entry> m_Entries;
	};
} // namespace DefectStudio