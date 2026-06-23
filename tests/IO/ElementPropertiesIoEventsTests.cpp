#include <gtest/gtest.h>

#include <string>

#include "Core/Utils/Path.hpp"
#include "IO/ElementPropertiesIO.hpp"

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
	TEST(ElementPropertiesIoEventsTests, LoadsElementPropertiesFromYamlFile)
	{
		const Path sourcePath = FindRepoRoot() / "install" / "app" / "data" / "elements" / "element_properties.yaml";

		std::unordered_map<std::string, ElementProperties> entries;
		std::string error;
		const bool loaded = ElementPropertiesIO::LoadFromFile(sourcePath, entries, error);
		const auto found = entries.find("Si");
		const ElementProperties silicon = found != entries.end() ? found->second : ElementProperties{};

		EXPECT_TRUE(loaded);
		EXPECT_TRUE(error.empty());
		EXPECT_GT(entries.size(), 20u);
		EXPECT_EQ(silicon.atomicNumber, 14);
		EXPECT_NEAR(silicon.covalentRadius, 1.11f, 1e-3f);
	}

	TEST(ElementPropertiesIoEventsTests, ReturnsErrorForMissingFile)
	{
		const Path missingPath = FindRepoRoot() / "install" / "app" / "data" / "elements" / "missing-element-properties.yaml";

		std::unordered_map<std::string, ElementProperties> entries;
		std::string error;
		const bool loaded = ElementPropertiesIO::LoadFromFile(missingPath, entries, error);
		EXPECT_FALSE(loaded);
		EXPECT_FALSE(error.empty());
	}
} // namespace DefectStudio::Tests
