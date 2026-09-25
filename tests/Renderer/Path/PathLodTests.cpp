#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "Renderer/Path/PathLod.hpp"

namespace DefectStudio::Tests
{
	TEST(PathLodTests, QuantisesFiniteDensityAndRejectsInvalidDensity)
	{
		EXPECT_EQ(QuantiseLod(8.0, kNoLodBucket), 3);
		EXPECT_EQ(QuantiseLod(0.0, kNoLodBucket), kMinLodBucket);
		EXPECT_EQ(QuantiseLod(std::numeric_limits<double>::infinity(), kNoLodBucket), kMinLodBucket);
		EXPECT_EQ(QuantiseLod(std::pow(2.0, 40.0), kNoLodBucket), kMaxLodBucket);
	}

	TEST(PathLodTests, HysteresisDoesNotOscillateAcrossBoundaries)
	{
		int bucket = kNoLodBucket;
		int transitions = 0;
		for (int index = 0; index <= 200; ++index)
		{
			const int next = QuantiseLod(4.0 * std::pow(8.0, static_cast<double>(index) / 200.0), bucket);
			transitions += next != bucket && bucket != kNoLodBucket;
			bucket = next;
		}
		for (int index = 200; index >= 0; --index)
		{
			const int next = QuantiseLod(4.0 * std::pow(8.0, static_cast<double>(index) / 200.0), bucket);
			transitions += next != bucket;
			bucket = next;
		}
		EXPECT_LE(transitions, 6);
	}

	TEST(PathLodTests, ConvertsBucketsToFinitePositiveTolerances)
	{
		EXPECT_DOUBLE_EQ(ToleranceForLod(3, 2.0), 0.25);
		for (int bucket = kMinLodBucket; bucket <= kMaxLodBucket; ++bucket)
			EXPECT_TRUE(std::isfinite(ToleranceForLod(bucket, 1.0)) && ToleranceForLod(bucket, 1.0) > 0.0);
	}
} // namespace DefectStudio::Tests
