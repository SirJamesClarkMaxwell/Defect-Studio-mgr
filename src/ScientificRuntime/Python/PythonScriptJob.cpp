#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PythonScriptJob.hpp"

#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	PythonScriptJob::PythonScriptJob(
		std::string name,
		Path scriptPath,
		std::vector<std::string> arguments,
		Path workingDirectory)
		: m_Name(std::move(name)),
		  m_ScriptPath(std::move(scriptPath)),
		  m_Arguments(std::move(arguments)),
		  m_WorkingDirectory(std::move(workingDirectory))
	{
	}

	std::string PythonScriptJob::GetName() const
	{
		return m_Name;
	}

	std::string PythonScriptJob::GetType() const
	{
		return "PythonScriptJob";
	}

	bool PythonScriptJob::UsesPythonRuntime() const
	{
		return true;
	}

	void PythonScriptJob::Execute(JobContext &context)
	{
		context.SetStage("python-script");
		context.SetMessage("Running Python script job");
		context.SetProgress(0.0f, 1.0f);

		ScriptRunOptions options;
		options.scriptPath = m_ScriptPath;
		options.arguments = m_Arguments;
		if (m_WorkingDirectory.Empty())
			options.workingDirectory = Path::FromResolved(FileSystem::CurrentPath());
		else
			options.workingDirectory = m_WorkingDirectory;

		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		if (!runResult)
			throw std::runtime_error(runResult.Error().technicalDetails);

		if (!runResult->standardOutput.empty())
			context.LogInfo("python stdout: " + runResult->standardOutput);
		if (!runResult->standardError.empty())
			context.LogWarning("python stderr: " + runResult->standardError);

		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Python script completed");
	}
} // namespace DefectStudio
