#include <gtest/gtest.h>

#include <string>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "IO/AtomStyleIO.hpp"

namespace
{
	[[nodiscard]] DefectStudio::Path CreateTempDirectory()
	{
		const auto stamp = DefectStudio::Time::Now().time_since_epoch().count();
		const DefectStudio::Path directory =
			DefectStudio::Path::FromResolved(FileSystem::TempDirectoryPath()) /
			("DefectStudioAtomStyleIoTests_" + std::to_string(stamp));
		FileSystem::CreateDirectories(directory.Native());
		return directory;
	}

	void RemoveTempDirectory(const DefectStudio::Path &path)
	{
		std::error_code ignored;
		FileSystem::RemoveAll(path.Native(), ignored);
	}

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

	TEST(AtomStyleIoEventsTests, SaveToFileRoundTripsThroughParseYaml)
	{
		const Path tempDirectory = CreateTempDirectory();
		const Path savePath = tempDirectory / "atom_styles.yaml";

		std::unordered_map<std::string, AtomRenderStyle> styles;
		styles.emplace("C", AtomRenderStyle{glm::vec3(0.2f, 0.2f, 0.2f), 0.4f});
		styles.emplace("O", AtomRenderStyle{glm::vec3(0.9f, 0.1f, 0.1f), 0.35f});
		VacancyRenderStyle vacancyStyle;
		vacancyStyle.color = glm::vec3(0.5f, 0.1f, 0.6f);
		vacancyStyle.displayRadius = 0.5f;
		vacancyStyle.opacity = 0.42f;
		vacancyStyle.renderMode = VacancyRenderMode::Wireframe;

		std::string saveError;
		ASSERT_TRUE(AtomStyleIO::SaveToFile(savePath, styles, vacancyStyle, saveError)) << saveError;

		std::unordered_map<std::string, AtomRenderStyle> loadedStyles;
		VacancyRenderStyle loadedVacancy;
		std::string loadError;
		ASSERT_TRUE(AtomStyleIO::LoadFromFile(savePath, loadedStyles, loadedVacancy, loadError)) << loadError;

		ASSERT_EQ(loadedStyles.size(), 2u);
		EXPECT_NEAR(loadedStyles.at("C").displayRadius, 0.4f, 1e-4f);
		EXPECT_NEAR(loadedStyles.at("O").color.x, 0.9f, 1e-4f);
		EXPECT_NEAR(loadedVacancy.opacity, 0.42f, 1e-4f);
		EXPECT_EQ(loadedVacancy.renderMode, VacancyRenderMode::Wireframe);

		RemoveTempDirectory(tempDirectory);
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
