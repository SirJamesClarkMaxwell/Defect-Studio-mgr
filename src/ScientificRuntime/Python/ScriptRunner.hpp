#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct ScriptRunOptions
	{
		Path scriptPath;
		std::vector<std::string> arguments;
		Path workingDirectory;
		std::string pythonExecutable;
		std::chrono::milliseconds timeout{0};
		std::function<bool()> shouldCancel;
		bool requireZeroExitCode = true;
	};

	struct ScriptRunResult
	{
		int exitCode = -1;
		std::string commandLine;
		std::string standardOutput;
		std::string standardError;
		bool timedOut = false;
		bool cancelled = false;
		bool terminated = false;
	};

	class ScriptRunner final
	{
	public:
		[[nodiscard]] Result<ScriptRunResult> RunFile(const ScriptRunOptions &options) const;
	};
} // namespace DefectStudio
