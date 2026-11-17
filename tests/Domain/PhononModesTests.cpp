#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "Domain/Vibrations/PhononModes.hpp"
#include "Domain/Vibrations/VibrationField.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Two atoms (masses 1 and 4), six modes. Mode 0 is a translation, mode 3 is the one under
		// test: e_0 = (0.8, 0, 0), e_1 = (0, 0.6, 0), so participations 0.64 / 0.36 and physical
		// displacements u_0 = (0.8, 0, 0), u_1 = (0, 0.3, 0).
		PhononAnalysisResult TwoAtoms()
		{
			PhononAnalysisResult result;
			result.symbols = {"B", "N"};
			result.positionsAngstrom = {glm::dvec3(0.0), glm::dvec3(1.0, 2.0, 3.0)};
			result.massesAmu = {1.0, 4.0};
			for (int k = 0; k < 6; ++k)
			{
				PhononMode mode;
				mode.index = k;
				mode.frequencyMeV = k == 0 ? 0.0 : 10.0 * k;
				mode.isTranslation = k == 0;
				result.modes.push_back(mode);
			}
			result.eigenvectors.assign(6 * 2 * 3, 0.0f);
			result.participation.assign(6 * 2, 0.0f);
			auto set = [&](int mode, int atom, glm::dvec3 e) {
				for (int a = 0; a < 3; ++a)
					result.eigenvectors[(mode * 2 + atom) * 3 + a] = static_cast<float>(e[a]);
				result.participation[mode * 2 + atom] = static_cast<float>(glm::dot(e, e));
			};
			set(0, 0, glm::dvec3(std::sqrt(0.2), 0.0, 0.0));
			set(0, 1, glm::dvec3(std::sqrt(0.8), 0.0, 0.0));
			set(3, 0, glm::dvec3(0.8, 0.0, 0.0));
			set(3, 1, glm::dvec3(0.0, 0.6, 0.0));
			return result;
		}

		void ExpectVec(const glm::dvec3 &actual, const glm::dvec3 &expected)
		{
			EXPECT_NEAR(actual.x, expected.x, 1e-6);
			EXPECT_NEAR(actual.y, expected.y, 1e-6);
			EXPECT_NEAR(actual.z, expected.z, 1e-6);
		}
	} // namespace

	TEST(PhononModesTests, SparseServerRowsDriveArrowsAnimationParticipationAndFieldLikeDenseRows)
	{
		const auto dense = TwoAtoms();
		auto sparse = dense;
		sparse.hasEigenvectors = false; sparse.eigenvectors.clear(); sparse.participation.clear();
		auto vector = CreateRef<PhononModeVector>(); vector->modeIndex = 3;
		const auto rows = ModeRows(dense, 3);
		vector->eigenvector.assign(rows.eigenvector.begin(), rows.eigenvector.end());
		vector->participation.assign(rows.participation.begin(), rows.participation.end());
		sparse.fetchedModes[3] = vector;
		const auto arrows = BuildModeArrows(sparse, 3, 0, 1);
		const auto expected = BuildModeArrows(dense, 3, 0, 1);
		ASSERT_EQ(arrows.size(), expected.size());
		for (std::size_t i = 0; i < arrows.size(); ++i) ExpectVec(arrows[i].vectorAngstrom, expected[i].vectorAngstrom);
		EXPECT_EQ(AnimatedPositions(sparse, 3, 1, .2), AnimatedPositions(dense, 3, 1, .2));
		EXPECT_EQ(AtomsCarryingFraction(sparse, 3, .9), AtomsCarryingFraction(dense, 3, .9));
		EXPECT_EQ(DefectModeField(sparse, 3).real, DefectModeField(dense, 3).real);
		EXPECT_EQ(DefectModeField(sparse, 3).participation, DefectModeField(dense, 3).participation);
		EXPECT_TRUE(ModeRows(sparse, 2).eigenvector.empty());
		EXPECT_TRUE(ModeRows(sparse, 99).eigenvector.empty());
		EXPECT_TRUE(DefectModeField(sparse, 3, 4).real.empty());
		sparse.fetchedModes.clear();
		EXPECT_TRUE(BuildModeArrows(sparse, 3, 0, 1).empty());
		EXPECT_EQ(AnimatedPositions(sparse, 3, 1, .2), sparse.positionsAngstrom);
	}

	TEST(PhononModesTests, LargeServerCellUsesOnlyOneLinearSizedRow)
	{
		PhononAnalysisResult result;
		result.hasEigenvectors = false;
		result.symbols.assign(2001, "C"); result.massesAmu.assign(2001, 12.);
		result.positionsAngstrom.assign(2001, glm::dvec3(0.)); result.modes.resize(6003);
		auto row = CreateRef<PhononModeVector>(); row->modeIndex = 6002;
		row->eigenvector.assign(6003, 0.f); row->eigenvector[0] = 1.f;
		row->participation.assign(2001, 0.f); row->participation[0] = 1.f;
		result.fetchedModes[6002] = row;
		EXPECT_EQ(BuildModeArrows(result, 6002, .01, 1).size(), 1u);
		EXPECT_EQ(AtomsCarryingFraction(result, 6002, .9), 1u);
		EXPECT_EQ(DefectModeField(result, 6002).real.size(), 2001u);
		EXPECT_GT(AnimatedPositions(result, 6002, std::numbers::pi / 2, .1)[0].x, 0.);
		EXPECT_TRUE(result.eigenvectors.empty()); EXPECT_TRUE(result.participation.empty());
	}

	TEST(PhononModesTests, ArrowsFollowPhysicalDisplacementScaledToTheLongest)
	{
		const std::vector<ModeArrow> arrows = BuildModeArrows(TwoAtoms(), 3, 0.0, 1.0);
		ASSERT_EQ(arrows.size(), 2u);
		EXPECT_EQ(arrows[0].atomIndex, 0u);
		ExpectVec(arrows[0].vectorAngstrom, glm::dvec3(1.0, 0.0, 0.0));
		EXPECT_NEAR(arrows[0].participation, 0.64, 1e-6);
		EXPECT_EQ(arrows[1].atomIndex, 1u);
		ExpectVec(arrows[1].vectorAngstrom, glm::dvec3(0.0, 0.375, 0.0)); // 0.3 / 0.8
		EXPECT_NEAR(arrows[1].participation, 0.36, 1e-6);
	}

	TEST(PhononModesTests, ThresholdHidesArrowsWithoutRescalingTheRest)
	{
		const std::vector<ModeArrow> arrows = BuildModeArrows(TwoAtoms(), 3, 0.5, 2.0);
		ASSERT_EQ(arrows.size(), 1u);
		EXPECT_EQ(arrows[0].atomIndex, 0u);
		ExpectVec(arrows[0].vectorAngstrom, glm::dvec3(2.0, 0.0, 0.0));
	}

	TEST(PhononModesTests, ArrowsForAnOutOfRangeModeAreEmpty)
	{
		EXPECT_TRUE(BuildModeArrows(TwoAtoms(), 6, 0.0, 1.0).empty());
	}

	TEST(PhononModesTests, AtomsCarryingFractionCountsLargestParticipationsFirst)
	{
		const PhononAnalysisResult result = TwoAtoms();
		EXPECT_EQ(AtomsCarryingFraction(result, 3, 0.5), 1u);
		EXPECT_EQ(AtomsCarryingFraction(result, 3, 0.64), 1u);
		EXPECT_EQ(AtomsCarryingFraction(result, 3, 0.9), 2u);
		EXPECT_EQ(AtomsCarryingFraction(result, 3, 1.0), 2u);
		EXPECT_EQ(AtomsCarryingFraction(result, 99, 0.9), 0u);
	}

	TEST(PhononModesTests, AnimatedPositionsSwingTheLargestDisplacementByTheAmplitude)
	{
		const PhononAnalysisResult result = TwoAtoms();
		const std::vector<glm::dvec3> peak = AnimatedPositions(result, 3, std::numbers::pi / 2.0, 0.2);
		ASSERT_EQ(peak.size(), 2u);
		ExpectVec(peak[0], glm::dvec3(0.2, 0.0, 0.0));
		ExpectVec(peak[1], glm::dvec3(1.0, 2.075, 3.0)); // 0.2 * 0.3 / 0.8

		const std::vector<glm::dvec3> rest = AnimatedPositions(result, 3, 0.0, 0.2);
		ExpectVec(rest[0], result.positionsAngstrom[0]);
		ExpectVec(rest[1], result.positionsAngstrom[1]);
	}

	TEST(PhononModesTests, TranslationsAndOutOfRangeModesDoNotMove)
	{
		const PhononAnalysisResult result = TwoAtoms();
		for (const std::size_t mode : {std::size_t{0}, std::size_t{42}})
		{
			const std::vector<glm::dvec3> frame = AnimatedPositions(result, mode, std::numbers::pi / 2.0, 0.5);
			ASSERT_EQ(frame.size(), 2u);
			ExpectVec(frame[0], result.positionsAngstrom[0]);
			ExpectVec(frame[1], result.positionsAngstrom[1]);
		}
	}
} // namespace DefectStudio::Tests
