#include <gtest/gtest.h>

#include <unordered_map>

#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio::Tests
{
	TEST(ElementPropertiesTableTests, ReplacedDataProvidesKnownElementValues)
	{
		ElementPropertiesTable table;
		std::unordered_map<std::string, ElementProperties> entries;
		entries.emplace("Si", ElementProperties{14, 28.086f, 1.11f, 2.10f});
		table.ReplaceData(std::move(entries));
		ASSERT_FALSE(table.Empty());
		ASSERT_EQ(table.Size(), 1u);

		const ElementProperties &silicon = table.Get("Si");
		EXPECT_EQ(silicon.atomicNumber, 14);
		EXPECT_NEAR(silicon.mass, 28.086f, 1e-3f);
		EXPECT_NEAR(silicon.covalentRadius, 1.11f, 1e-3f);
		EXPECT_NEAR(silicon.vanDerWaalsRadius, 2.10f, 1e-3f);
		EXPECT_NEAR(table.CovalentRadius("Si"), 1.11f, 1e-3f);
	}

	TEST(ElementPropertiesTableTests, UsesFallbackForUnknownElement)
	{
		ElementPropertiesTable table;
		ASSERT_TRUE(table.Empty());
		const ElementProperties &unknown = table.Get("Xx");
		EXPECT_EQ(unknown.atomicNumber, 0);
		EXPECT_FLOAT_EQ(unknown.mass, 0.0f);
		EXPECT_FLOAT_EQ(unknown.covalentRadius, 0.77f);
		EXPECT_FLOAT_EQ(unknown.vanDerWaalsRadius, 1.50f);
	}
} // namespace DefectStudio::Tests
