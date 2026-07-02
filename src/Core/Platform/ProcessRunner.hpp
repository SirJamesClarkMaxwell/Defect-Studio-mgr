#pragma once

#include <chrono>
#include <functional>
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
		std::chrono::milliseconds timeout{0};
		std::function<bool()> shouldCancel;
	};

	struct ProcessRunResult
	{
		int exitCode = -1;
		std::string commandLine;
		std::string standardOutput;
		std::string standardError;
		bool timedOut = false;
		bool cancelled = false;
		bool terminated = false;
	};

	[[nodiscard]] Result<ProcessRunResult> RunProcess(const ProcessRunOptions &options);
} // namespace DefectStudio::Platform
