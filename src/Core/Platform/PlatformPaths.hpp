#pragma once

#include <vector>

#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	[[nodiscard]] std::vector<FilePath> GetSystemFontDirectories();
	[[nodiscard]] Path GetExecutableDirectory();
	// Same spelling as the file system compares names: case-insensitive (Unicode) on Windows, exact elsewhere.
	[[nodiscard]] bool FileSystemNamesEqual(const FilePath &left, const FilePath &right);
} // namespace DefectStudio::Platform
