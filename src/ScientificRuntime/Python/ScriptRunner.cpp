#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScriptRunner.hpp"

#include <fstream>
#include <sstream>

#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"

namespace DefectStudio
{
	static std::string quoteShellArgument(const std::string &raw)
	{
		std::string escaped;
		escaped.reserve(raw.size() + 2);
		escaped.push_back('"');
		for (char ch : raw)
		{
			if (ch == '"')
				escaped.push_back('\\');
			escaped.push_back(ch);
		}
		escaped.push_back('"');
		return escaped;
	}

	static std::string readTextFile(const Path &filePath)
	{
		std::ifstream stream(filePath.Native(), std::ios::binary);
		if (!stream)
			return {};

		std::ostringstream output;
		output << stream.rdbuf();
		return output.str();
	}

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
		const Path tempDirectory = Path::FromResolved(FileSystem::TempDirectoryPath());
		const auto runToken = std::to_string(Time::NowSteady().time_since_epoch().count());
		const Path stdoutPath = tempDirectory / ("defectstudio_python_stdout_" + runToken + ".log");
		const Path stderrPath = tempDirectory / ("defectstudio_python_stderr_" + runToken + ".log");

		std::ostringstream commandBuilder;
		commandBuilder << quoteShellArgument(pythonExecutable.String()) << " ";
		commandBuilder << quoteShellArgument(scriptPath.String());
		for (const std::string &argument : options.arguments)
			commandBuilder << " " << quoteShellArgument(argument);
		commandBuilder << " > " << quoteShellArgument(stdoutPath.String());
		commandBuilder << " 2> " << quoteShellArgument(stderrPath.String());

		const std::string payloadCommand = commandBuilder.str();
		const std::string shellCommand =
			Platform::BuildShellCommand(quoteShellArgument(workingDirectory.String()), payloadCommand);

		ScriptRunResult result;
		result.commandLine = shellCommand;
		result.exitCode = std::system(shellCommand.c_str());
		result.standardOutput = readTextFile(stdoutPath);
		result.standardError = readTextFile(stderrPath);

		std::error_code removeError;
		FileSystem::Remove(stdoutPath, removeError);
		FileSystem::Remove(stderrPath, removeError);

		if (options.requireZeroExitCode && result.exitCode != 0)
		{
			return MakePythonExecutionError(
				"Python script execution failed.",
				"Command: " + shellCommand + "\nExit code: " + std::to_string(result.exitCode) + "\nStderr:\n" + result.standardError,
				"Check install/app/python runtime content (or fallback .venv) and script arguments.",
				"python.script.non_zero_exit");
		}

		return result;
	}
} // namespace DefectStudio
