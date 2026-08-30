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
} // namespace DefectStudio::Tests
