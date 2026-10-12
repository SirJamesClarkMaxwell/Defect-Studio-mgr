#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	std::vector<std::string> BuildPhononAnalysisArguments(const PhononAnalysisRequest &request)
	{
		std::vector<std::string> arguments = {
			"--fc-dir", request.forceConstantDirectory.Utf8(),
			"--gs-dir", request.groundStateDirectory.Utf8(),
			"--es-dir", request.excitedStateDirectory.Utf8()};
		if (request.tripletDirectory)
			arguments.insert(arguments.end(), {"--triplet-dir", request.tripletDirectory->Utf8()});
		if (request.experimentalZplEv)
			arguments.insert(arguments.end(), {"--zpl-exp", std::format("{:.10g}", *request.experimentalZplEv)});
		arguments.insert(arguments.end(), {
			"--sigma", std::format("{:.10g}", request.sigmaMeV),
			"--gamma", std::format("{:.10g}", request.gammaMeV)});
		if (request.bulkForceConstantDirectory)
			arguments.insert(arguments.end(), {"--bulk-fc-dir", request.bulkForceConstantDirectory->Utf8()});
		if (request.embeddingMult)
		{
			const auto &mult = *request.embeddingMult;
			arguments.insert(arguments.end(), {"--mult", std::format("{}x{}x{}", mult[0], mult[1], mult[2])});
		}
		if (request.dcutoffAngstrom)
			arguments.insert(arguments.end(), {"--dcutoff", std::format("{:.10g}", *request.dcutoffAngstrom)});
		if (request.bcutoffAngstrom)
			arguments.insert(arguments.end(), {"--bcutoff", std::format("{:.10g}", *request.bcutoffAngstrom)});
		arguments.insert(arguments.end(), {"--out-dir", request.outputDirectory.Utf8()});
		return arguments;
	}

	Result<PhononAnalysisRunSummary> ParsePhononAnalysisOutput(const std::string &standardOutput)
	{
		// The shared extractor returns the first JSON line; this script's final line is the result.
		std::istringstream input(standardOutput);
		std::string line;
		std::string jsonLine;
		while (std::getline(input, line))
		{
			const std::string candidate = ExtractJsonLineFromOutput(line);
			if (!candidate.empty())
				jsonLine = candidate;
		}
		try
		{
			const auto payload = nlohmann::json::parse(jsonLine);
			if (!payload.at("ok").get<bool>())
			{
				const auto message = payload.at("error").get<std::string>();
				return MakePythonExecutionError(
					"Phonon analysis failed: " + message, "Stdout: " + standardOutput,
					"Check the phonon analysis inputs.", "python.phonon.script_failed");
			}
			if (!payload.at("s_total").is_number())
				throw std::invalid_argument("s_total must be numeric");
			PhononAnalysisRunSummary summary;
			summary.outputDirectory = Path::FromUtf8(payload.at("out_dir").get<std::string>());
			summary.sTotal = payload.at("s_total").get<double>();
			summary.stable = payload.at("stable").get<bool>();
			if (summary.outputDirectory.Empty() || !std::isfinite(summary.sTotal))
				throw std::invalid_argument("Expected a nonempty output directory and finite s_total");
			return summary;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Phonon analysis output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nStdout: " + standardOutput,
				"Ensure phonon_vibronic_analysis.py prints its JSON result line.", "python.phonon.invalid_json");
		}
	}

	Result<PhononAnalysisRunSummary> PhononAnalysisBridge::Run(
		const PhononAnalysisRequest &request, std::function<bool()> shouldCancel) const
	{
		if (request.outputDirectory.Empty() || !std::isfinite(request.sigmaMeV) || request.sigmaMeV <= 0.0 ||
			!std::isfinite(request.gammaMeV) || request.gammaMeV <= 0.0 ||
			(request.experimentalZplEv && (!std::isfinite(*request.experimentalZplEv) || *request.experimentalZplEv <= 0.0)))
		{
			return MakePythonExecutionError(
				"Phonon analysis request is invalid.",
				"Expected an output path, positive finite sigma/gamma and a positive finite experimental ZPL when provided.",
				"Check the output path, broadening and experimental ZPL.", "python.phonon.request_invalid");
		}
		if (request.bulkForceConstantDirectory.has_value() != request.embeddingMult.has_value() ||
			(request.embeddingMult && std::any_of(request.embeddingMult->begin(), request.embeddingMult->end(),
				[](int value) { return value < 1; })) ||
			(request.dcutoffAngstrom && (!request.embeddingMult || !std::isfinite(*request.dcutoffAngstrom) || *request.dcutoffAngstrom <= 0.0)) ||
			(request.bcutoffAngstrom && (!request.embeddingMult || !std::isfinite(*request.bcutoffAngstrom) || *request.bcutoffAngstrom <= 0.0)))
		{
			return MakePythonExecutionError(
				"Phonon embedding request is invalid.",
				"Expected both bulk FC directory and three positive multipliers; cutoffs require embedding and must be positive and finite.",
				"Check the embedding directory, multipliers and cutoffs.", "python.phonon.request_invalid");
		}
		for (const Path &directory : {request.forceConstantDirectory, request.groundStateDirectory,
			request.excitedStateDirectory, request.tripletDirectory.value_or(request.excitedStateDirectory),
			request.bulkForceConstantDirectory.value_or(request.forceConstantDirectory)})
		{
			std::error_code error;
			if (directory.Empty() || !std::filesystem::is_directory(directory.Native(), error))
			{
				return MakePythonExecutionError(
					"A phonon analysis input directory was not found.",
					"Directory: " + directory.Utf8() + "\n" + error.message(),
					"Pick existing force-constant, ground-state, excited-state and optional triplet/bulk directories.",
					"python.phonon.request_invalid");
			}
		}

		const PythonExampleScript script = ResolvePythonExampleScript("phonon_vibronic_analysis.py");
		ScriptRunOptions options;
		options.scriptPath = script.scriptPath;
		options.workingDirectory = script.workingDirectory;
		options.arguments = BuildPhononAnalysisArguments(request);
		options.requireZeroExitCode = false;
		options.shouldCancel = std::move(shouldCancel);
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		if (!runResult)
			return runResult.Error();
		if (runResult->standardError.find("ModuleNotFoundError: No module named 'dephonopy'") != std::string::npos)
		{
			return MakePythonUnavailableError(
				"dephonopy is not installed.", runResult->standardError,
				"Install dephonopy in the application's Python environment.", "python.phonon.dephonopy_not_installed");
		}
		auto summary = ParsePhononAnalysisOutput(runResult->standardOutput);
		if (!summary)
			summary.Error().technicalDetails += "\nStderr: " + runResult->standardError;
		return summary;
	}
} // namespace DefectStudio
