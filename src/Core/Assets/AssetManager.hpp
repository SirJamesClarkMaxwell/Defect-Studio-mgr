#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	enum class AssetType
	{
		Unknown,
		Font,
		Icon,
		Shader,
		Texture,
		Config,
		Script,
		Data
	};

	enum class AssetCriticality
	{
		Optional,
		Critical
	};

	struct AssetDescriptor
	{
		std::string logicalPath;
		AssetType type = AssetType::Unknown;
		AssetCriticality criticality = AssetCriticality::Optional;
		std::string version;
		std::string cacheKey;
	};

	struct ResolvedAsset
	{
		AssetDescriptor descriptor;
		Path resolvedPath;
		bool fromUserOverride = false;
	};

	struct AssetValidationIssue
	{
		AssetDescriptor descriptor;
		StructuredError error;
	};

	struct AssetValidationReport
	{
		std::vector<ResolvedAsset> resolvedAssets;
		std::vector<AssetValidationIssue> issues;

		[[nodiscard]] bool HasCriticalFailures() const;
	};

	class AssetManager
	{
	public:
		AssetManager() = default;
		AssetManager(Path defaultRoot, Path userOverrideRoot = {});

		void SetRoots(Path defaultRoot, Path userOverrideRoot = {});
		void RegisterAsset(AssetDescriptor descriptor);
		void ClearDescriptors();

		[[nodiscard]] const Path &GetDefaultRoot() const noexcept;
		[[nodiscard]] const Path &GetUserOverrideRoot() const noexcept;
		[[nodiscard]] std::vector<AssetDescriptor> ListDescriptors() const;
		[[nodiscard]] Result<ResolvedAsset> ResolvePath(const std::string &logicalPath) const;
		[[nodiscard]] AssetValidationReport ValidateRegisteredAssets() const;

	private:
		[[nodiscard]] Result<std::filesystem::path> NormalizeLogicalPath(const std::string &logicalPath) const;
		[[nodiscard]] std::optional<AssetDescriptor> FindDescriptor(const std::string &logicalPath) const;

		Path m_DefaultRoot;
		Path m_UserOverrideRoot;
		std::vector<AssetDescriptor> m_Descriptors;
	};
} // namespace DefectStudio
