#include <gtest/gtest.h>

#include "Core/Utils/Path.hpp"
#include "IO/AtomStyleIO.hpp"

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
	TEST(AtomStyleIoEventsTests, LoadsAtomStylesFromYamlFile)
	{
		const Path sourcePath = FindRepoRoot() / "install" / "app" / "assets" / "renderer" / "atom_styles.yaml";

		std::unordered_map<std::string, AtomRenderStyle> styles;
		VacancyRenderStyle vacancyStyle;
		std::string error;

		const bool loaded = AtomStyleIO::LoadFromFile(sourcePath, styles, vacancyStyle, error);

		AtomRenderStyle nitrogen;
		const auto found = styles.find("N");
		if (found != styles.end())
			nitrogen = found->second;

		EXPECT_TRUE(loaded);
		EXPECT_TRUE(error.empty());
		EXPECT_GT(styles.size(), 80u);
		EXPECT_NEAR(nitrogen.color.x, 0.19f, 1e-3f);
		EXPECT_NEAR(nitrogen.displayRadius, 0.33f, 1e-3f);
		EXPECT_NEAR(vacancyStyle.opacity, 0.35f, 1e-3f);
		EXPECT_EQ(vacancyStyle.renderMode, VacancyRenderMode::Ghost);
	}

	TEST(AtomStyleIoEventsTests, ReturnsErrorForMissingFile)
	{
		const Path missingPath = FindRepoRoot() / "install" / "app" / "assets" / "renderer" / "missing-atom-styles.yaml";

		std::unordered_map<std::string, AtomRenderStyle> styles;
		VacancyRenderStyle vacancyStyle;
		std::string error;
		const bool loaded = AtomStyleIO::LoadFromFile(missingPath, styles, vacancyStyle, error);
		EXPECT_FALSE(loaded);
		EXPECT_FALSE(error.empty());
	}
} // namespace DefectStudio::Tests
