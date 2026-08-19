#pragma once

#include <optional>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	// nullopt = user cancelled (not an error). StructuredError = dialog/init failure.
	[[nodiscard]] Result<std::optional<Path>> PickFolder(const Path &defaultPath = {});

	// nullopt = user cancelled (not an error). StructuredError = dialog/init failure.
	// defaultName should include the extension (e.g. "structure_export.png").
	[[nodiscard]] Result<std::optional<Path>> PickSaveFile(
		const Path &defaultDirectory,
		const std::string &defaultName,
		const std::string &filterName,
		const std::string &filterExtension);
} // namespace DefectStudio::Platform
