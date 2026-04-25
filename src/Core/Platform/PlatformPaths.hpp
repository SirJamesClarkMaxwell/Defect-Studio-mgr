#pragma once

#include <vector>

#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	[[nodiscard]] std::vector<FilePath> GetSystemFontDirectories();
} // namespace DefectStudio::Platform
