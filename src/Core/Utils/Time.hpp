#pragma once

#include <chrono>

namespace DefectStudio
{
	struct Time
	{
		using Milliseconds = std::chrono::milliseconds;

		using Clock = std::chrono::system_clock;
		using TimePoint = Clock::time_point;

		using SteadyClock = std::chrono::steady_clock;
		using SteadyTimePoint = SteadyClock::time_point;

		[[nodiscard]] static TimePoint Now()
		{
			return Clock::now();
		}

		[[nodiscard]] static SteadyTimePoint NowSteady()
		{
			return SteadyClock::now();
		}
	};
} // namespace DefectStudio
