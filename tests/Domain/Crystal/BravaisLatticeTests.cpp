#include <gtest/gtest.h>

#include "Domain/Crystal/BravaisLattice.hpp"

namespace DefectStudio::Tests
{
	TEST(BravaisLatticeTests, CubicLocksBAndCToAAndAllAnglesTo90)
	{
		LatticeParameters params;
		params.a = 3.5f;
		params.b = 999.0f; // must be ignored - Cubic derives b,c from a
		params.c = 111.0f;
		params.alphaDegrees = 45.0f; // must be ignored - Cubic locks to 90

		const LatticeCell cell = BuildLatticeCell(CrystalSystem::Cubic, params);

		EXPECT_NEAR(glm::length(cell.vectors[0]), 3.5f, 1e-5f);
		EXPECT_NEAR(glm::length(cell.vectors[1]), 3.5f, 1e-5f);
		EXPECT_NEAR(glm::length(cell.vectors[2]), 3.5f, 1e-5f);
		EXPECT_NEAR(glm::dot(glm::normalize(cell.vectors[0]), glm::normalize(cell.vectors[1])), 0.0f, 1e-5f);
		EXPECT_NEAR(glm::dot(glm::normalize(cell.vectors[0]), glm::normalize(cell.vectors[2])), 0.0f, 1e-5f);
	}

	TEST(BravaisLatticeTests, HexagonalGamma120DegreesBetweenAAndB)
	{
		LatticeParameters params;
		params.a = 2.46f; // graphene-like
		params.c = 6.7f;

		const LatticeCell cell = BuildLatticeCell(CrystalSystem::Hexagonal, params);

		const float cosGamma = glm::dot(glm::normalize(cell.vectors[0]), glm::normalize(cell.vectors[1]));
		EXPECT_NEAR(cosGamma, -0.5f, 1e-5f); // cos(120 deg)
		EXPECT_NEAR(glm::length(cell.vectors[2]), 6.7f, 1e-5f);
	}

	TEST(BravaisLatticeTests, TriclinicKeepsAllSixFreeParameters)
	{
		LatticeParameters params;
		params.a = 5.0f; params.b = 6.0f; params.c = 7.0f;
		params.alphaDegrees = 80.0f; params.betaDegrees = 95.0f; params.gammaDegrees = 100.0f;

		const LatticeCell cell = BuildLatticeCell(CrystalSystem::Triclinic, params);

		EXPECT_NEAR(glm::length(cell.vectors[0]), 5.0f, 1e-4f);
		EXPECT_NEAR(glm::length(cell.vectors[1]), 6.0f, 1e-4f);
		EXPECT_NEAR(glm::length(cell.vectors[2]), 7.0f, 1e-4f);
		const float cosAlpha = glm::dot(glm::normalize(cell.vectors[1]), glm::normalize(cell.vectors[2]));
		EXPECT_NEAR(cosAlpha, glm::cos(glm::radians(80.0f)), 1e-4f);
	}

	TEST(BravaisLatticeTests, CubicFieldConstraintsLockBCAndAllAngles)
	{
		const LatticeFieldConstraints constraints = GetFieldConstraints(CrystalSystem::Cubic);
		EXPECT_TRUE(constraints.bLocked);
		EXPECT_TRUE(constraints.cLocked);
		EXPECT_TRUE(constraints.alphaLocked);
		EXPECT_TRUE(constraints.betaLocked);
		EXPECT_TRUE(constraints.gammaLocked);
		EXPECT_FLOAT_EQ(constraints.lockedAngleDegrees, 90.0f);
	}

	TEST(BravaisLatticeTests, TriclinicFieldConstraintsLockNothing)
	{
		const LatticeFieldConstraints constraints = GetFieldConstraints(CrystalSystem::Triclinic);
		EXPECT_FALSE(constraints.bLocked);
		EXPECT_FALSE(constraints.cLocked);
		EXPECT_FALSE(constraints.alphaLocked);
		EXPECT_FALSE(constraints.betaLocked);
		EXPECT_FALSE(constraints.gammaLocked);
	}

	TEST(BravaisLatticeTests, FaceCenteredPresetReturnsFourFractionalPositions)
	{
		const std::vector<glm::vec3> basis = GetCenteringPresetBasis(BravaisCenteringPreset::FaceCentered);
		ASSERT_EQ(basis.size(), 4u);
		EXPECT_EQ(basis[0], glm::vec3(0.0f));
	}

	TEST(BravaisLatticeTests, IsPresetSupportedForCubicSupportsAllFourHexagonalOnlyPrimitive)
	{
		EXPECT_TRUE(IsPresetSupportedFor(CrystalSystem::Cubic, BravaisCenteringPreset::Primitive));
		EXPECT_TRUE(IsPresetSupportedFor(CrystalSystem::Cubic, BravaisCenteringPreset::BodyCentered));
		EXPECT_TRUE(IsPresetSupportedFor(CrystalSystem::Cubic, BravaisCenteringPreset::FaceCentered));
		EXPECT_TRUE(IsPresetSupportedFor(CrystalSystem::Cubic, BravaisCenteringPreset::BaseCentered));

		EXPECT_TRUE(IsPresetSupportedFor(CrystalSystem::Hexagonal, BravaisCenteringPreset::Primitive));
		EXPECT_FALSE(IsPresetSupportedFor(CrystalSystem::Hexagonal, BravaisCenteringPreset::BodyCentered));
		EXPECT_FALSE(IsPresetSupportedFor(CrystalSystem::Hexagonal, BravaisCenteringPreset::FaceCentered));
		EXPECT_FALSE(IsPresetSupportedFor(CrystalSystem::Hexagonal, BravaisCenteringPreset::BaseCentered));

		EXPECT_TRUE(IsPresetSupportedFor(CrystalSystem::Trigonal, BravaisCenteringPreset::Primitive));
		EXPECT_FALSE(IsPresetSupportedFor(CrystalSystem::Trigonal, BravaisCenteringPreset::BodyCentered));
	}
} // namespace DefectStudio::Tests
