#pragma once

#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	struct ProcessRunOptions
	{
		Path executable;
		std::vector<std::string> arguments;
		Path workingDirectory;
	};

	struct ProcessRunResult
	{
		int exitCode = -1;
		std::string commandLine;
		std::string standardOutput;
		std::string standardError;
	};

	[[nodiscard]] Result<ProcessRunResult> RunProcess(const ProcessRunOptions &options);
} // namespace DefectStudio::Platform
