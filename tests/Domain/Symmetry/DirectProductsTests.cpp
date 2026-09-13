#include <gtest/gtest.h>

#include "Domain/Symmetry/DirectProducts.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] std::vector<std::pair<std::string, int>> Flatten(const std::vector<IrrepMultiplicity> &sum)
		{
			std::vector<std::pair<std::string, int>> result;
			for (const IrrepMultiplicity &entry : sum)
				result.emplace_back(entry.irrepLabel, entry.multiplicity);
			return result;
		}
	} // namespace

	TEST(DirectProductsTests, C3vTable)
	{
		CharacterTable table;
		table.pointGroupLabel = "C3v";
		table.groupOrder = 6;
		table.classLabels = {"E", "2C3", "3sv"};
		table.classSizes = {1, 2, 3};
		table.irrepLabels = {"A1", "A2", "E"};
		table.irrepDimensions = {1, 1, 2};
		table.characters = {
			{{"1", 1.0}, {"1", 1.0}, {"1", 1.0}},
			{{"1", 1.0}, {"1", 1.0}, {"-1", -1.0}},
			{{"2", 2.0}, {"-1", -1.0}, {"0", 0.0}}};

		const auto products = ComputeDirectProducts(table);
		ASSERT_EQ(products.size(), 3u);
		using Sum = std::vector<std::pair<std::string, int>>;
		EXPECT_EQ(Flatten(products[0][0]), (Sum{{"A1", 1}}));
		EXPECT_EQ(Flatten(products[0][2]), (Sum{{"E", 1}}));
		EXPECT_EQ(Flatten(products[1][1]), (Sum{{"A1", 1}}));
		EXPECT_EQ(Flatten(products[1][2]), (Sum{{"E", 1}}));
		EXPECT_EQ(Flatten(products[2][1]), (Sum{{"E", 1}}));
		EXPECT_EQ(Flatten(products[2][2]), (Sum{{"A1", 1}, {"A2", 1}, {"E", 1}}));
		EXPECT_EQ(products[2][2][2].dimension, 2);
	}

	// C3 has complex-conjugate 1-D irreps: E ⊗ E* = A must come out of the conj(χ_k) term.
	TEST(DirectProductsTests, ComplexCharactersC3)
	{
		const double c = -0.5, s = 0.8660254037844386;
		CharacterTable table;
		table.pointGroupLabel = "C3";
		table.groupOrder = 3;
		table.classLabels = {"E", "C3", "C3^2"};
		table.classSizes = {1, 1, 1};
		table.irrepLabels = {"A", "E1", "E2"};
		table.irrepDimensions = {1, 1, 1};
		table.characters = {
			{{"1", 1.0}, {"1", 1.0}, {"1", 1.0}},
			{{"1", 1.0}, {"w", c, s}, {"w*", c, -s}},
			{{"1", 1.0}, {"w*", c, -s}, {"w", c, s}}};

		const auto products = ComputeDirectProducts(table);
		using Sum = std::vector<std::pair<std::string, int>>;
		EXPECT_EQ(Flatten(products[1][2]), (Sum{{"A", 1}}));
		EXPECT_EQ(Flatten(products[1][1]), (Sum{{"E2", 1}}));
	}

	TEST(DirectProductsTests, EmptyTable)
	{
		EXPECT_TRUE(ComputeDirectProducts(CharacterTable{}).empty());
	}
} // namespace DefectStudio::Tests
