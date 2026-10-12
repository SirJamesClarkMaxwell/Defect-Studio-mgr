#include "Core/dspch.hpp"
#include "ScientificRuntime/Python/PhononDispersionJob.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "Core/JobSystem/JobContext.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	PhononDispersionJob::PhononDispersionJob(Path forceConstantDirectory, Path outputDirectory, bool inPlane, int points)
		: m_ForceConstantDirectory(std::move(forceConstantDirectory)), m_OutputDirectory(std::move(outputDirectory)),
		  m_InPlane(inPlane), m_Points(points) {}

	std::string PhononDispersionJob::GetName() const { return "Bulk phonon dispersion"; }
	std::string PhononDispersionJob::GetType() const { return "phonon-dispersion"; }

	void PhononDispersionJob::Execute(JobContext &context)
	{
		context.ThrowIfCancellationRequested();
		m_Result.reset();
		m_Error.reset();
		context.SetStage("phonon-dispersion");
		context.SetMessage("Computing bulk phonon dispersion");
		context.SetProgress(0.0f, 1.0f);
		if (m_OutputDirectory.Empty() || m_Points < 2 || !FileSystem::IsDirectory(m_ForceConstantDirectory.Native()))
			m_Error = MakePythonExecutionError("Phonon dispersion inputs are invalid.",
				"Expected an existing FC directory, an output path and at least two points.",
				"Check dispersion directories and npoints.", "python.phonon_dispersion.request_invalid");
		else
		{
			const auto script = ResolvePythonExampleScript("phonon_dispersion.py");
			ScriptRunOptions options;
			options.scriptPath = script.scriptPath;
			options.workingDirectory = script.workingDirectory;
			options.arguments = {"--fc-dir", m_ForceConstantDirectory.Utf8(), "--out-dir", m_OutputDirectory.Utf8(),
				"--npoints", std::to_string(m_Points)};
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
							"Check bulk FC inputs and choose an empty output directory.", "python.phonon_dispersion.script_failed");
					else
					{
						const double minimum = payload.at("min_freq_meV").get<double>();
						const double maximum = payload.at("max_freq_meV").get<double>();
						const auto directory = payload.at("out_dir").get<std::string>();
						if (run->exitCode != 0 || !payload.at("n_bands").is_number_integer() ||
							payload.at("n_bands").get<int>() <= 0 || !std::isfinite(minimum) || !std::isfinite(maximum) ||
							minimum > maximum || directory.empty())
							throw std::invalid_argument("Invalid dispersion result values or exit code");
						m_Result = Path::FromUtf8(directory);
					}
				}
				catch (const std::exception &exception)
				{
					m_Error = MakePythonExecutionError("Phonon dispersion output parsing failed.",
						std::string(exception.what()) + "\nStdout: " + run->standardOutput + "\nStderr: " + run->standardError,
						"Check phonon_dispersion.py output.", "python.phonon_dispersion.invalid_json");
				}
			}
		}
		if (m_Error) throw std::runtime_error(m_Error->userMessage + "\n" + m_Error->technicalDetails);
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Bulk phonon dispersion computed");
	}
}
