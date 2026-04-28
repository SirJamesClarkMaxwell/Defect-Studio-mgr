#include "Core/dspch.hpp"

#include "Core/Platform/PlatformFontDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Utils/Logger.hpp"

namespace DefectStudio::Platform
{
	namespace
	{
		std::string toLowerCopy(std::string value)
		{
			for (char &character : value)
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			return value;
		}

		bool isSupportedFontFile(const std::filesystem::path &path)
		{
			const std::string extension = toLowerCopy(path.extension().string());
			return extension == ".ttf" || extension == ".otf";
		}

		std::string fontLabelFromPath(const std::filesystem::path &fontPath, std::string_view source)
		{
			const std::string stem = fontPath.stem().string();
			const std::string filename = fontPath.filename().string();
			return std::string(source) + ": " + (!stem.empty() ? stem : filename);
		}

		void collectFontsFromDirectory(std::vector<FontOption> &fontOptions,
		                               std::vector<std::string> &seenPaths,
		                               const std::filesystem::path &directory,
		                               std::string_view source)
		{
			std::error_code error;
			if (!std::filesystem::exists(directory, error) || error)
				return;

			std::filesystem::recursive_directory_iterator iterator(
				directory,
				std::filesystem::directory_options::skip_permission_denied,
				error);
			const std::filesystem::recursive_directory_iterator end;
			if (error)
			{
				DS_LOG_WARN("Font scan skipped [{}]: {}", directory.string(), error.message());
				return;
			}

			for (; iterator != end; iterator.increment(error))
			{
				if (error)
				{
					DS_LOG_WARN("Font scan warning [{}]: {}", directory.string(), error.message());
					error.clear();
					continue;
				}

				const std::filesystem::directory_entry &entry = *iterator;
				if (!entry.is_regular_file(error) || error)
				{
					error.clear();
					continue;
				}

				const std::filesystem::path fontPath = entry.path();
				if (!isSupportedFontFile(fontPath))
					continue;

				const std::string normalizedPath = toLowerCopy(fontPath.lexically_normal().string());
				if (std::find(seenPaths.begin(), seenPaths.end(), normalizedPath) != seenPaths.end())
					continue;

				FontOption option;
				option.label = fontLabelFromPath(fontPath, source);
				option.source = std::string(source);
				option.path = FileSystem::Absolute(fontPath, error).lexically_normal().string();
				if (error)
				{
					error.clear();
					option.path = fontPath.lexically_normal().string();
				}

				fontOptions.push_back(std::move(option));
				seenPaths.push_back(std::move(normalizedPath));
			}
		}
	} // namespace

	FontOption DefaultFontOption()
	{
		FontOption option;
		option.label = "Default: ImGui";
		option.source = "Default";
		return option;
	}

	bool FontPathsEqual(std::string_view left, std::string_view right)
	{
		return toLowerCopy(std::string(left)) == toLowerCopy(std::string(right));
	}

	std::vector<FontOption> GetFontsFromDirectory(const Path &directory, std::string_view source)
	{
		std::vector<FontOption> fontOptions;
		std::vector<std::string> seenPaths;
		collectFontsFromDirectory(fontOptions, seenPaths, directory.Native(), source);
		std::sort(fontOptions.begin(), fontOptions.end(), [](const FontOption &left, const FontOption &right) {
			if (left.source != right.source)
				return left.source < right.source;
			return left.label < right.label;
		});
		return fontOptions;
	}

	std::vector<FontOption> GetSystemFonts()
	{
		std::vector<FontOption> fontOptions;
		std::vector<std::string> seenPaths;
		for (const FilePath &directory : GetSystemFontDirectories())
			collectFontsFromDirectory(fontOptions, seenPaths, directory, "System");
		std::sort(fontOptions.begin(), fontOptions.end(), [](const FontOption &left, const FontOption &right) {
			if (left.source != right.source)
				return left.source < right.source;
			return left.label < right.label;
		});
		return fontOptions;
	}
} // namespace DefectStudio::Platform
