#pragma once

#include <optional>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	// nullopt = user cancelled (not an error). StructuredError = dialog/init failure.
	[[nodiscard]] Result<std::optional<Path>> PickFolder(const Path &defaultPath = {});
} // namespace DefectStudio::Platform
