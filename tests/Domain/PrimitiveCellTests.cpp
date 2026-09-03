#include <gtest/gtest.h>

#include "Domain/Crystal/BravaisLattice.hpp"
#include "Domain/Crystal/PrimitiveCell.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] float CellVolume(const glm::mat3 &lattice)
		{
			return std::abs(glm::determinant(lattice));
		}
	} // namespace

	TEST(PrimitiveCellTests, PrimitiveCenteringHasNoDistinctPrimitiveCell)
	{
		const glm::mat3 conventional = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 4.0f}).ToMatrix();
		EXPECT_FALSE(PrimitiveCellVectors(conventional, BravaisCenteringPreset::Primitive).has_value());
	}

	// The volume ratio is the whole point: an F cell holds 4 lattice points, so its primitive cell is
	// a quarter of it. Getting the transformation matrix wrong shows up here immediately.
	TEST(PrimitiveCellTests, FaceCenteredPrimitiveCellIsAQuarterOfTheConventionalOne)
	{
		const glm::mat3 conventional = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 4.0f}).ToMatrix();
		const std::optional<glm::mat3> primitive =
			PrimitiveCellVectors(conventional, BravaisCenteringPreset::FaceCentered);

		ASSERT_TRUE(primitive.has_value());
		EXPECT_NEAR(CellVolume(*primitive), CellVolume(conventional) / 4.0f, 1e-3f);
		// Every FCC primitive vector is a face diagonal: a/sqrt(2).
		for (int row = 0; row < 3; ++row)
			EXPECT_NEAR(glm::length((*primitive)[row]), 4.0f / std::sqrt(2.0f), 1e-3f);
	}

	TEST(PrimitiveCellTests, BodyCenteredPrimitiveCellIsHalfOfTheConventionalOne)
	{
		const glm::mat3 conventional = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 4.0f}).ToMatrix();
		const std::optional<glm::mat3> primitive =
			PrimitiveCellVectors(conventional, BravaisCenteringPreset::BodyCentered);

		ASSERT_TRUE(primitive.has_value());
		EXPECT_NEAR(CellVolume(*primitive), CellVolume(conventional) / 2.0f, 1e-3f);
		// Each BCC primitive vector is half a body diagonal: a*sqrt(3)/2.
		for (int row = 0; row < 3; ++row)
			EXPECT_NEAR(glm::length((*primitive)[row]), 4.0f * std::sqrt(3.0f) / 2.0f, 1e-3f);
	}

	TEST(PrimitiveCellTests, BaseCenteredPrimitiveCellIsHalfOfTheConventionalOne)
	{
		const glm::mat3 conventional =
			BuildLatticeCell(CrystalSystem::Orthorhombic, LatticeParameters{.a = 3.0f, .b = 4.0f, .c = 5.0f}).ToMatrix();
		const std::optional<glm::mat3> primitive =
			PrimitiveCellVectors(conventional, BravaisCenteringPreset::BaseCentered);

		ASSERT_TRUE(primitive.has_value());
		EXPECT_NEAR(CellVolume(*primitive), CellVolume(conventional) / 2.0f, 1e-3f);
	}

	TEST(PrimitiveCellTests, CenteringNamesFromPrototypesYamlParse)
	{
		EXPECT_EQ(ParseCenteringName("Face-centered"), BravaisCenteringPreset::FaceCentered);
		EXPECT_EQ(ParseCenteringName("Primitive"), BravaisCenteringPreset::Primitive);
		// An unrecognised spelling must not fall back to Primitive - that would silently hide the
		// overlay instead of surfacing the typo.
		EXPECT_FALSE(ParseCenteringName("face centred").has_value());
	}
} // namespace DefectStudio::Tests
