#include <gtest/gtest.h>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace
{
	[[nodiscard]] DefectStudio::Path FindRepoRoot()
	{
		DefectStudio::Path cursor = DefectStudio::Path::FromResolved(FileSystem::CurrentPath());
		for (int depth = 0; depth < 10; ++depth)
		{
			if (FileSystem::Exists((cursor / "pyproject.toml").Native()))
				return cursor;

			const DefectStudio::Path parent = cursor.parent_path();
			if (parent.Empty() || parent == cursor)
				break;
			cursor = parent;
		}

		return DefectStudio::Path::FromResolved(FileSystem::CurrentPath());
	}
} // namespace

namespace DefectStudio::Tests
{
	TEST(ElementPropertiesTableTests, LoadsYamlAndProvidesKnownElementValues)
	{
		const Path filePath = FindRepoRoot() / "install" / "app" / "data" / "elements" / "element_properties.yaml";

		ElementPropertiesTable table;
		ASSERT_TRUE(table.LoadFromFile(filePath.String()));

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
		const ElementProperties &unknown = table.Get("Xx");
		EXPECT_EQ(unknown.atomicNumber, 0);
		EXPECT_FLOAT_EQ(unknown.mass, 0.0f);
		EXPECT_FLOAT_EQ(unknown.covalentRadius, 0.77f);
		EXPECT_FLOAT_EQ(unknown.vanDerWaalsRadius, 1.50f);
	}
} // namespace DefectStudio::Tests
