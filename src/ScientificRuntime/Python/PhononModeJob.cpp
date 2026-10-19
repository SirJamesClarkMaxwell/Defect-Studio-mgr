#include "Core/dspch.hpp"
#include "ScientificRuntime/Python/PhononModeJob.hpp"

#include <cmath>
#include <format>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "Core/JobSystem/JobContext.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	PhononModeJob::PhononModeJob(Path forceConstantDirectory, Path outputDirectory, glm::dvec3 q, int band, std::string supercell, bool inPlane)
		: m_ForceConstantDirectory(std::move(forceConstantDirectory)), m_OutputDirectory(std::move(outputDirectory)),
		  m_InPlane(inPlane), m_Q(q), m_Band(band), m_Supercell(std::move(supercell)) {}

	std::string PhononModeJob::GetName() const { return "Bulk phonon mode"; }
	std::string PhononModeJob::GetType() const { return "phonon-mode"; }

	void PhononModeJob::Execute(JobContext &context)
	{
		context.ThrowIfCancellationRequested();
		m_Result.reset();
		m_Error.reset();
		context.SetStage("phonon-mode");
		context.SetMessage("Computing bulk phonon mode");
		context.SetProgress(0.0f, 1.0f);
		if (m_OutputDirectory.Empty() || m_Band < 0 || !std::isfinite(m_Q.x) || !std::isfinite(m_Q.y) || !std::isfinite(m_Q.z) || !FileSystem::IsDirectory(m_ForceConstantDirectory.Native()))
			m_Error = MakePythonExecutionError("Phonon mode inputs are invalid.",
				"Expected an existing FC directory, an output path and a finite q and nonnegative band.",
				"Check bulk FC directory and q/band.", "python.phonon_mode.request_invalid");
		else
		{
			const auto script = ResolvePythonExampleScript("phonon_mode.py");
			ScriptRunOptions options;
			options.scriptPath = script.scriptPath;
			options.workingDirectory = script.workingDirectory;
			options.arguments = {"--fc-dir", m_ForceConstantDirectory.Utf8(), "--out-dir", m_OutputDirectory.Utf8(),
				"--q", std::format("{:.17g}", m_Q.x), std::format("{:.17g}", m_Q.y), std::format("{:.17g}", m_Q.z),
				"--band", std::to_string(m_Band), "--supercell", m_Supercell};
			if (m_InPlane) options.arguments.push_back("--in-plane");
			options.requireZeroExitCode = false;
			options.shouldCancel = [&context] { return context.IsCancellationRequested(); };
			const auto run = m_ScriptRunner.RunFile(options);
			context.ThrowIfCancellationRequested();
			if (!run)
				m_Error = run.Error();
			else
			{
				std::istringstream output(run->standardOutput);
				std::string line, jsonLine;
				while (std::getline(output, line))
					if (const auto candidate = ExtractJsonLineFromOutput(line); !candidate.empty()) jsonLine = candidate;
				try
				{
					const auto payload = nlohmann::json::parse(jsonLine);
					if (!payload.at("ok").get<bool>())
						m_Error = MakePythonExecutionError(payload.at("error").get<std::string>(), run->standardError,
							"Check bulk FC inputs and choose an empty output directory.", "python.phonon_mode.script_failed");
					else
					{
						const auto directory = payload.at("out_dir").get<std::string>();
						if (run->exitCode != 0 || directory.empty())
							throw std::invalid_argument("Invalid bulk mode output or exit code");
						m_Result = Path::FromUtf8(directory);
					}
				}
				catch (const std::exception &exception)
				{
					m_Error = MakePythonExecutionError("Phonon mode output parsing failed.",
						std::string(exception.what()) + "\nStdout: " + run->standardOutput + "\nStderr: " + run->standardError,
						"Check phonon_mode.py output.", "python.phonon_mode.invalid_json");
				}
			}
		}
		if (m_Error) throw std::runtime_error(m_Error->userMessage + "\n" + m_Error->technicalDetails);
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Bulk phonon mode computed");
	}
}
