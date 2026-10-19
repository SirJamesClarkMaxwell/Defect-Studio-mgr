#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

#include "Domain/Vibrations/VibrationField.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		PhononAnalysisResult Pair()
		{
			PhononAnalysisResult result;
			result.symbols = {"B", "N"};
			result.positionsAngstrom = {glm::dvec3(1, 2, 3), glm::dvec3(4, 5, 6)};
			result.massesAmu = {1, 4};
			for (int index = 0; index < 4; ++index)
			{
				PhononMode mode;
				mode.index = 57 + index;
				mode.frequencyMeV = index == 2 ? 100.2 : 100.0;
				mode.irrep = index == 3 ? "A1" : "E";
				mode.isTranslation = index == 0;
				result.modes.push_back(mode);
			}
			result.eigenvectors = {0, 0, 0, 0, 0, 0,
				0.8f, 0, 0, 0, 0.6f, 0,
				0, 0.8f, 0, 0.6f, 0, 0,
				0, 0, 1, 0, 0, 0};
			result.participation = {0, 0, 0.64f, 0.36f, 0.64f, 0.36f, 1, 0};
			return result;
		}

		void ExpectVec(const glm::dvec3 &actual, const glm::dvec3 &expected, double tolerance = 1e-9)
		{
			EXPECT_NEAR(actual.x, expected.x, tolerance);
			EXPECT_NEAR(actual.y, expected.y, tolerance);
			EXPECT_NEAR(actual.z, expected.z, tolerance);
		}
	} // namespace

	TEST(VibrationFieldTests, RealDefectFieldPreservesExistingFramesAndArrows)
	{
		const auto result = Pair();
		const auto field = DefectModeField(result, 1);
		EXPECT_EQ(field.symbols, result.symbols);
		EXPECT_EQ(field.equilibriumAngstrom, result.positionsAngstrom);
		EXPECT_TRUE(field.imag.empty());
		for (double phase : {0.0, 0.4, std::numbers::pi / 2.0, -std::numbers::pi / 2.0, std::numbers::pi})
		{
			const auto frame = AnimatedPositions(result, 1, phase, 0.2);
			const auto displacement = FieldDisplacements(field, phase, 0.2);
			ASSERT_EQ(displacement.size(), frame.size());
			for (std::size_t atom = 0; atom < frame.size(); ++atom)
				ExpectVec(displacement[atom], frame[atom] - result.positionsAngstrom[atom]);
		}
		for (double threshold : {0.0, static_cast<double>(0.36f), 0.5, 1.0})
		{
			const auto expected = BuildModeArrows(result, 1, threshold, 2.0);
			const auto actual = FieldArrows(field, std::nullopt, threshold, 2.0);
			ASSERT_EQ(actual.size(), expected.size());
			for (std::size_t atom = 0; atom < actual.size(); ++atom)
			{
				EXPECT_EQ(actual[atom].atomIndex, expected[atom].atomIndex);
				EXPECT_DOUBLE_EQ(actual[atom].participation, expected[atom].participation);
				ExpectVec(actual[atom].vectorAngstrom, expected[atom].vectorAngstrom);
			}
		}
	}

	TEST(VibrationFieldTests, CircularMotionHasConstantAmplitude)
	{
		VibrationField field;
		field.real = {glm::dvec3(1, 0, 0)};
		field.imag = {glm::dvec3(0, 1, 0)};
		EXPECT_DOUBLE_EQ(FieldCycleMaximum(field), 1.0);
		for (double phase : {0.0, 0.3, 1.5, 2.7, -1.0})
		{
			const auto displacement = FieldDisplacements(field, phase, 0.2);
			ASSERT_EQ(displacement.size(), 1u);
			EXPECT_NEAR(glm::length(displacement[0]), 0.2, 1e-12);
			ExpectVec(displacement[0], glm::dvec3(0.2 * std::sin(phase), 0.2 * std::cos(phase), 0));
		}
	}

	TEST(VibrationFieldTests, EllipseUsesOneNormalisationThroughoutTheCycle)
	{
		VibrationField field;
		field.real = {glm::dvec3(2, 0, 0)};
		field.imag = {glm::dvec3(0, 1, 0)};
		EXPECT_DOUBLE_EQ(FieldCycleMaximum(field), 2.0);
		for (double phase : {0.0, 0.4, std::numbers::pi / 2.0, -0.9})
			ExpectVec(FieldDisplacements(field, phase, 0.2)[0],
				glm::dvec3(0.2 * std::sin(phase), 0.1 * std::cos(phase), 0));
	}

	TEST(VibrationFieldTests, NonOrthogonalQuadraturesAndAllAtomsDetermineCycleMaximum)
	{
		VibrationField field;
		field.real = {glm::dvec3(1, 0, 0), glm::dvec3(0, 0, 0)};
		field.imag = {glm::dvec3(1, 0, 0), glm::dvec3(0, 0, 1)};
		EXPECT_NEAR(FieldCycleMaximum(field), std::sqrt(2.0), 1e-12);
		ExpectVec(FieldDisplacements(field, std::numbers::pi / 4.0, 0.3)[0], glm::dvec3(0.3, 0, 0));
		ExpectVec(FieldDisplacements(field, 0.0, 0.3)[1], glm::dvec3(0, 0, 0.3 / std::sqrt(2.0)));
		field.imag[1].z = 2.0;
		EXPECT_DOUBLE_EQ(FieldCycleMaximum(field), 2.0);
	}

	TEST(VibrationFieldTests, PhaseArrowsUseCurrentDirectionAndThresholdDoesNotRescale)
	{
		VibrationField field;
		field.real = {glm::dvec3(2, 0, 0), glm::dvec3(1, 0, 0)};
		field.imag = {glm::dvec3(0, 2, 0), glm::dvec3(0, 1, 0)};
		field.participation = {0.2, 0.8};
		const auto arrows = FieldArrows(field, 0.0, 0.5, 3.0);
		ASSERT_EQ(arrows.size(), 1u);
		EXPECT_EQ(arrows[0].atomIndex, 1u);
		ExpectVec(arrows[0].vectorAngstrom, glm::dvec3(0, 1.5, 0));
		const auto real = FieldArrows(field, std::nullopt, 0.5, 3.0);
		ASSERT_EQ(real.size(), 1u);
		ExpectVec(real[0].vectorAngstrom, glm::dvec3(1.5, 0, 0));
		field.participation.clear();
		const auto uniform = FieldArrows(field, std::numbers::pi / 4.0, 0.5, 3.0);
		ASSERT_EQ(uniform.size(), 2u);
		EXPECT_DOUBLE_EQ(uniform[0].participation, 0.5);
		EXPECT_NEAR(glm::length(uniform[0].vectorAngstrom), 3.0, 1e-12);
		EXPECT_TRUE(FieldArrows(field, std::nullopt, 0.6, 3.0).empty());
	}

	TEST(VibrationFieldTests, MixingEndpointsAndOrthonormalPairParticipation)
	{
		const auto result = Pair();
		const auto first = DefectModeField(result, 1);
		const auto second = DefectModeField(result, 2);
		const auto zero = DefectModeField(result, 1, 2, 0.0);
		const auto right = DefectModeField(result, 1, 2, std::numbers::pi / 2.0);
		const auto diagonal = DefectModeField(result, 1, 2, std::numbers::pi / 4.0);
		ASSERT_EQ(diagonal.real.size(), 2u);
		for (std::size_t atom = 0; atom < 2; ++atom)
		{
			ExpectVec(zero.real[atom], first.real[atom]);
			ExpectVec(right.real[atom], second.real[atom]);
			ExpectVec(diagonal.real[atom], (first.real[atom] + second.real[atom]) / std::sqrt(2.0));
		}
		EXPECT_NEAR(std::accumulate(diagonal.participation.begin(), diagonal.participation.end(), 0.0), 1.0, 1e-6);
		EXPECT_NE(diagonal.label.find("58/59"), std::string::npos);
	}

	TEST(VibrationFieldTests, MixingParticipationIncludesInterferenceBeforeMassCorrection)
	{
		auto result = Pair();
		result.eigenvectors = {0, 0, 0, 0, 0, 0,
			0.6f, 0, 0, 0.8f, 0, 0,
			0.8f, 0, 0, -0.6f, 0, 0,
			0, 0, 0, 0, 0, 0};
		const auto mixed = DefectModeField(result, 1, 2, std::numbers::pi / 4.0);
		ASSERT_EQ(mixed.participation.size(), 2u);
		EXPECT_NEAR(mixed.participation[0], 0.98, 1e-6);
		EXPECT_NEAR(mixed.participation[1], 0.02, 1e-6);
		EXPECT_NEAR(mixed.real[1].x, 0.1 / std::sqrt(2.0), 1e-6);
	}

	TEST(VibrationFieldTests, PartnersAreAdjacentAndExcludeTranslationsAndDifferentIrreps)
	{
		const auto result = Pair();
		EXPECT_EQ(DegeneratePartner(result, 1), 2u);
		EXPECT_EQ(DegeneratePartner(result, 2), 1u);
		EXPECT_FALSE(DegeneratePartner(result, 0));
		EXPECT_FALSE(DegeneratePartner(result, 3));
		EXPECT_FALSE(DegeneratePartner(result, 99));
	}

	TEST(VibrationFieldTests, AdjacentDifferentPairsWithTheSameIrrepAreNotPartners)
	{
		auto result = Pair();
		result.modes[3].irrep = "E";
		result.modes[3].frequencyMeV = 120.0;
		EXPECT_EQ(DegeneratePartner(result, 2), 1u);
		EXPECT_FALSE(DegeneratePartner(result, 3));
	}

	TEST(VibrationFieldTests, MissingIrrepsUseFrequencyToleranceAndNeverCrossTranslation)
	{
		auto result = Pair();
		for (auto &mode : result.modes) mode.irrep.clear();
		result.modes[3].frequencyMeV = 102.0;
		EXPECT_EQ(DegeneratePartner(result, 1), 2u);
		EXPECT_EQ(DegeneratePartner(result, 2), 1u);
		EXPECT_FALSE(DegeneratePartner(result, 1, 0.1));
		EXPECT_FALSE(DegeneratePartner(result, 3));
		result.modes[2].irrep = "E";
		EXPECT_FALSE(DegeneratePartner(result, 1));
		EXPECT_FALSE(DegeneratePartner(result, 2));
	}

	TEST(VibrationFieldTests, ThreefoldUsesNextMemberAndLabelsTwoMemberLimit)
	{
		auto result = Pair();
		result.modes[3].irrep = "E";
		EXPECT_EQ(DegeneratePartner(result, 1), 2u);
		EXPECT_EQ(DegeneratePartner(result, 2), 3u);
		EXPECT_EQ(DegeneratePartner(result, 3), 2u);
		const auto field = DefectModeField(result, 1, 2);
		EXPECT_NE(field.label.find("tylko dwóch modów"), std::string::npos);
	}

	TEST(VibrationFieldTests, EmptyZeroAndInvalidFieldsDoNotProduceNonfiniteMotion)
	{
		EXPECT_DOUBLE_EQ(FieldCycleMaximum({}), 0.0);
		EXPECT_TRUE(FieldDisplacements({}, 0, 0.2).empty());
		VibrationField field;
		field.real = {glm::dvec3(0)};
		EXPECT_DOUBLE_EQ(FieldCycleMaximum(field), 0.0);
		ExpectVec(FieldDisplacements(field, 1, 0.2)[0], glm::dvec3(0));
		field.real[0].x = 1.0;
		const auto nan = std::numeric_limits<double>::quiet_NaN();
		ExpectVec(FieldDisplacements(field, nan, 0.2)[0], glm::dvec3(0));
		ExpectVec(FieldDisplacements(field, 0.5, nan)[0], glm::dvec3(0));
		EXPECT_TRUE(FieldArrows(field, nan, 0, 1).empty());
		EXPECT_TRUE(FieldArrows(field, std::nullopt, 0, -1).empty());
		field.imag = {glm::dvec3(0), glm::dvec3(0)};
		EXPECT_DOUBLE_EQ(FieldCycleMaximum(field), 0.0);
		EXPECT_TRUE(FieldArrows(field, std::nullopt, 0, 1).empty());
		field.imag.clear();
		field.real[0].x = nan;
		EXPECT_DOUBLE_EQ(FieldCycleMaximum(field), 0.0);
		ExpectVec(FieldDisplacements(field, 0.5, 0.2)[0], glm::dvec3(0));
	}

	TEST(VibrationFieldTests, CcdSweepUsesPhysicalDisplacementAndOscillationUsesCycleNormalisation)
	{
		CcdDisplacement data;
		data.symbols = {"H", "He"};
		data.masses = {1, 4};
		data.gsPositions = {glm::dvec3(3.8, 0, 0), glm::dvec3(1.6, 0, 0)};
		data.displacement = {glm::dvec3(0.32, 0, 0), glm::dvec3(-0.08, 0, 0)};
		data.deltaQ = std::sqrt(0.128);
		auto field = CcdDirectionField(data);
		ASSERT_EQ(field.real.size(), 2u);
		EXPECT_EQ(field.real, data.displacement);
		EXPECT_EQ(field.equilibriumAngstrom, data.gsPositions);
		EXPECT_EQ(field.motion, VibrationMotion::Sweep);
		EXPECT_NEAR(std::accumulate(field.participation.begin(), field.participation.end(), 0.0), 1.0, 1e-12);
		EXPECT_NEAR(field.participation[0], 0.8, 1e-12);
		ExpectVec(FieldArrows(field, std::numbers::pi, 0, 1)[0].vectorAngstrom, glm::dvec3(1, 0, 0));
		for (std::size_t atom = 0; atom < 2; ++atom)
		{
			ExpectVec(FieldDisplacements(field, 0, 100)[atom], glm::dvec3(0));
			ExpectVec(FieldDisplacements(field, std::numbers::pi, 0)[atom], data.displacement[atom]);
			ExpectVec(FieldDisplacements(field, std::numbers::pi, -7, 1.5)[atom], 1.5 * data.displacement[atom]);
			ExpectVec(FieldDisplacements(field, 2 * std::numbers::pi, 0)[atom], glm::dvec3(0));
		}
		field.motion = VibrationMotion::Oscillate;
		ExpectVec(FieldDisplacements(field, std::numbers::pi / 2, 0.2)[0], glm::dvec3(0.2, 0, 0));
		ExpectVec(FieldDisplacements(field, -std::numbers::pi / 2, 0.2)[1], glm::dvec3(0.05, 0, 0));
		data.deltaQ *= 2;
		EXPECT_TRUE(CcdDirectionField(data).real.empty());
		data.deltaQ = 0;
		EXPECT_TRUE(CcdDirectionField(data).real.empty());
	}

	TEST(VibrationFieldTests, InvalidDefectDataReturnsEmptyField)
	{
		auto result = Pair();
		EXPECT_TRUE(DefectModeField(result, 0).real.empty());
		EXPECT_TRUE(DefectModeField(result, 99).real.empty());
		EXPECT_TRUE(DefectModeField(result, 1, 0).real.empty());
		EXPECT_TRUE(DefectModeField(result, 1, 99).real.empty());
		result.massesAmu[0] = 0;
		EXPECT_TRUE(DefectModeField(result, 1).real.empty());
		result = Pair();
		result.eigenvectors.resize(3);
		EXPECT_TRUE(DefectModeField(result, 1).real.empty());
		result = Pair();
		result.positionsAngstrom.clear();
		EXPECT_TRUE(DefectModeField(result, 1).real.empty());
		result = Pair();
		result.eigenvectors[6] = std::numeric_limits<float>::infinity();
		EXPECT_TRUE(DefectModeField(result, 1).real.empty());
	}
} // namespace DefectStudio::Tests
