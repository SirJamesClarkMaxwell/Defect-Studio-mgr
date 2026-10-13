#include "Core/dspch.hpp"
#include "ScientificRuntime/Python/CcdAnalysisJob.hpp"

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
	std::vector<std::string> BuildCcdAnalysisArguments(const CcdAnalysisRequest &request)
	{
		std::vector<std::string> args;
		const auto path = [&](const char *flag, const Path &value) {
			if (!value.Empty()) args.insert(args.end(), {flag, value.Utf8()});
		};
		path("--gs-dir", request.gsDir);
		path("--es-dir", request.esDir);
		path("--triplet-dir", request.tripletDir);
		path("--gs-at-es-dir", request.gsAtEsDir);
		path("--es-at-gs-dir", request.esAtGsDir);
		path("--ccd-dir", request.ccdDir);
		path("--phonon-result", request.phononResultDir);
		if (!request.zplFormula.empty()) args.insert(args.end(), {"--zpl-formula", request.zplFormula});
		if (!request.label.empty()) args.insert(args.end(), {"--label", request.label});
		args.insert(args.end(), {"--sigma", std::format("{:.10g}", request.sigmaMeV),
			"--temperature", std::format("{:.10g}", request.temperatureK), "--out-dir", request.outputDirectory.Utf8()});
		return args;
	}

	Result<CcdAnalysisRunSummary> ParseCcdAnalysisOutput(const std::string &standardOutput)
	{
		std::istringstream input(standardOutput);
		std::string line, jsonLine;
		while (std::getline(input, line))
			if (const auto candidate = ExtractJsonLineFromOutput(line); !candidate.empty()) jsonLine = candidate;
		try
		{
			const auto payload = nlohmann::json::parse(jsonLine);
			if (!payload.at("ok").get<bool>())
				return MakePythonExecutionError("CCD analysis failed: " + payload.at("error").get<std::string>(),
					"Stdout: " + standardOutput, "Check CCD inputs and curvature.", "python.ccd.script_failed");
			if (!payload.at("zpl_eV").is_number()) throw std::invalid_argument("zpl_eV must be numeric");
			CcdAnalysisRunSummary result{Path::FromUtf8(payload.at("out_dir").get<std::string>()), payload.at("zpl_eV").get<double>()};
			if (result.outputDirectory.Empty() || !std::isfinite(result.zplEv) || result.zplEv <= 0)
				throw std::invalid_argument("expected a nonempty output directory and positive finite ZPL");
			return result;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError("CCD analysis output parsing failed.",
				std::string(exception.what()) + "\nStdout: " + standardOutput,
				"Check ccd_analysis.py output.", "python.ccd.invalid_json");
		}
	}

	Result<CcdAnalysisRunSummary> CcdAnalysisBridge::Run(const CcdAnalysisRequest &request,
		std::function<bool()> shouldCancel) const
	{
		const bool points = !request.gsDir.Empty();
		if (request.outputDirectory.Empty() || request.gsDir.Empty() != request.esDir.Empty() ||
			(points && !request.ccdDir.Empty()) || (!points && request.ccdDir.Empty() && request.phononResultDir.Empty()) ||
			(!points && (!request.tripletDir.Empty() || !request.gsAtEsDir.Empty() || !request.esAtGsDir.Empty())) ||
			!std::isfinite(request.sigmaMeV) || request.sigmaMeV <= 0 ||
			!std::isfinite(request.temperatureK) || request.temperatureK < 0)
			return MakePythonExecutionError("CCD analysis request is invalid.",
				"Use either CCD path or paired GS/ES directories, or a phonon result; positive sigma, nonnegative T and output required.",
				"Check CCD directories and broadening.", "python.ccd.request_invalid");
		for (const auto &directory : {request.gsDir, request.esDir, request.tripletDir, request.gsAtEsDir,
			request.esAtGsDir, request.ccdDir, request.phononResultDir})
		{
			if (directory.Empty()) continue;
			std::error_code error;
			if (!std::filesystem::is_directory(directory.Native(), error))
				return MakePythonExecutionError("A CCD input directory was not found.", directory.Utf8() + "\n" + error.message(),
					"Pick existing CCD input directories.", "python.ccd.request_invalid");
		}
		const auto script = ResolvePythonExampleScript("ccd_analysis.py");
		ScriptRunOptions options;
		options.scriptPath = script.scriptPath;
		options.workingDirectory = script.workingDirectory;
		options.arguments = BuildCcdAnalysisArguments(request);
		options.requireZeroExitCode = false;
		options.shouldCancel = std::move(shouldCancel);
		const auto run = m_ScriptRunner.RunFile(options);
		if (!run) return run.Error();
		auto result = ParseCcdAnalysisOutput(run->standardOutput);
		if (!result) result.Error().technicalDetails += "\nStderr: " + run->standardError;
		else if (run->exitCode != 0)
			return MakePythonExecutionError("CCD analysis exited with an error.", run->standardError,
				"Check ccd_analysis.py output.", "python.ccd.script_failed");
		return result;
	}

	CcdAnalysisJob::CcdAnalysisJob(CcdAnalysisRequest request) : m_Request(std::move(request)) {}
	std::string CcdAnalysisJob::GetName() const { return "CCD / PL 1D"; }
	std::string CcdAnalysisJob::GetType() const { return "ccd-analysis"; }

	void CcdAnalysisJob::Execute(JobContext &context)
	{
		context.ThrowIfCancellationRequested();
		m_Result.reset();
		context.SetStage("ccd-analysis");
		context.SetMessage("Computing CCD wells, vibrational levels and 1D PL");
		context.SetProgress(0.0f, 1.0f);
		auto result = m_Bridge.Run(m_Request, [&context] { return context.IsCancellationRequested(); });
		context.ThrowIfCancellationRequested();
		if (!result) throw std::runtime_error(result.Error().userMessage + "\n" + result.Error().technicalDetails);
		m_Result = std::move(result).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("CCD analysis completed");
	}
}
