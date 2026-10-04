#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "Domain/Defects/DefectModel.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		void ExpectOrthonormalRightHanded(const DefectFrame &frame)
		{
			EXPECT_NEAR(glm::length(frame.x), 1.0f, 1e-5f);
			EXPECT_NEAR(glm::length(frame.y), 1.0f, 1e-5f);
			EXPECT_NEAR(glm::length(frame.z), 1.0f, 1e-5f);
			EXPECT_NEAR(glm::dot(frame.x, frame.y), 0.0f, 1e-5f);
			EXPECT_NEAR(glm::dot(frame.x, frame.z), 0.0f, 1e-5f);
			EXPECT_NEAR(glm::dot(frame.y, frame.z), 0.0f, 1e-5f);
			EXPECT_NEAR(glm::dot(glm::cross(frame.x, frame.y), frame.z), 1.0f, 1e-5f);
		}
	} // namespace

	// NV-: vacancy at the origin, N along [111]; z must run from V to N.
	TEST(DefectFrameTests, ZRunsFromOriginToTarget)
	{
		const glm::vec3 vacancy(1.0f, 2.0f, 3.0f);
		const glm::vec3 nitrogen = vacancy + glm::vec3(0.9f, 0.9f, 0.9f);
		const auto frame = MakeDefectFrame(vacancy, nitrogen);
		ASSERT_TRUE(frame) << frame.Error().technicalDetails;
		EXPECT_EQ(frame->origin, vacancy);
		const glm::vec3 expected = glm::normalize(glm::vec3(1.0f));
		EXPECT_NEAR(glm::dot(frame->z, expected), 1.0f, 1e-5f);
		ExpectOrthonormalRightHanded(*frame);
	}

	TEST(DefectFrameTests, XPointsTowardTheHintProjectedOffZ)
	{
		const auto frame = MakeDefectFrame(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(3.0f, 0.0f, 5.0f));
		ASSERT_TRUE(frame);
		EXPECT_NEAR(frame->x.x, 1.0f, 1e-5f);
		EXPECT_NEAR(frame->y.y, 1.0f, 1e-5f);
		ExpectOrthonormalRightHanded(*frame);
	}

	TEST(DefectFrameTests, HintOnTheZAxisFallsBackToACartesianAxis)
	{
		const auto withoutHint = MakeDefectFrame(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		const auto onAxis = MakeDefectFrame(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 4.0f));
		ASSERT_TRUE(withoutHint);
		ASSERT_TRUE(onAxis);
		EXPECT_NEAR(withoutHint->x.x, 1.0f, 1e-5f);
		EXPECT_EQ(onAxis->x, withoutHint->x);
		ExpectOrthonormalRightHanded(*onAxis);
	}

	TEST(DefectFrameTests, CoincidentPointsAreRejected)
	{
		const auto frame = MakeDefectFrame(glm::vec3(1.0f), glm::vec3(1.0f));
		ASSERT_FALSE(frame);
		EXPECT_EQ(frame.Error().code, "domain.defect_frame.degenerate_axis");
	}
} // namespace DefectStudio::Tests
