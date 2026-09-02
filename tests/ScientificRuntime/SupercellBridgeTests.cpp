#include <gtest/gtest.h>

#include "Domain/Crystal/BravaisLattice.hpp"
#include "ScientificRuntime/Python/SupercellBridge.hpp"

namespace DefectStudio::Tests
{
	TEST(SupercellBridgeTests, SuggestsIntegerMatrixForSimpleCubicSurface)
	{
		CrystalStructure unitCell;
		unitCell.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 3.0f});
		unitCell.atoms = { AtomSite{"Cu", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };

		SupercellBridge bridge;
		const Result<SupercellMatrix> result =
			bridge.SuggestSurfaceOrientedMatrix(unitCell, MillerIndices{0, 0, 1}, 2);

		if (!result)
		{
			GTEST_SKIP() << "ASE unavailable in current environment: " << result.Error().technicalDetails;
		}

		EXPECT_GT(result->Determinant(), 0);
	}

	TEST(SupercellBridgeTests, ReportsSpacegroup225ForSimpleCubicCopper)
	{
		CrystalStructure structure;
		structure.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 3.615f});
		// Cu is FCC (Fm-3m, #225) - use the FaceCentered preset basis from Task 1 directly.
		for (const glm::vec3 &fractional : GetCenteringPresetBasis(BravaisCenteringPreset::FaceCentered))
			structure.atoms.push_back(AtomSite{"Cu", structure.FractionalToCartesian(fractional), fractional, 0});

		SupercellBridge bridge;
		const Result<SymmetryInfo> result = bridge.GetSymmetryInfo(structure, 0.01f);
		if (!result)
			GTEST_SKIP() << "spglib unavailable in current environment: " << result.Error().technicalDetails;

		EXPECT_EQ(result->spacegroupNumber, 225);
		EXPECT_EQ(result->wyckoffLetters.size(), structure.atoms.size());
	}
} // namespace DefectStudio::Tests
