#include "Core/dspch.hpp"
#include "ScientificRuntime/Python/ServerJobBridge.hpp"

#include <cmath>
#include <format>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	std::vector<std::string> BuildServerJobArguments(const ServerJobRequest &request)
	{
		auto inputs = request.inputs;
		inputs.embeddingMult.reset();
		inputs.serverHrDirectory = {};
		auto arguments = BuildPhononAnalysisArguments(inputs);
		arguments.insert(arguments.begin(), {"--templates-dir", request.templatesDirectory.Utf8(),
			"--n-min", std::to_string(request.nMin), "--n-max", std::to_string(request.nMax)});
		if (!request.fallbackOutputDirectory.Empty())
			arguments.insert(arguments.end(), {"--fallback-dir", request.fallbackOutputDirectory.Utf8()});
		// The generator evaluates the same ZPL formula; numeric experimental input is in meV here.
		for (std::size_t i = 0; i + 1 < arguments.size(); ++i)
			if (arguments[i] == "--zpl-exp")
			{
				arguments[i] = "--zpl";
				arguments[i + 1] = std::format("{:.10g}", *inputs.experimentalZplEv * 1000.0);
			}
		return arguments;
	}

	Result<Path> ParseServerJobOutput(const std::string &standardOutput)
	{
		std::istringstream input(standardOutput);
		std::string line, json;
		while (std::getline(input, line))
			if (!ExtractJsonLineFromOutput(line).empty()) json = line;
		try
		{
			const auto payload = nlohmann::json::parse(json);
			if (!payload.at("ok").get<bool>())
				return MakePythonExecutionError(payload.at("error").get<std::string>(), standardOutput,
					"Check the server templates and inputs.", "python.server_job.script_failed");
			const auto path = Path::FromUtf8(payload.at("out_dir").get<std::string>());
			if (path.Empty()) throw std::invalid_argument("Empty output path");
			return path;
		}
		catch (const std::exception &error)
		{
			return MakePythonExecutionError("Invalid server job generator output.", error.what(),
				"Check server_job.py output.", "python.server_job.invalid_json");
		}
	}

	Result<Path> ServerJobBridge::Run(const ServerJobRequest &request, std::function<bool()> shouldCancel) const
	{
		const auto &s = request.inputs;
		if (request.nMin < 1 || request.nMax < request.nMin || s.outputDirectory.Empty() ||
			!s.bulkForceConstantDirectory || !std::isfinite(s.sigmaMeV) || s.sigmaMeV <= 0 ||
			!std::isfinite(s.gammaMeV) || s.gammaMeV <= 0 ||
			(s.hrMethod != "displacements" && s.hrMethod != "approximate_forces" && s.hrMethod != "forces") ||
			(s.hrMethod == "forces" && s.groundAtExcitedDirectory.Empty()) ||
			(s.sigmaMaxMeV && (!std::isfinite(*s.sigmaMaxMeV) || *s.sigmaMaxMeV < s.sigmaMeV)))
			return MakePythonExecutionError("Invalid server job inputs or size range.", "Invalid request.",
				"Choose FC, bulk FC, GS/ES, output and positive sizes/broadening.", "python.server_job.request_invalid");
		for (const auto value : {s.hrCutoffAngstrom, s.dcutoffAngstrom, s.bcutoffAngstrom, s.experimentalZplEv})
			if (value && (!std::isfinite(*value) || *value <= 0))
				return MakePythonExecutionError("Invalid server job cutoff or ZPL.", "Nonpositive/nonfinite value.",
					"Use positive finite values.", "python.server_job.request_invalid");
		for (const auto &directory : {request.templatesDirectory, s.forceConstantDirectory,
			*s.bulkForceConstantDirectory, s.groundStateDirectory, s.excitedStateDirectory})
		{
			std::error_code error;
			if (directory.Empty() || !std::filesystem::is_directory(directory.Native(), error))
				return MakePythonExecutionError("Server job directory not found: " + directory.Utf8(), error.message(),
					"Choose existing templates and input directories.", "python.server_job.request_invalid");
		}
		const auto script = ResolvePythonExampleScript("server_job.py");
		ScriptRunOptions options;
		options.scriptPath = script.scriptPath;
		options.workingDirectory = script.workingDirectory;
		options.arguments = BuildServerJobArguments(request);
		options.requireZeroExitCode = false;
		options.shouldCancel = std::move(shouldCancel);
		const auto run = m_ScriptRunner.RunFile(options);
		if (!run) return run.Error();
		auto result = ParseServerJobOutput(run->standardOutput);
		if (!result) result.Error().technicalDetails += "\nStderr: " + run->standardError;
		return result;
	}
}
