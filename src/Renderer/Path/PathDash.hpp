#pragma once

#include <vector>

#include "Renderer/Path/PathStyle.hpp"

namespace DefectStudio
{
	// One "on" run, in world arc length from the path start. Gaps are the complement - they are not
	// returned, because nothing downstream draws them.
	struct DashInterval
	{
		double start = 0.0;
		double end = 0.0;
	};

	// The shared dash-interval generator for path geometry.
	//
	// Contract:
	// - Intervals are ordered, disjoint, and clipped to [rangeStart, rangeEnd].
	// - A disabled pattern, or a non-positive / non-finite dash or gap length, yields the single
	//   interval [rangeStart, rangeEnd] - "not dashed" means one solid run, never zero runs.
	// - An empty or inverted range yields no intervals.
	// - `phase` is a world-space offset into the pattern evaluated at arc length 0, NOT at rangeStart:
	//   trimming a shaft for a decoration must not slide the dashes along it. Negative and
	//   larger-than-period phases wrap.
	// - Because the pattern is anchored at arc length 0 of the whole path, phase is continuous across
	//   a segment boundary for free; there is no per-segment restart.
	// - Interval count is bounded: a period far smaller than the range is rejected as a
	//   non-representable pattern and yields the single solid run rather than millions of dashes.
	[[nodiscard]] std::vector<DashInterval> BuildDashIntervals(
		double rangeStart, double rangeEnd, const PathDashStyle &dash);

	// Total "on" length of the intervals. Exposed because it is the natural coverage assertion and the
	// mesher needs it anyway to report an empty stroke.
	[[nodiscard]] double DashCoverage(const std::vector<DashInterval> &intervals);
}
