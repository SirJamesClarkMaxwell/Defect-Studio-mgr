#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	struct FontOption
	{
		std::string label;
		std::string source;
		std::string path;
	};

	struct FontScanResult
	{
		std::vector<FontOption> fontOptions;
		std::size_t localFontCount = 0;
		std::size_t systemFontCount = 0;
	};

	[[nodiscard]] FontOption DefaultFontOption();
	[[nodiscard]] bool FontPathsEqual(std::string_view left, std::string_view right);
	[[nodiscard]] std::vector<FontOption> GetFontsFromDirectory(const Path &directory, std::string_view source);
	[[nodiscard]] std::vector<FontOption> GetSystemFonts();
} // namespace DefectStudio::Platform
