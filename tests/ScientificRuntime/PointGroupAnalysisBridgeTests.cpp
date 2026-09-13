#include <gtest/gtest.h>

#include <cmath>

#include "Domain/Symmetry/PointGroupAnalysis.hpp"
#include "ScientificRuntime/Python/GroupTheoryBridge.hpp"

// No GTEST_SKIP anywhere in this file on purpose: a missing groupy must turn these RED. Task 19
// showed that skipping positives while negatives "pass" on `import groupy` failures proves nothing.

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr const char *kGroupySetupHint =
			"groupy missing from the app Python runtime. Setup: `uv pip install C:/Users/fzabi/Desktop/dev/groupy symengine` "
			"into .venv, then `python scripts/python/prepare_app_python_runtime.py` (see docs/work/project/tasks/23-group-theory-panel.md).";

		// NV- in diamond as it sits in a real supercell: vacancy at the origin, the four nearest
		// neighbours on the tetrahedral directions, N on [111]. The C3 axis is therefore [111], NOT z -
		// the bridge has to find groupy's frame itself.
		[[nodiscard]] PointGroupAnalysisRequest MakeNvCluster()
		{
			constexpr double kQuarter = 3.567 / 4.0;
			PointGroupAnalysisRequest request;
			request.sites = {
				BasisSite{"N0", glm::dvec3(1.0, 1.0, 1.0) * kQuarter, "N"},
				BasisSite{"C1", glm::dvec3(1.0, -1.0, -1.0) * kQuarter, "C"},
				BasisSite{"C2", glm::dvec3(-1.0, 1.0, -1.0) * kQuarter, "C"},
				BasisSite{"C3", glm::dvec3(-1.0, -1.0, 1.0) * kQuarter, "C"}};
			request.symmetryTolerance = 0.1;
			return request;
		}

		[[nodiscard]] const IrrepMultiplicity *FindIrrep(const PointGroupReduction &reduction, const std::string &label)
		{
			for (const IrrepMultiplicity &entry : reduction.decomposition)
				if (entry.irrepLabel == label)
					return &entry;
			return nullptr;
		}
	} // namespace

	TEST(PointGroupAnalysisBridgeTests, GroupyRuntimeIsInstalled)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result || result.Error().code != "python.groupy.not_installed") << kGroupySetupHint;
	}

	TEST(PointGroupAnalysisBridgeTests, DetectsC3vForIdealNvClusterOffAxis)
	{
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(MakeNvCluster());
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		EXPECT_TRUE(result->detection.ran);
		ASSERT_TRUE(result->detection.determined) << result->detection.reason;
		EXPECT_EQ(result->detection.pointGroupLabel, "C3v");
		EXPECT_DOUBLE_EQ(result->detection.tolerance, 0.1);

		// frameRotation must be a proper rotation that sends the [111] axis onto groupy's z.
		EXPECT_NEAR(glm::determinant(result->frameRotation), 1.0, 1e-6);
		const glm::dvec3 axis = result->frameRotation * glm::normalize(glm::dvec3(1.0, 1.0, 1.0));
		EXPECT_NEAR(std::abs(axis.z), 1.0, 1e-6);
	}

	TEST(PointGroupAnalysisBridgeTests, DetectsC3vForSlightlyRelaxedCluster)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.sites[1].position += glm::dvec3(0.02, -0.01, 0.0);
		request.sites[3].position += glm::dvec3(-0.01, 0.0, 0.015);
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		ASSERT_TRUE(result->detection.determined) << result->detection.reason;
		EXPECT_EQ(result->detection.pointGroupLabel, "C3v");
		EXPECT_EQ(result->reduction.decomposition.size(), 2u);
	}

	TEST(PointGroupAnalysisBridgeTests, DistortedClusterReportsLowerGroupNotError)
	{
		// Pull C1 0.4 Å outward along its own bond: the mirror y = z survives, C3 does not.
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.sites[1].position += glm::normalize(glm::dvec3(1.0, -1.0, -1.0)) * 0.4;
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		ASSERT_TRUE(result->detection.determined) << result->detection.reason;
		EXPECT_TRUE(result->detection.pointGroupLabel == "Cs" || result->detection.pointGroupLabel == "C1")
			<< result->detection.pointGroupLabel;
	}

	TEST(PointGroupAnalysisBridgeTests, CharacterTableAndReducibleCharactersForC3v)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		EXPECT_FALSE(result->detection.ran);

		const CharacterTable &table = result->characterTable;
		EXPECT_EQ(table.groupOrder, 6);
		EXPECT_EQ(table.classLabels, (std::vector<std::string>{"E", "2C3", "3sv"}));
		EXPECT_EQ(table.classSizes, (std::vector<int>{1, 2, 3}));
		EXPECT_EQ(table.irrepLabels, (std::vector<std::string>{"A1", "A2", "E"}));
		EXPECT_EQ(table.irrepDimensions, (std::vector<int>{1, 1, 2}));
		ASSERT_EQ(table.characters.size(), 3u);
		const std::vector<std::vector<double>> expected{{1, 1, 1}, {1, 1, -1}, {2, -1, 0}};
		for (std::size_t irrep = 0; irrep < 3; ++irrep)
		{
			ASSERT_EQ(table.characters[irrep].size(), 3u);
			for (std::size_t cls = 0; cls < 3; ++cls)
			{
				EXPECT_NEAR(table.characters[irrep][cls].numeric, expected[irrep][cls], 1e-9);
				EXPECT_FALSE(table.characters[irrep][cls].exact.empty());
			}
		}

		// Γ(N + 3C): E -> 4, C3 -> 1 (only N fixed), σv -> 2 (N + one C).
		ASSERT_EQ(result->reducibleCharacters.size(), 3u);
		EXPECT_NEAR(result->reducibleCharacters[0].numeric, 4.0, 1e-9);
		EXPECT_NEAR(result->reducibleCharacters[1].numeric, 1.0, 1e-9);
		EXPECT_NEAR(result->reducibleCharacters[2].numeric, 2.0, 1e-9);

		const IrrepMultiplicity *a1 = FindIrrep(result->reduction, "A1");
		const IrrepMultiplicity *e = FindIrrep(result->reduction, "E");
		ASSERT_NE(a1, nullptr);
		ASSERT_NE(e, nullptr);
		EXPECT_EQ(a1->multiplicity, 2);
		EXPECT_EQ(e->multiplicity, 1);
		EXPECT_EQ(result->reduction.projectedVectors.size(), 4u);
	}

	// Hard acceptance criterion of task 23. Exact table as groupy 's term_table returns it for
	// C3v, active {A1, E}, 4 electrons: 3A2, 3E, 1A1 (x2), 1E (x2), 15 states.
	TEST(PointGroupAnalysisBridgeTests, MultipletsForNvActiveSpace)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		request.activeOrbitalIrreps = {"A1", "E"};
		request.activeElectronCount = 4;
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;

		struct Expected
		{
			const char *irrep;
			int spinMultiplicity, dimension, perRow, total;
		};
		const std::vector<Expected> expected{{"A2", 3, 1, 1, 3}, {"E", 3, 2, 1, 6}, {"A1", 1, 1, 2, 2}, {"E", 1, 2, 2, 4}};
		ASSERT_EQ(result->multiplets.size(), expected.size());
		for (std::size_t index = 0; index < expected.size(); ++index)
		{
			const MultipletTerm &term = result->multiplets[index];
			EXPECT_EQ(term.irrepLabel, expected[index].irrep) << index;
			EXPECT_EQ(term.spinMultiplicity, expected[index].spinMultiplicity) << index;
			EXPECT_EQ(term.irrepDimension, expected[index].dimension) << index;
			EXPECT_EQ(term.countPerRow, expected[index].perRow) << index;
			EXPECT_EQ(term.totalStates, expected[index].total) << index;
		}
		EXPECT_EQ(result->multipletTotalStates, 15);
	}

	TEST(PointGroupAnalysisBridgeTests, NoMultipletsWithoutActiveSpace)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		EXPECT_TRUE(result->multiplets.empty());
		EXPECT_EQ(result->multipletTotalStates, 0);
	}

	// --- Negative cases. Each asserts the exact code, which proves the script got past
	// `import groupy` and failed at the guard named in the test. ---

	TEST(PointGroupAnalysisBridgeTests, RejectsEmptyBasis)
	{
		PointGroupAnalysisRequest request;
		request.pointGroupLabel = "C3v";
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.groupy.analysis.empty_basis");
	}

	TEST(PointGroupAnalysisBridgeTests, RejectsUnknownPointGroup)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "NotAGroup";
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.groupy.analysis.unknown_point_group");
	}

	TEST(PointGroupAnalysisBridgeTests, RejectsManualGroupTheSitesDoNotHave)
	{
		// Td needs four equivalent sites; N breaks it (element-aware matching), so either no frame
		// is found or the basis does not close - both are the right refusal, a result is not.
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "Td";
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_FALSE(result);
		EXPECT_TRUE(
			result.Error().code == "python.groupy.analysis.frame_alignment_failed" ||
			result.Error().code == "python.groupy.analysis.basis_not_closed")
			<< result.Error().code;
	}

	TEST(PointGroupAnalysisBridgeTests, RejectsInvalidActiveSpace)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		request.activeOrbitalIrreps = {"A1", "T2"}; // T2 does not exist in C3v
		request.activeElectronCount = 4;
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.groupy.analysis.invalid_active_space");
	}
} // namespace DefectStudio::Tests
