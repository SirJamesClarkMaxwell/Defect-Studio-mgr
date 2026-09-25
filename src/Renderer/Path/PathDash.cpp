#include "Core/dspch.hpp"

#include "Renderer/Path/PathDash.hpp"

#include <cmath>
#include <limits>

namespace DefectStudio
{
	namespace
	{
		constexpr std::size_t kMaximumIntervals = 65536;

		[[nodiscard]] double WrapPhase(const double phase, const double period)
		{
			double wrapped = std::fmod(phase, period);
			if (wrapped < 0.0)
				wrapped += period;
			return wrapped;
		}
	} // namespace

	std::vector<DashInterval> BuildDashIntervals(const double rangeStart, const double rangeEnd, const PathDashStyle &dash)
	{
		if (!std::isfinite(rangeStart) || !std::isfinite(rangeEnd) || !(rangeEnd > rangeStart))
			return {};
		const auto solid = [&] { return std::vector<DashInterval>{{rangeStart, rangeEnd}}; };
		const double dashLength = static_cast<double>(dash.dashLength);
		const double gapLength = static_cast<double>(dash.gapLength);
		if (!dash.enabled || !std::isfinite(dashLength) || !std::isfinite(gapLength) || dashLength <= 0.0 || gapLength <= 0.0)
			return solid();
		const double period = dashLength + gapLength;
		if (!std::isfinite(period) || period <= 0.0 || (rangeEnd - rangeStart) / period > static_cast<double>(kMaximumIntervals))
			return solid();
		const double phase = std::isfinite(static_cast<double>(dash.phase)) ? WrapPhase(dash.phase, period) : 0.0;
		const double first = std::floor((rangeStart + phase) / period) * period - phase;
		std::vector<DashInterval> result;
		for (double start = first; start < rangeEnd; start += period)
		{
			const double clippedStart = std::max(start, rangeStart);
			const double clippedEnd = std::min(start + dashLength, rangeEnd);
			if (clippedEnd > clippedStart)
				result.push_back({clippedStart, clippedEnd});
		}
		return result;
	}

	double DashCoverage(const std::vector<DashInterval> &intervals)
	{
		double coverage = 0.0;
		for (const DashInterval &interval : intervals)
			if (std::isfinite(interval.start) && std::isfinite(interval.end) && interval.end > interval.start)
				coverage += interval.end - interval.start;
		return coverage;
	}
} // namespace DefectStudio
