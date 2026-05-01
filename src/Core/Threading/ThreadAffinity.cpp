#include "Core/dspch.hpp"

#include "Core/Threading/ThreadAffinity.hpp"

#include <mutex>

namespace DefectStudio::Threading
{
	namespace
	{
		std::mutex g_MainThreadMutex;
		std::thread::id g_MainThreadId;
	}

	void SetMainThread()
	{
		std::scoped_lock lock(g_MainThreadMutex);
		g_MainThreadId = std::this_thread::get_id();
	}

	bool IsMainThread()
	{
		const auto currentThreadId = std::this_thread::get_id();

		std::scoped_lock lock(g_MainThreadMutex);
		if (g_MainThreadId == std::thread::id{})
		{
			g_MainThreadId = currentThreadId;
			return true;
		}

		return g_MainThreadId == currentThreadId;
	}

	std::thread::id GetMainThreadId()
	{
		std::scoped_lock lock(g_MainThreadMutex);
		return g_MainThreadId;
	}
} // namespace DefectStudio::Threading
