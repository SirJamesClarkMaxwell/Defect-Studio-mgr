#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScriptRunner.hpp"

#include "Core/Platform/ProcessRunner.hpp"
#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	static Path resolvePythonExecutable(const ScriptRunOptions &options)
	{
		if (!options.pythonExecutable.empty())
			return Path(options.pythonExecutable);

		return Platform::ResolvePreferredPythonExecutable();
	}

	static Path resolveWorkingDirectory(const ScriptRunOptions &options)
	{
		if (!options.workingDirectory.Empty())
			return options.workingDirectory;

		return FileSystem::CurrentPath();
	}

	Result<ScriptRunResult> ScriptRunner::RunFile(const ScriptRunOptions &options) const
	{
		if (options.scriptPath.Empty())
		{
			return MakePythonExecutionError(
				"Python script path is missing.",
				"ScriptRunner received an empty script path.",
				"Provide an existing file path from scripts/python/examples.",
				"python.script.path_missing");
		}

		const Path scriptPath = options.scriptPath;
		if (!FileSystem::Exists(scriptPath))
		{
			return MakePythonExecutionError(
				"Python script file was not found.",
				"Missing script file: " + scriptPath.String(),
				"Ensure scripts/python/examples contains the requested script and path casing matches.",
				"python.script.path_not_found");
		}

		const Path workingDirectory = resolveWorkingDirectory(options);
		if (!FileSystem::Exists(workingDirectory))
		{
			return MakePythonExecutionError(
				"Python working directory does not exist.",
				"Invalid working directory: " + workingDirectory.String(),
				"Use a valid repository-relative working directory before running scripts.",
				"python.script.working_directory_not_found");
		}

		const Path pythonExecutable = resolvePythonExecutable(options);
		Platform::ProcessRunOptions processOptions;
		processOptions.executable = pythonExecutable;
		processOptions.arguments.push_back(scriptPath.String());
		processOptions.arguments.insert(
			processOptions.arguments.end(),
			options.arguments.begin(),
			options.arguments.end());
		processOptions.workingDirectory = workingDirectory;
		processOptions.timeout = options.timeout;
		processOptions.shouldCancel = options.shouldCancel;

		Result<Platform::ProcessRunResult> processResult = Platform::RunProcess(processOptions);
		if (!processResult.HasValue())
			return processResult.Error();

		if (processResult->cancelled)
		{
			return StructuredError{
				ErrorCategory::Cancelled,
				Severity::Warning,
				"Python script cancelled.",
				"Command cancelled: " + processResult->commandLine,
				"No action required if the cancellation was intentional.",
				"ScientificRuntime/Python/ScriptRunner",
				"python.script.cancelled"};
		}

		if (processResult->timedOut)
		{
			return MakePythonExecutionError(
				"Python script timed out.",
				"Command timed out and was terminated: " + processResult->commandLine,
				"Increase the timeout or check the Python script for hangs.",
				"python.script.timeout");
		}

		ScriptRunResult result;
		result.exitCode = processResult->exitCode;
		result.commandLine = processResult->commandLine;
		result.standardOutput = std::move(processResult->standardOutput);
		result.standardError = std::move(processResult->standardError);
		result.timedOut = processResult->timedOut;
		result.cancelled = processResult->cancelled;
		result.terminated = processResult->terminated;

		if (options.requireZeroExitCode && result.exitCode != 0)
		{
			return MakePythonExecutionError(
				"Python script execution failed.",
				"Command: " + result.commandLine + "\nExit code: " + std::to_string(result.exitCode) + "\nStderr:\n" + result.standardError,
				"Check install/app/python runtime content (or fallback .venv) and script arguments.",
				"python.script.non_zero_exit");
		}

		return result;
	}
} // namespace DefectStudio
