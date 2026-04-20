#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include "Core/Utils/Time.hpp"
#include "App/ConfigManager.hpp"

namespace
{
	[[nodiscard]] std::filesystem::path CreateTempDirectory()
	{
		const auto stamp = DefectStudio::Time::Now().time_since_epoch().count();
		const std::filesystem::path baseDirectory = std::filesystem::temp_directory_path() /
		                                            ("DefectStudioConfigTests_" + std::to_string(stamp));
		std::filesystem::create_directories(baseDirectory);
		return baseDirectory;
	}

	void WriteFile(const std::filesystem::path &path, const std::string &text)
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		ASSERT_TRUE(stream.good());
		stream << text;
	}
} // namespace

TEST(ConfigManagerTests, LoadsNestedJsonAndFlattensKeys)
{
	const std::filesystem::path tempDirectory = CreateTempDirectory();
	const std::filesystem::path configPath = tempDirectory / "default.json";
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
	std::filesystem::remove_all(tempDirectory, ignored);
}

TEST(ConfigManagerTests, LoadsNestedYamlAndFlattensKeys)
{
	const std::filesystem::path tempDirectory = CreateTempDirectory();
	const std::filesystem::path configPath = tempDirectory / "ui_settings.yaml";
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
	std::filesystem::remove_all(tempDirectory, ignored);
}

TEST(ConfigManagerTests, LoadsYamlSequencesAsCommaSeparatedScalars)
{
	const std::filesystem::path tempDirectory = CreateTempDirectory();
	const std::filesystem::path configPath = tempDirectory / "default.yaml";
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
	std::filesystem::remove_all(tempDirectory, ignored);
}

TEST(ConfigManagerTests, SaveAndLoadRoundTripAcrossFormats)
{
	const std::filesystem::path tempDirectory = CreateTempDirectory();

	auto defaultDocument = DefectStudio::ConfigManager::CreateDefaultDocument();
	defaultDocument.Set("window.width", "1400");
	defaultDocument.Set("window.height", "900");

	std::string error;
	const std::filesystem::path jsonPath = tempDirectory / "default.json";
	ASSERT_TRUE(DefectStudio::ConfigManager::SaveFile(jsonPath, defaultDocument, error)) << error;
	const auto jsonResult = DefectStudio::ConfigManager::LoadFile(jsonPath);
	EXPECT_TRUE(jsonResult.success);
	EXPECT_EQ(jsonResult.document.Get("window.width"), "1400");
	EXPECT_EQ(jsonResult.document.Get("window.height"), "900");

	auto uiDocument = DefectStudio::ConfigManager::CreateUiSettingsDocument();
	uiDocument.Set("show_demo_window", "false");
	const std::filesystem::path yamlPath = tempDirectory / "ui_settings.yaml";
	ASSERT_TRUE(DefectStudio::ConfigManager::SaveFile(yamlPath, uiDocument, error)) << error;
	const auto yamlResult = DefectStudio::ConfigManager::LoadFile(yamlPath);
	EXPECT_TRUE(yamlResult.success);
	EXPECT_FALSE(DefectStudio::ConfigManager::GetBool(yamlResult.document, "show_demo_window"));

	std::error_code ignored;
	std::filesystem::remove_all(tempDirectory, ignored);
}

TEST(ConfigManagerTests, SaveYamlWithDottedKeysWritesNestedStructure)
{
	const std::filesystem::path tempDirectory = CreateTempDirectory();
	const std::filesystem::path yamlPath = tempDirectory / "ui_settings.yaml";

	DefectStudio::ConfigDocument document;
	document.Set("schema_version", "1");
	document.Set("viewport.fov", "45.0");
	document.Set("ui.show_demo_window", "true");

	std::string error;
	ASSERT_TRUE(DefectStudio::ConfigManager::SaveFile(yamlPath, document, error)) << error;

	std::ifstream stream(yamlPath, std::ios::binary);
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
	std::filesystem::remove_all(tempDirectory, ignored);
}

TEST(ConfigManagerTests, MissingFileReturnsStructuredFailure)
{
	const auto result = DefectStudio::ConfigManager::LoadFile(std::filesystem::path("missing-config.yaml"));
	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.error.empty());
}