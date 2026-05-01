#include <gtest/gtest.h>

#include <chrono>
#include <fstream>

#include "Core/Assets/AssetManager.hpp"

namespace
{
	struct AssetManagerTestWorkspace
	{
		std::filesystem::path root;
		std::filesystem::path defaults;
		std::filesystem::path overrides;

		AssetManagerTestWorkspace()
		{
			root = std::filesystem::temp_directory_path()
				/ ("DefectStudioAssetManagerTests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			defaults = root / "app_assets";
			overrides = root / "user_assets";
			std::filesystem::create_directories(defaults);
			std::filesystem::create_directories(overrides);
		}

		~AssetManagerTestWorkspace()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}

		void WriteDefault(const std::filesystem::path &relative, std::string_view contents = "asset")
		{
			Write(defaults / relative, contents);
		}

		void WriteOverride(const std::filesystem::path &relative, std::string_view contents = "override")
		{
			Write(overrides / relative, contents);
		}

		static void Write(const std::filesystem::path &path, std::string_view contents)
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream stream(path, std::ios::binary);
			stream << contents;
		}
	};
} // namespace

TEST(AssetManagerTests, ResolvePathPrefersUserOverride)
{
	AssetManagerTestWorkspace workspace;
	workspace.WriteDefault("fonts/Default.ttf", "default");
	workspace.WriteOverride("fonts/Default.ttf", "override");

	DefectStudio::AssetManager manager(
		DefectStudio::Path::FromResolved(workspace.defaults),
		DefectStudio::Path::FromResolved(workspace.overrides));

	auto resolved = manager.ResolvePath("fonts/Default.ttf");

	ASSERT_TRUE(resolved);
	EXPECT_TRUE(resolved->fromUserOverride);
	EXPECT_EQ(resolved->resolvedPath.Native(), (workspace.overrides / "fonts/Default.ttf").lexically_normal());
}

TEST(AssetManagerTests, ResolvePathRejectsPathEscapes)
{
	AssetManagerTestWorkspace workspace;
	DefectStudio::AssetManager manager(
		DefectStudio::Path::FromResolved(workspace.defaults),
		DefectStudio::Path::FromResolved(workspace.overrides));

	auto resolved = manager.ResolvePath("../secret.txt");

	ASSERT_FALSE(resolved);
	EXPECT_EQ(resolved.Error().code, "asset.resolve.path_escape");
}

TEST(AssetManagerTests, ValidateRegisteredAssetsSeparatesCriticalAndOptional)
{
	AssetManagerTestWorkspace workspace;
	workspace.WriteDefault("icon.ico");

	DefectStudio::AssetManager manager(
		DefectStudio::Path::FromResolved(workspace.defaults),
		DefectStudio::Path::FromResolved(workspace.overrides));
	manager.RegisterAsset(DefectStudio::AssetDescriptor{
		"icon.ico",
		DefectStudio::AssetType::Icon,
		DefectStudio::AssetCriticality::Critical,
		"1",
		"app.icon"});
	manager.RegisterAsset(DefectStudio::AssetDescriptor{
		"fonts/missing.ttf",
		DefectStudio::AssetType::Font,
		DefectStudio::AssetCriticality::Optional,
		"1",
		"optional.font"});

	const DefectStudio::AssetValidationReport report = manager.ValidateRegisteredAssets();

	EXPECT_FALSE(report.HasCriticalFailures());
	ASSERT_EQ(report.resolvedAssets.size(), 1u);
	ASSERT_EQ(report.issues.size(), 1u);
	EXPECT_EQ(report.issues.front().error.severity, DefectStudio::Severity::Warning);
}
