#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Renderer/Path/PathDash.hpp"

namespace DefectStudio::Tests
{
	TEST(PathDashTests, PatternIsAnchoredAtPathOriginAndPhaseWraps)
	{
		PathDashStyle dash;
		dash.enabled = true;
		dash.dashLength = 2.0f;
		dash.gapLength = 1.0f;
		const auto complete = BuildDashIntervals(0.0, 10.0, dash);
		const auto trimmed = BuildDashIntervals(1.0, 10.0, dash);
		ASSERT_EQ(trimmed.size(), 4u);
		EXPECT_NEAR(trimmed[0].start, 1.0, 1e-12);
		EXPECT_NEAR(trimmed[0].end, complete[0].end, 1e-12);
		dash.phase = -1.0f;
		const auto negative = BuildDashIntervals(0.0, 9.0, dash);
		dash.phase = 2.0f;
		const auto wrapped = BuildDashIntervals(0.0, 9.0, dash);
		EXPECT_EQ(negative.size(), wrapped.size());
		for (std::size_t index = 0; index < negative.size(); ++index)
		{
			EXPECT_NEAR(negative[index].start, wrapped[index].start, 1e-12);
			EXPECT_NEAR(negative[index].end, wrapped[index].end, 1e-12);
		}
	}

	TEST(PathDashTests, InvalidPatternsAreSolidAndCoverageSumsOnRuns)
	{
		PathDashStyle dash;
		dash.enabled = true;
		dash.dashLength = 1.0f;
		dash.gapLength = 3.0f;
		EXPECT_NEAR(DashCoverage(BuildDashIntervals(0.0, 16.0, dash)), 4.0, 1e-12);
		dash.gapLength = 0.0f;
		const auto solid = BuildDashIntervals(2.0, 5.0, dash);
		ASSERT_EQ(solid.size(), 1u);
		EXPECT_DOUBLE_EQ(solid[0].start, 2.0);
		EXPECT_DOUBLE_EQ(solid[0].end, 5.0);
		EXPECT_TRUE(BuildDashIntervals(2.0, 2.0, dash).empty());
		dash.gapLength = 1.0e-9f;
		dash.dashLength = 1.0e-9f;
		EXPECT_EQ(BuildDashIntervals(0.0, 1.0, dash).size(), 1u);
	}
} // namespace DefectStudio::Tests
