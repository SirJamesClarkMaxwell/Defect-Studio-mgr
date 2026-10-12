#include "Core/dspch.hpp"
#include "ScientificRuntime/Python/CcdInputsJob.hpp"

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
	CcdInputsJob::CcdInputsJob(Path groundStateDirectory, Path excitedStateDirectory, Path outputDirectory, double step)
		: m_GroundStateDirectory(std::move(groundStateDirectory)), m_ExcitedStateDirectory(std::move(excitedStateDirectory)),
		  m_OutputDirectory(std::move(outputDirectory)), m_Step(step) {}

	std::string CcdInputsJob::GetName() const { return "CCD inputs"; }
	std::string CcdInputsJob::GetType() const { return "ccd-inputs"; }

	void CcdInputsJob::Execute(JobContext &context)
	{
		context.ThrowIfCancellationRequested();
		m_Result.reset();
		m_Error.reset();
		context.SetStage("ccd-inputs");
		context.SetMessage("Generating CCD single-point inputs");
		context.SetProgress(0.0f, 1.0f);
		if (m_OutputDirectory.Empty() || !std::isfinite(m_Step) || m_Step <= 0.0 ||
			!FileSystem::IsDirectory(m_GroundStateDirectory.Native()) || !FileSystem::IsDirectory(m_ExcitedStateDirectory.Native()))
			m_Error = MakePythonExecutionError("CCD inputs are invalid.",
				"Expected existing GS/ES directories, an output path and a positive finite step.",
				"Check CCD directories and step.", "python.ccd.request_invalid");
		else
		{
			const auto script = ResolvePythonExampleScript("ccd_inputs.py");
			ScriptRunOptions options;
			options.scriptPath = script.scriptPath;
			options.workingDirectory = script.workingDirectory;
			options.arguments = {"--gs-dir", m_GroundStateDirectory.Utf8(), "--es-dir", m_ExcitedStateDirectory.Utf8(),
				"--out-dir", m_OutputDirectory.Utf8(), "--step", std::format("{:.10g}", m_Step)};
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
							"Check CCD inputs and choose an empty output directory.", "python.ccd.script_failed");
					else
					{
						const double deltaQ = payload.at("delta_Q_amu05_A").get<double>();
						if (!payload.at("points").is_number_integer() || payload.at("points").get<int>() <= 0 ||
							!std::isfinite(deltaQ) || deltaQ < 0.0 || payload.at("out_dir").get<std::string>().empty())
							throw std::invalid_argument("Invalid CCD result values");
						m_Result = payload.dump(2);
					}
				}
				catch (const std::exception &exception)
				{
					m_Error = MakePythonExecutionError("CCD output parsing failed.",
						std::string(exception.what()) + "\nStdout: " + run->standardOutput + "\nStderr: " + run->standardError,
						"Check ccd_inputs.py output.", "python.ccd.invalid_json");
				}
			}
		}
		if (m_Error)
			throw std::runtime_error(m_Error->userMessage + "\n" + m_Error->technicalDetails);
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("CCD inputs generated");
	}
}
