#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "App/ConfigManager.hpp"
#include "Core/Utils/Time.hpp"

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
		FileSystem::CreateDirectories(path.parent_path().Native());
		std::ofstream stream(path.Native(), std::ios::binary | std::ios::trunc);
		ASSERT_TRUE(stream.good());
		stream << text;
	}

	[[nodiscard]] std::string ReadFile(const Path &path)
	{
		std::ifstream stream(path.Native(), std::ios::binary);
		EXPECT_TRUE(stream.good());
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}

	void RemoveTempDirectory(const Path &path)
	{
		std::error_code ignored;
		FileSystem::RemoveAll(path.Native(), ignored);
	}
} // namespace

TEST(ConfigManagerTests, InitializeCreatesPortableYamlFiles)
{
	const Path tempDirectory = CreateTempDirectory();

	DefectStudio::ConfigManager manager(tempDirectory);
	std::string error;
	ASSERT_TRUE(manager.Initialize(error)) << error;

	EXPECT_TRUE(FileSystem::Exists(DefectStudio::ConfigManager::GetDefaultConfigPath(tempDirectory).Native()));
	EXPECT_TRUE(FileSystem::Exists(DefectStudio::ConfigManager::GetUserSettingsPath(tempDirectory).Native()));
	EXPECT_EQ(manager.GetConfigDirectory().String(), tempDirectory.String());
	EXPECT_EQ(manager.GetConfig().layout.imGuiIniPath, DefectStudio::ConfigManager::GetLayoutPath(tempDirectory).String());

	RemoveTempDirectory(tempDirectory);
}

TEST(ConfigManagerTests, LoadsNestedYamlIntoTypedConfig)
{
	const Path tempDirectory = CreateTempDirectory();
	WriteFile(DefectStudio::ConfigManager::GetDefaultConfigPath(tempDirectory),
	          "schema_version: 1\n"
	          "log:\n"
	          "  level: debug\n"
	          "  to_file: false\n"
	          "  file_path: logs/custom.log\n"
	          "trace_events: true\n"
	          "window:\n"
	          "  width: 1600\n"
	          "  height: 900\n"
	          "  title: Nested DefectStudio\n"
	          "ui:\n"
	          "  font_scale:\n"
	          "    min: 0.5\n"
	          "    max: 3.0\n"
	          "  font_scale_step:\n"
	          "    min: 0.02\n"
	          "    max: 0.8\n"
	          "    slider_max: 0.4\n"
	          "appearance:\n"
	          "  clear_color: [0.20, 0.30, 0.40, 1.00]\n"
	          "jobs:\n"
	          "  default_worker_threads: 7\n"
	          "  reserve_urgent_worker: false\n"
	          "event_queue:\n"
	          "  initial_capacity: 512\n"
	          "  growth_step: 128\n");
	WriteFile(DefectStudio::ConfigManager::GetUserSettingsPath(tempDirectory),
	          "schema_version: 1\n"
	          "ui:\n"
	          "  font:\n"
	          "    path: assets/fonts/Test.ttf\n"
	          "  font_scale: 2.5\n"
	          "  font_scale_step: 0.25\n"
	          "appearance:\n"
	          "  clear_color: [0.10, 0.20, 0.30, 1.00]\n");

	DefectStudio::ConfigManager manager(tempDirectory);
	std::string error;
	ASSERT_TRUE(manager.Initialize(error)) << error;

	const DefectStudio::ApplicationConfig &config = manager.GetConfig();
	EXPECT_EQ(config.log.level, DefectStudio::LogLevel::Debug);
	EXPECT_FALSE(config.log.toFile);
	EXPECT_EQ(config.log.filePath.String(), "logs/custom.log");
	EXPECT_TRUE(config.log.traceEvents);
	EXPECT_EQ(config.window.width, 1600);
	EXPECT_EQ(config.window.height, 900);
	EXPECT_EQ(config.window.title, "Nested DefectStudio");
	EXPECT_FLOAT_EQ(config.ui.fontScaleMin, 0.5f);
	EXPECT_FLOAT_EQ(config.ui.fontScaleMax, 3.0f);
	EXPECT_FLOAT_EQ(config.ui.fontScaleStepMin, 0.02f);
	EXPECT_FLOAT_EQ(config.ui.fontScaleStepMax, 0.8f);
	EXPECT_FLOAT_EQ(config.ui.fontScaleStepSliderMax, 0.4f);
	EXPECT_EQ(config.jobs.defaultWorkerThreadCount, 7);
	EXPECT_FALSE(config.jobs.reserveUrgentWorker);
	EXPECT_EQ(config.eventQueue.initialCapacity, 512u);
	EXPECT_EQ(config.eventQueue.growthStep, 128u);
	EXPECT_EQ(config.ui.fontPath, "assets/fonts/Test.ttf");
	EXPECT_FLOAT_EQ(config.ui.fontScale, 2.5f);
	EXPECT_FLOAT_EQ(config.ui.fontScaleStep, 0.25f);
	EXPECT_FLOAT_EQ(config.ui.clearColor[0], 0.10f);
	EXPECT_FLOAT_EQ(config.ui.clearColor[1], 0.20f);
	EXPECT_FLOAT_EQ(config.ui.clearColor[2], 0.30f);
	EXPECT_FLOAT_EQ(config.ui.clearColor[3], 1.00f);

	DefectStudio::ApplicationSpecification specification;
	manager.ApplySpecification(specification);
	EXPECT_EQ(specification.logLevel, DefectStudio::LogLevel::Debug);
	EXPECT_FALSE(specification.logToFile);
	EXPECT_TRUE(specification.traceEvents);

	RemoveTempDirectory(tempDirectory);
}

TEST(ConfigManagerTests, LoadsLegacyFlatYamlDuringMigration)
{
	const Path tempDirectory = CreateTempDirectory();
	WriteFile(DefectStudio::ConfigManager::GetDefaultConfigPath(tempDirectory),
	          "schema_version: 1\n"
	          "log.level: warn\n"
	          "log.to_file: true\n"
	          "log.file_path: logs/legacy.log\n"
	          "trace_events: false\n"
	          "window.width: 1440\n"
	          "window.height: 810\n"
	          "window.title: Legacy DefectStudio\n"
	          "ui.font_scale.min: 0.70\n"
	          "ui.font_scale.max: 2.00\n"
	          "ui.font_scale_step.min: 0.05\n"
	          "ui.font_scale_step.max: 0.50\n"
	          "ui.font_scale_step.slider_max: 0.25\n"
	          "jobs.default_worker_threads: 3\n"
	          "jobs.reserve_urgent_worker: false\n"
	          "event_queue.initial_capacity: 64\n"
	          "event_queue.growth_step: 96\n");
	WriteFile(DefectStudio::ConfigManager::GetUserSettingsPath(tempDirectory),
	          "schema_version: 1\n"
	          "font_path: assets/fonts/Legacy.ttf\n"
	          "font_scale: 1.75\n"
	          "font_scale_step: 0.15\n"
	          "clear_color: 0.40, 0.30, 0.20, 1.00\n");

	DefectStudio::ConfigManager manager(tempDirectory);
	std::string error;
	ASSERT_TRUE(manager.Initialize(error)) << error;

	const DefectStudio::ApplicationConfig &config = manager.GetConfig();
	EXPECT_EQ(config.log.level, DefectStudio::LogLevel::Warn);
	EXPECT_EQ(config.window.width, 1440);
	EXPECT_EQ(config.window.height, 810);
	EXPECT_EQ(config.window.title, "Legacy DefectStudio");
	EXPECT_EQ(config.jobs.defaultWorkerThreadCount, 3);
	EXPECT_FALSE(config.jobs.reserveUrgentWorker);
	EXPECT_EQ(config.eventQueue.initialCapacity, 64u);
	EXPECT_EQ(config.eventQueue.growthStep, 96u);
	EXPECT_EQ(config.ui.fontPath, "assets/fonts/Legacy.ttf");
	EXPECT_FLOAT_EQ(config.ui.fontScale, 1.75f);
	EXPECT_FLOAT_EQ(config.ui.fontScaleStep, 0.15f);
	EXPECT_FLOAT_EQ(config.ui.clearColor[0], 0.40f);
	EXPECT_FLOAT_EQ(config.ui.clearColor[1], 0.30f);
	EXPECT_FLOAT_EQ(config.ui.clearColor[2], 0.20f);

	RemoveTempDirectory(tempDirectory);
}

TEST(ConfigManagerTests, SaveUserSettingsWritesNestedYamlAndRoundTrips)
{
	const Path tempDirectory = CreateTempDirectory();

	DefectStudio::ConfigManager manager(tempDirectory);
	std::string error;
	ASSERT_TRUE(manager.Initialize(error)) << error;

	DefectStudio::ApplicationConfig config = manager.GetConfig();
	config.ui.fontPath = "assets/fonts/CascadiaCode.ttf";
	config.ui.fontScale = 1.25f;
	config.ui.fontScaleStep = 0.20f;
	config.ui.clearColor = {0.05f, 0.15f, 0.25f, 1.00f};
	manager.SetConfig(config);
	ASSERT_TRUE(manager.SaveUserSettings(error)) << error;

	const std::string text = ReadFile(DefectStudio::ConfigManager::GetUserSettingsPath(tempDirectory));
	EXPECT_NE(text.find("ui:"), std::string::npos);
	EXPECT_NE(text.find("appearance:"), std::string::npos);
	EXPECT_NE(text.find("clear_color:"), std::string::npos);
	EXPECT_EQ(text.find("font_path:"), std::string::npos);

	DefectStudio::ConfigManager reloaded(tempDirectory);
	ASSERT_TRUE(reloaded.Initialize(error)) << error;
	const DefectStudio::ApplicationConfig &reloadedConfig = reloaded.GetConfig();
	EXPECT_EQ(reloadedConfig.ui.fontPath, "assets/fonts/CascadiaCode.ttf");
	EXPECT_FLOAT_EQ(reloadedConfig.ui.fontScale, 1.25f);
	EXPECT_FLOAT_EQ(reloadedConfig.ui.fontScaleStep, 0.20f);
	EXPECT_FLOAT_EQ(reloadedConfig.ui.clearColor[0], 0.05f);
	EXPECT_FLOAT_EQ(reloadedConfig.ui.clearColor[1], 0.15f);
	EXPECT_FLOAT_EQ(reloadedConfig.ui.clearColor[2], 0.25f);
	EXPECT_FLOAT_EQ(reloadedConfig.ui.clearColor[3], 1.00f);

	RemoveTempDirectory(tempDirectory);
}

TEST(ConfigManagerTests, InvalidYamlFailsInitialization)
{
	const Path tempDirectory = CreateTempDirectory();
	WriteFile(DefectStudio::ConfigManager::GetDefaultConfigPath(tempDirectory), "window: [\n");

	DefectStudio::ConfigManager manager(tempDirectory);
	std::string error;
	EXPECT_FALSE(manager.Initialize(error));
	EXPECT_FALSE(error.empty());

	RemoveTempDirectory(tempDirectory);
}
