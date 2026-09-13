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
		EXPECT_EQ(result->tensorPower, 0);
		EXPECT_TRUE(result->tensorPowerDecomposition.empty());
	}

	// Γ = 2A1 ⊕ E, χ = (4, 1, 2). χ² = (16, 1, 4) -> 5A1 ⊕ A2 ⊕ 5E; χ⁶ = (4096, 1, 64) -> 715A1 ⊕ 651A2 ⊕ 1365E.
	// Needs only the electron count - no active irreps, and no multiplets come out of it.
	TEST(PointGroupAnalysisBridgeTests, TensorPowerOfNvBasis)
	{
		const auto check = [](int electrons, const std::vector<IrrepMultiplicity> &expected) {
			PointGroupAnalysisRequest request = MakeNvCluster();
			request.pointGroupLabel = "C3v";
			request.activeElectronCount = electrons;
			const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
			ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
			EXPECT_EQ(result->tensorPower, electrons);
			EXPECT_TRUE(result->multiplets.empty());
			ASSERT_EQ(result->tensorPowerDecomposition.size(), expected.size()) << electrons;
			for (std::size_t index = 0; index < expected.size(); ++index)
			{
				EXPECT_EQ(result->tensorPowerDecomposition[index].irrepLabel, expected[index].irrepLabel);
				EXPECT_EQ(result->tensorPowerDecomposition[index].multiplicity, expected[index].multiplicity);
				EXPECT_EQ(result->tensorPowerDecomposition[index].dimension, expected[index].dimension);
			}
		};
		check(2, {{"A1", 5, 1}, {"A2", 1, 1}, {"E", 5, 2}});
		check(6, {{"A1", 715, 1}, {"A2", 651, 1}, {"E", 1365, 2}});
	}

	TEST(PointGroupAnalysisBridgeTests, ProjectedCoefficientsCarryLatex)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		bool anyRadical = false;
		for (const SymmetryAdaptedVector &vector : result->reduction.projectedVectors)
			for (const ExactCoefficient &coefficient : vector.coefficients)
			{
				EXPECT_FALSE(coefficient.latex.empty()) << coefficient.exact;
				anyRadical = anyRadical || coefficient.latex.find("\\sqrt") != std::string::npos;
			}
		EXPECT_TRUE(anyRadical);
		EXPECT_FALSE(result->characterTable.characters.at(0).at(0).latex.empty());
	}

	// NV- with the full dangling-bond space: a1, a1', e (8 spin-orbitals), 6 electrons -> C(8,6) = 28 states.
	TEST(PointGroupAnalysisBridgeTests, MultipletsForNvSixElectrons)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		request.activeOrbitalIrreps = {"A1", "A1", "E"};
		request.activeElectronCount = 6;
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		EXPECT_EQ(result->multipletTotalStates, 28);
		bool hasTripletA2 = false;
		for (const MultipletTerm &term : result->multiplets)
			hasTripletA2 = hasTripletA2 || (term.irrepLabel == "A2" && term.spinMultiplicity == 3);
		EXPECT_TRUE(hasTripletA2);
		EXPECT_EQ(result->tensorPower, 6);
	}

	// a1² e² ³A₂ for active {A1, E}, 4 e⁻. Every (row, m_s, copy) state is listed once, so their count
	// equals multipletTotalStates.
	TEST(PointGroupAnalysisBridgeTests, WavefunctionsForNvTripletA2)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		request.activeOrbitalIrreps = {"A1", "E"};
		request.activeElectronCount = 4;
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		EXPECT_TRUE(result->wavefunctionsSkippedReason.empty());
		EXPECT_EQ(static_cast<int>(result->wavefunctions.size()), result->multipletTotalStates);

		ASSERT_EQ(result->activeShells.size(), 2u);
		EXPECT_EQ(result->activeShells[0].irrepLabel, "A1");
		EXPECT_EQ(result->activeShells[0].dimension, 1);
		EXPECT_EQ(result->activeShells[1].irrepLabel, "E");
		EXPECT_EQ(result->activeShells[1].firstOrbital, 1);
		EXPECT_EQ(result->activeShells[1].dimension, 2);
		ASSERT_EQ(result->activeOrbitalLabels.size(), 3u);

		std::vector<const MultipletWavefunction *> triplet;
		for (const MultipletWavefunction &state : result->wavefunctions)
			if (state.irrepLabel == "A2" && state.spinMultiplicity == 3)
				triplet.push_back(&state);
		ASSERT_EQ(triplet.size(), 3u);
		EXPECT_EQ(triplet[0]->twiceMs, 2);
		EXPECT_EQ(triplet[1]->twiceMs, 0);
		EXPECT_EQ(triplet[2]->twiceMs, -2);
		for (const MultipletWavefunction *state : triplet)
		{
			EXPECT_EQ(state->configuration, (std::vector<int>{2, 2}));
			EXPECT_EQ(state->irrepRow, 0);
			EXPECT_EQ(state->copyIndex, 0);
		}

		// m_s = +1: the single determinant |a1 ā1 ex ey|.
		ASSERT_EQ(triplet[0]->determinants.size(), 1u);
		const std::vector<SpinOrbital> &occupied = triplet[0]->determinants[0].occupied;
		ASSERT_EQ(occupied.size(), 4u);
		EXPECT_EQ(occupied[0].orbitalIndex, 0);
		EXPECT_TRUE(occupied[0].spinUp);
		EXPECT_EQ(occupied[1].orbitalIndex, 0);
		EXPECT_FALSE(occupied[1].spinUp);
		EXPECT_TRUE(occupied[2].spinUp);
		EXPECT_TRUE(occupied[3].spinUp);
		EXPECT_NEAR(std::abs(triplet[0]->determinants[0].coefficient.numeric), 1.0, 1e-9);

		// m_s = 0: two determinants with |c| = 1/√2.
		ASSERT_EQ(triplet[1]->determinants.size(), 2u);
		for (const DeterminantTerm &term : triplet[1]->determinants)
		{
			EXPECT_NEAR(std::abs(term.coefficient.numeric), std::sqrt(0.5), 1e-9);
			EXPECT_FALSE(term.coefficient.latex.empty());
		}
	}

	TEST(PointGroupAnalysisBridgeTests, ActiveOrbitalLabelsAreUsedAndEmptyEntriesFilled)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		request.activeOrbitalIrreps = {"A1", "E"};
		request.activeElectronCount = 2;
		request.activeOrbitalLabels = {"a_{1}'", "", "e_{y}"};
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_TRUE(result) << result.Error().code << ": " << result.Error().technicalDetails;
		ASSERT_EQ(result->activeOrbitalLabels.size(), 3u);
		EXPECT_EQ(result->activeOrbitalLabels[0], "a_{1}'");
		EXPECT_FALSE(result->activeOrbitalLabels[1].empty());
		EXPECT_EQ(result->activeOrbitalLabels[2], "e_{y}");
		ASSERT_EQ(result->activeShells.size(), 2u);
		EXPECT_EQ(result->activeShells[0].label, "a_{1}'");
		EXPECT_EQ(result->activeShells[1].label, "e");
	}

	TEST(PointGroupAnalysisBridgeTests, RejectsActiveOrbitalLabelsOfWrongLength)
	{
		PointGroupAnalysisRequest request = MakeNvCluster();
		request.pointGroupLabel = "C3v";
		request.activeOrbitalIrreps = {"A1", "E"};
		request.activeElectronCount = 2;
		request.activeOrbitalLabels = {"a_{1}", "e_{x}"};
		const Result<PointGroupAnalysisResult> result = GroupTheoryBridge{}.Analyze(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.groupy.analysis.invalid_active_space");
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
