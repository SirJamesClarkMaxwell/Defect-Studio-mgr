#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "Renderer/Path/PathFrames.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		void ExpectFrame(const PathFrame &frame)
		{
			EXPECT_TRUE(std::isfinite(frame.tangent.x) && std::isfinite(frame.tangent.y) && std::isfinite(frame.tangent.z));
			EXPECT_NEAR(glm::length(frame.tangent), 1.0, 1.0e-9);
			EXPECT_NEAR(glm::length(frame.normal), 1.0, 1.0e-9);
			EXPECT_NEAR(glm::length(frame.binormal), 1.0, 1.0e-9);
			EXPECT_NEAR(glm::dot(frame.tangent, frame.normal), 0.0, 1.0e-9);
			EXPECT_NEAR(glm::dot(frame.tangent, frame.binormal), 0.0, 1.0e-9);
			EXPECT_NEAR(glm::dot(frame.normal, frame.binormal), 0.0, 1.0e-9);
			EXPECT_NEAR(glm::distance(glm::cross(frame.tangent, frame.normal), frame.binormal), 0.0, 1.0e-9);
		}
	}

	TEST(PathFramesTests, SeedsFixedNormalAndFallsBackForDegenerateInputs)
	{
		const PathFrame fixed = SeedFrame(glm::dvec3(1.0, 0.0, 0.0), {FrameSeed::Mode::FixedNormal, glm::dvec3(0.0, 0.0, 1.0)});
		ExpectFrame(fixed);
		EXPECT_NEAR(glm::distance(fixed.normal, glm::dvec3(0.0, 0.0, 1.0)), 0.0, 1.0e-9);

		ExpectFrame(SeedFrame(glm::dvec3(1.0, 0.0, 0.0), {FrameSeed::Mode::FixedNormal, glm::dvec3(1.0, 0.0, 0.0)}));
		ExpectFrame(SeedFrame(glm::dvec3(0.0), {}));
		ExpectFrame(SeedFrame(glm::dvec3(std::numeric_limits<double>::quiet_NaN()), {}));
	}

	TEST(PathFramesTests, TransportsByMinimalRotationThroughWorldUp)
	{
		PathFrame frame = SeedFrame(glm::dvec3(1.0, 0.0, 0.0), {});
		const glm::dvec3 tangents[] = {glm::normalize(glm::dvec3(1.0, 0.0, 1.0)), glm::dvec3(0.0, 0.0, 1.0), glm::normalize(glm::dvec3(-1.0, 0.0, 1.0))};
		for (const glm::dvec3 tangent : tangents)
		{
			const PathFrame next = TransportFrame(frame, tangent);
			ExpectFrame(next);
			EXPECT_GT(glm::dot(frame.normal, next.normal), 0.0);
			frame = next;
		}
	}
} // namespace DefectStudio::Tests
