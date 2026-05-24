#pragma once

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
		bool requireZeroExitCode = true;
	};

	struct ScriptRunResult
	{
		int exitCode = -1;
		std::string commandLine;
		std::string standardOutput;
		std::string standardError;
	};

	class ScriptRunner final
	{
	public:
		[[nodiscard]] Result<ScriptRunResult> RunFile(const ScriptRunOptions &options) const;
	};
} // namespace DefectStudio
