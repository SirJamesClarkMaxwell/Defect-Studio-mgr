#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include "Core/Utils/Time.hpp"
#include "App/ConfigManager.hpp"

namespace
{
	using Path = DefectStudio::Path;
	using FileSystem = ::FileSystem;

	[[nodiscard]] Path CreateTempDirectory()
	{
		const auto stamp = DefectStudio::Time::Now().time_since_epoch().count();
		const Path baseDirectory = Path::FromResolved(FileSystem::TempDirectoryPath()) / ("DefectStudioConfigTests_" + std::to_string(stamp));
		FileSystem::CreateDirectories(baseDirectory.Native());
		return baseDirectory;
	}

	void WriteFile(const Path &path, const std::string &text)
	{
		std::ofstream stream(path.Native(), std::ios::binary | std::ios::trunc);
		ASSERT_TRUE(stream.good());
		stream << text;
	}
} // namespace

TEST(ConfigManagerTests, LoadsNestedJsonAndFlattensKeys)
{
	const Path tempDirectory = CreateTempDirectory();
	const Path configPath = tempDirectory / "default.json";
	WriteFile(configPath,
	          R"JSON({
  "schema_version": "1",
  "window": {
    "width": 1280,
    "height": 720
  },
  "ui": {
    "show_demo_window": true
  }
})JSON");

	const auto result = DefectStudio::ConfigManager::LoadFile(configPath);
	EXPECT_TRUE(result.success);
	EXPECT_EQ(result.format, DefectStudio::ConfigFormat::Json);
	EXPECT_EQ(result.document.Get("schema_version"), "1");
	EXPECT_EQ(result.document.Get("window.width"), "1280");
	EXPECT_EQ(result.document.Get("window.height"), "720");
	EXPECT_TRUE(DefectStudio::ConfigManager::GetBool(result.document, "ui.show_demo_window"));

	std::error_code ignored;
	FileSystem::RemoveAll(tempDirectory.Native(), ignored);
}

TEST(ConfigManagerTests, LoadsNestedYamlAndFlattensKeys)
{
	const Path tempDirectory = CreateTempDirectory();
	const Path configPath = tempDirectory / "ui_settings.yaml";
	WriteFile(configPath,
	          "schema_version: 1\n"
	          "window:\n"
	          "  width: 1280\n"
	          "  height: 720\n"
	          "ui:\n"
	          "  show_demo_window: true\n"
	          "  clear_color: \"0.10, 0.10, 0.12, 1.00\"\n");

	const auto result = DefectStudio::ConfigManager::LoadFile(configPath);
	EXPECT_TRUE(result.success);
	EXPECT_EQ(result.format, DefectStudio::ConfigFormat::Yaml);
	EXPECT_EQ(result.document.Get("window.width"), "1280");
	EXPECT_EQ(result.document.Get("window.height"), "720");
	EXPECT_TRUE(DefectStudio::ConfigManager::GetBool(result.document, "ui.show_demo_window"));
	EXPECT_EQ(DefectStudio::ConfigManager::GetString(result.document, "ui.clear_color"),
	          "0.10, 0.10, 0.12, 1.00");

	std::error_code ignored;
	FileSystem::RemoveAll(tempDirectory.Native(), ignored);
}

TEST(ConfigManagerTests, LoadsYamlSequencesAsCommaSeparatedScalars)
{
	const Path tempDirectory = CreateTempDirectory();
	const Path configPath = tempDirectory / "default.yaml";
	WriteFile(configPath,
	          "schema_version: 1\n"
	          "viewport:\n"
	          "  background: [0.12, 0.12, 0.16]\n"
	          "recent_projects:\n"
	          "  paths: []\n");

	const auto result = DefectStudio::ConfigManager::LoadFile(configPath);
	ASSERT_TRUE(result.success) << result.error;
	EXPECT_EQ(result.format, DefectStudio::ConfigFormat::Yaml);
	EXPECT_EQ(result.document.Get("viewport.background"), "0.12, 0.12, 0.16");
	EXPECT_EQ(result.document.Get("recent_projects.paths"), "");

	std::error_code ignored;
	FileSystem::RemoveAll(tempDirectory.Native(), ignored);
}

TEST(ConfigManagerTests, SaveAndLoadRoundTripAcrossFormats)
{
	const Path tempDirectory = CreateTempDirectory();

	auto defaultDocument = DefectStudio::ConfigManager::CreateDefaultDocument();
	defaultDocument.Set("window.width", "1400");
	defaultDocument.Set("window.height", "900");

	std::string error;
	const Path jsonPath = tempDirectory / "default.json";
	ASSERT_TRUE(DefectStudio::ConfigManager::SaveFile(jsonPath, defaultDocument, error)) << error;
	const auto jsonResult = DefectStudio::ConfigManager::LoadFile(jsonPath);
	EXPECT_TRUE(jsonResult.success);
	EXPECT_EQ(jsonResult.document.Get("window.width"), "1400");
	EXPECT_EQ(jsonResult.document.Get("window.height"), "900");

	auto uiDocument = DefectStudio::ConfigManager::CreateUiSettingsDocument();
	uiDocument.Set("show_demo_window", "false");
	const Path yamlPath = tempDirectory / "ui_settings.yaml";
	ASSERT_TRUE(DefectStudio::ConfigManager::SaveFile(yamlPath, uiDocument, error)) << error;
	const auto yamlResult = DefectStudio::ConfigManager::LoadFile(yamlPath);
	EXPECT_TRUE(yamlResult.success);
	EXPECT_FALSE(DefectStudio::ConfigManager::GetBool(yamlResult.document, "show_demo_window"));

	std::error_code ignored;
	FileSystem::RemoveAll(tempDirectory.Native(), ignored);
}

TEST(ConfigManagerTests, SaveYamlWithDottedKeysWritesNestedStructure)
{
	const Path tempDirectory = CreateTempDirectory();
	const Path yamlPath = tempDirectory / "ui_settings.yaml";

	DefectStudio::ConfigDocument document;
	document.Set("schema_version", "1");
	document.Set("viewport.fov", "45.0");
	document.Set("ui.show_demo_window", "true");

	std::string error;
	ASSERT_TRUE(DefectStudio::ConfigManager::SaveFile(yamlPath, document, error)) << error;

	std::ifstream stream(yamlPath.Native(), std::ios::binary);
	ASSERT_TRUE(stream.good());
	std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	EXPECT_NE(text.find("viewport:"), std::string::npos);
	EXPECT_NE(text.find("fov:"), std::string::npos);
	EXPECT_EQ(text.find("viewport.fov:"), std::string::npos);

	const auto result = DefectStudio::ConfigManager::LoadFile(yamlPath);
	ASSERT_TRUE(result.success) << result.error;
	EXPECT_EQ(result.document.Get("viewport.fov"), "45.0");
	EXPECT_EQ(result.document.Get("ui.show_demo_window"), "true");

	std::error_code ignored;
	FileSystem::RemoveAll(tempDirectory.Native(), ignored);
}

TEST(ConfigManagerTests, MissingFileReturnsStructuredFailure)
{
	const auto result = DefectStudio::ConfigManager::LoadFile(Path("missing-config.yaml"));
	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.error.empty());
}

TEST(ConfigManagerTests, InstanceManagerBootstrapsAndPersistsUiSettings)
{
	const Path tempDirectory = CreateTempDirectory();

	DefectStudio::ConfigManager manager(tempDirectory);
	std::string error;
	ASSERT_TRUE(manager.Initialize(error)) << error;

	EXPECT_EQ(manager.GetDefaultString("window.title", ""), "DefectStudio");
	EXPECT_DOUBLE_EQ(manager.GetUiDouble("font_scale", 0.0), 1.0);

	manager.SetUiValue("font_scale", "1.25");
	ASSERT_TRUE(manager.SaveUiSettings(error)) << error;

	DefectStudio::ConfigManager reloaded(tempDirectory);
	ASSERT_TRUE(reloaded.Initialize(error)) << error;
	EXPECT_DOUBLE_EQ(reloaded.GetUiDouble("font_scale", 0.0), 1.25);

	std::error_code ignored;
	FileSystem::RemoveAll(tempDirectory.Native(), ignored);
}