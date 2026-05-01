#include "Core/dspch.hpp"

#include "Core/Assets/AssetManager.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError makeAssetError(
			std::string code,
			std::string userMessage,
			std::string technicalDetails,
			std::string suggestion,
			Severity severity = Severity::Error)
		{
			return StructuredError{
				ErrorCategory::IO,
				severity,
				std::move(userMessage),
				std::move(technicalDetails),
				std::move(suggestion),
				"AssetManager",
				std::move(code)};
		}

		[[nodiscard]] bool descriptorIsCritical(const AssetDescriptor &descriptor)
		{
			return descriptor.criticality == AssetCriticality::Critical;
		}
	}

	bool AssetValidationReport::HasCriticalFailures() const
	{
		return std::any_of(issues.begin(), issues.end(), [](const AssetValidationIssue &issue) {
			return descriptorIsCritical(issue.descriptor);
		});
	}

	AssetManager::AssetManager(Path defaultRoot, Path userOverrideRoot)
		: m_DefaultRoot(std::move(defaultRoot)), m_UserOverrideRoot(std::move(userOverrideRoot))
	{
	}

	void AssetManager::SetRoots(Path defaultRoot, Path userOverrideRoot)
	{
		m_DefaultRoot = std::move(defaultRoot);
		m_UserOverrideRoot = std::move(userOverrideRoot);
	}

	void AssetManager::RegisterAsset(AssetDescriptor descriptor)
	{
		auto existing = std::find_if(m_Descriptors.begin(), m_Descriptors.end(), [&descriptor](const AssetDescriptor &entry) {
			return entry.logicalPath == descriptor.logicalPath;
		});
		if (existing != m_Descriptors.end())
		{
			*existing = std::move(descriptor);
			return;
		}
		m_Descriptors.push_back(std::move(descriptor));
	}

	void AssetManager::ClearDescriptors()
	{
		m_Descriptors.clear();
	}

	const Path &AssetManager::GetDefaultRoot() const noexcept
	{
		return m_DefaultRoot;
	}

	const Path &AssetManager::GetUserOverrideRoot() const noexcept
	{
		return m_UserOverrideRoot;
	}

	std::vector<AssetDescriptor> AssetManager::ListDescriptors() const
	{
		return m_Descriptors;
	}

	Result<ResolvedAsset> AssetManager::ResolvePath(const std::string &logicalPath) const
	{
		auto normalizedResult = NormalizeLogicalPath(logicalPath);
		if (!normalizedResult)
			return normalizedResult.Error();

		const std::filesystem::path normalized = normalizedResult.Value();
		const std::filesystem::path userCandidate = m_UserOverrideRoot.Empty() ? std::filesystem::path{} : (m_UserOverrideRoot.Native() / normalized);
		if (!userCandidate.empty() && FileSystem::Exists(userCandidate))
		{
			ResolvedAsset resolved;
			resolved.descriptor = FindDescriptor(logicalPath).value_or(AssetDescriptor{logicalPath});
			resolved.resolvedPath = Path::FromResolved(userCandidate.lexically_normal());
			resolved.fromUserOverride = true;
			return resolved;
		}

		const std::filesystem::path defaultCandidate = m_DefaultRoot.Native() / normalized;
		if (FileSystem::Exists(defaultCandidate))
		{
			ResolvedAsset resolved;
			resolved.descriptor = FindDescriptor(logicalPath).value_or(AssetDescriptor{logicalPath});
			resolved.resolvedPath = Path::FromResolved(defaultCandidate.lexically_normal());
			return resolved;
		}

		return makeAssetError(
			"asset.resolve.not_found",
			"Asset could not be resolved.",
			"Logical asset path '" + logicalPath + "' was not found under user override root '" + m_UserOverrideRoot.String()
				+ "' or default root '" + m_DefaultRoot.String() + "'.",
			"Install the missing asset or register a non-critical descriptor if this asset is optional.");
	}

	AssetValidationReport AssetManager::ValidateRegisteredAssets() const
	{
		AssetValidationReport report;
		for (const AssetDescriptor &descriptor : m_Descriptors)
		{
			auto resolved = ResolvePath(descriptor.logicalPath);
			if (resolved)
			{
				ResolvedAsset asset = resolved.Value();
				asset.descriptor = descriptor;
				report.resolvedAssets.push_back(std::move(asset));
				continue;
			}

			StructuredError error = resolved.Error();
			if (!descriptorIsCritical(descriptor))
				error.severity = Severity::Warning;
			report.issues.push_back(AssetValidationIssue{descriptor, std::move(error)});
		}
		return report;
	}

	Result<std::filesystem::path> AssetManager::NormalizeLogicalPath(const std::string &logicalPath) const
	{
		if (logicalPath.empty())
		{
			return makeAssetError(
				"asset.resolve.empty_path",
				"Asset path is empty.",
				"ResolvePath was called with an empty logical asset path.",
				"Use a relative logical path such as 'fonts/CascadiaCode.ttf'.");
		}

		std::filesystem::path path(logicalPath);
		if (path.is_absolute())
		{
			return makeAssetError(
				"asset.resolve.absolute_path",
				"Asset path is not portable.",
				"Logical asset path '" + logicalPath + "' is absolute.",
				"Register asset paths relative to the asset root.");
		}

		path = path.lexically_normal();
		for (const auto &part : path)
		{
			if (part == "..")
			{
				return makeAssetError(
					"asset.resolve.path_escape",
					"Asset path escapes the asset root.",
					"Logical asset path '" + logicalPath + "' contains '..'.",
					"Keep asset descriptors rooted under the configured asset directories.");
			}
		}

		return path;
	}

	std::optional<AssetDescriptor> AssetManager::FindDescriptor(const std::string &logicalPath) const
	{
		auto it = std::find_if(m_Descriptors.begin(), m_Descriptors.end(), [&logicalPath](const AssetDescriptor &descriptor) {
			return descriptor.logicalPath == logicalPath;
		});
		if (it == m_Descriptors.end())
			return std::nullopt;
		return *it;
	}
} // namespace DefectStudio
