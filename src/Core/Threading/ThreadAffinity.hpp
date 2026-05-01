#pragma once

#include <thread>

#include "Core/Utils/Assert.hpp"

namespace DefectStudio::Threading
{
	void SetMainThread();
	[[nodiscard]] bool IsMainThread();
	[[nodiscard]] std::thread::id GetMainThreadId();
} // namespace DefectStudio::Threading

#define ASSERT_MAIN_THREAD() DS_ASSERT(::DefectStudio::Threading::IsMainThread(), "Expected main thread")