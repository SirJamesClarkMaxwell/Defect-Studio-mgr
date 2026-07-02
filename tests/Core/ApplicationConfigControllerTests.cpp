#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "App/Controllers/ApplicationConfigController.hpp"
#include "App/Managers/ConfigManager.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/EventQueue.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Time.hpp"
#include "IO/IOLayer.hpp"
#include "Events/EditorUiEvents.hpp"
#include "Events/RendererEvents.hpp"

namespace
{
	using Path = DefectStudio::Path;
	using FileSystem = ::FileSystem;

	[[nodiscard]] Path CreateTempDirectory()
	{
		const auto stamp = DefectStudio::Time::Now().time_since_epoch().count();
		const Path baseDirectory = Path::FromResolved(FileSystem::TempDirectoryPath())
			/ ("DefectStudioConfigControllerTests_" + std::to_string(stamp));
		FileSystem::CreateDirectories(baseDirectory.Native());
		return baseDirectory;
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

TEST(ApplicationConfigControllerTests, ApplyRequestUpdatesRuntimeConfigAndPublishesApplied)
{
	const Path tempDirectory = CreateTempDirectory();

	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	auto configManager = DefectStudio::CreateRef<DefectStudio::ConfigManager>(tempDirectory);
	std::string error;
	ASSERT_TRUE(configManager->Initialize(error)) << error;

	DefectStudio::ApplicationConfig runtimeConfig = configManager->GetConfig();
	DefectStudio::ApplicationSpecification specification;
	DefectStudio::EventQueue eventQueue;
	DefectStudio::ApplicationConfigController controller(
		eventBus,
		configManager,
		runtimeConfig,
		specification,
		eventQueue);

	bool applied = false;
	bool persisted = true;
	bool rendererApplied = false;
	DefectStudio::ApplicationConfig appliedConfig;
	const auto appliedSubscription = eventBus->Subscribe<DefectStudio::AppEvents::Config::Applied>(
		[&](const DefectStudio::AppEvents::Config::Applied &event) {
			applied = true;
			persisted = event.persisted;
			appliedConfig = event.config;
		});
	(void)appliedSubscription;
	const auto rendererAppliedSubscription = eventBus->Subscribe<DefectStudio::RendererEvents::Config::Applied>(
		[&](const DefectStudio::RendererEvents::Config::Applied &event) {
			rendererApplied = true;
			EXPECT_FALSE(event.wasPersisted);
		});
	(void)rendererAppliedSubscription;

	DefectStudio::ApplicationConfig requested = runtimeConfig;
	requested.log.level = DefectStudio::LogLevel::Debug;
	requested.log.traceEvents = true;
	requested.window.width = 180;
	requested.window.height = 100;
	requested.eventQueue.initialCapacity = 64;
	requested.eventQueue.growthStep = 32;
	requested.layout.imGuiIniPath.clear();

	DefectStudio::AppEvents::Config::ApplyRequested applyRequested{requested, false};
	eventBus->Publish(applyRequested);

	EXPECT_EQ(runtimeConfig.directory.String(), configManager->GetConfigDirectory().String());
	EXPECT_EQ(runtimeConfig.window.width, 320);
	EXPECT_EQ(runtimeConfig.window.height, 240);
	EXPECT_EQ(runtimeConfig.layout.imGuiIniPath, DefectStudio::ConfigManager::GetLayoutPath(tempDirectory).String());
	EXPECT_EQ(specification.logLevel, DefectStudio::LogLevel::Debug);
	EXPECT_TRUE(specification.traceEvents);
	EXPECT_EQ(eventQueue.Capacity(), 64u);
	EXPECT_EQ(eventBus->GetQueuedEventCount(), 4u);

	eventBus->ProcessQueue();

	EXPECT_TRUE(applied);
	EXPECT_TRUE(rendererApplied);
	EXPECT_FALSE(persisted);
	EXPECT_EQ(appliedConfig.window.width, 320);
	EXPECT_EQ(appliedConfig.window.height, 240);

	RemoveTempDirectory(tempDirectory);
}

TEST(ApplicationConfigControllerTests, SaveRequestsPersistAndPublishStatusEvents)
{
	const Path tempDirectory = CreateTempDirectory();

	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	auto configManager = DefectStudio::CreateRef<DefectStudio::ConfigManager>(tempDirectory);
	std::string error;
	ASSERT_TRUE(configManager->Initialize(error)) << error;

	DefectStudio::ApplicationConfig runtimeConfig = configManager->GetConfig();
	DefectStudio::ApplicationSpecification specification;
	DefectStudio::EventQueue eventQueue;
	DefectStudio::IOLayer ioLayer;
	ioLayer.BindRuntimeServices(eventBus);
	DefectStudio::ApplicationConfigController controller(
		eventBus,
		configManager,
		runtimeConfig,
		specification,
		eventQueue);

	bool userSaved = false;
	bool defaultsSaved = false;
	const auto userSavedSubscription = eventBus->Subscribe<DefectStudio::AppEvents::Config::UserSaved>(
		[&](const DefectStudio::AppEvents::Config::UserSaved &event) {
			userSaved = true;
			EXPECT_EQ(event.config.window.title, "User Controller Save");
		});
	const auto defaultsSavedSubscription = eventBus->Subscribe<DefectStudio::AppEvents::Config::DefaultsSaved>(
		[&](const DefectStudio::AppEvents::Config::DefaultsSaved &event) {
			defaultsSaved = true;
			EXPECT_EQ(event.config.window.title, "Default Controller Save");
		});
	(void)userSavedSubscription;
	(void)defaultsSavedSubscription;

	DefectStudio::ApplicationConfig userConfig = runtimeConfig;
	userConfig.window.title = "User Controller Save";
	userConfig.window.width = 1444;
	DefectStudio::AppEvents::Config::SaveUserRequested saveUserRequested{userConfig};
	eventBus->Publish(saveUserRequested);
	eventBus->ProcessQueue();
	eventBus->ProcessQueue();

	EXPECT_TRUE(userSaved);
	EXPECT_EQ(runtimeConfig.window.title, "User Controller Save");
	EXPECT_NE(ReadFile(DefectStudio::ConfigManager::GetUserSettingsPath(tempDirectory)).find("1444"),
	          std::string::npos);

	DefectStudio::ApplicationConfig defaultConfig = runtimeConfig;
	defaultConfig.window.title = "Default Controller Save";
	DefectStudio::AppEvents::Config::SaveDefaultsRequested saveDefaultsRequested{defaultConfig};
	eventBus->Publish(saveDefaultsRequested);
	eventBus->ProcessQueue();
	eventBus->ProcessQueue();

	EXPECT_TRUE(defaultsSaved);
	EXPECT_EQ(runtimeConfig.window.title, "Default Controller Save");
	EXPECT_NE(ReadFile(DefectStudio::ConfigManager::GetDefaultConfigPath(tempDirectory)).find("Default Controller Save"),
	          std::string::npos);

	RemoveTempDirectory(tempDirectory);
}

TEST(IOLayerPersistenceTests, PersistsLayoutAndThemeTextFromEvents)
{
	const Path tempDirectory = CreateTempDirectory();

	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	DefectStudio::IOLayer ioLayer;
	ioLayer.BindRuntimeServices(eventBus);

	const Path layoutPath = tempDirectory / "layouts" / "test.ini";
	const Path themePath = tempDirectory / "themes" / "orange.yaml";
	const std::string layoutText = "[Window][SettingsPanel]\nPos=1,2\n";
	const std::string themeText = "schema_version: '1'\nappearance: {}\n";

	bool layoutSaved = false;
	bool themeSaved = false;
	const auto layoutSavedSubscription = eventBus->Subscribe<DefectStudio::EditorUiEvents::LayoutSaved>(
		[&](const DefectStudio::EditorUiEvents::LayoutSaved &event) {
			layoutSaved = true;
			EXPECT_EQ(event.path.String(), layoutPath.String());
			EXPECT_EQ(event.bytes, layoutText.size());
		});
	const auto themeSavedSubscription = eventBus->Subscribe<DefectStudio::EditorUiEvents::ThemeSaved>(
		[&](const DefectStudio::EditorUiEvents::ThemeSaved &event) {
			themeSaved = true;
			EXPECT_EQ(event.path.String(), themePath.String());
		});
	(void)layoutSavedSubscription;
	(void)themeSavedSubscription;

	DefectStudio::EditorUiEvents::PersistRequested persistLayout{DefectStudio::EditorUiEvents::PersistKind::Layout, layoutPath, layoutText};
	eventBus->Publish(persistLayout);
	DefectStudio::EditorUiEvents::PersistRequested persistTheme{DefectStudio::EditorUiEvents::PersistKind::Theme, themePath, themeText};
	eventBus->Publish(persistTheme);
	eventBus->ProcessQueue();

	EXPECT_TRUE(layoutSaved);
	EXPECT_TRUE(themeSaved);
	EXPECT_EQ(ReadFile(layoutPath), layoutText);
	EXPECT_EQ(ReadFile(themePath), themeText);

	RemoveTempDirectory(tempDirectory);
}
