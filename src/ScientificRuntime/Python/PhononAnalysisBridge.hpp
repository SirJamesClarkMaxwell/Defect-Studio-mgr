#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// task/104c: runs scripts/python/examples/phonon_vibronic_analysis.py (task 104a). The script
	// publishes a complete result directory (or nothing); the panel then reads it with
	// PhononAnalysisIO::Load. The reference case (162 atoms) takes a few seconds.
	struct PhononAnalysisRequest
	{
		Path forceConstantDirectory;
		Path groundStateDirectory;
		Path excitedStateDirectory;
		std::optional<Path> tripletDirectory; // single point at the excited geometry - spin purification
		std::optional<double> experimentalZplEv;
		double sigmaMeV = 6.0;
		double gammaMeV = 2.0;
		Path outputDirectory;
	};

	struct PhononAnalysisRunSummary
	{
		Path outputDirectory;
		double sTotal = 0.0;
		bool stable = true;
	};

	// The script's command line, in this order: --fc-dir, --gs-dir, --es-dir, [--triplet-dir],
	// [--zpl-exp], --sigma, --gamma, --out-dir; each flag followed by its value as a separate
	// argument. Paths as Utf8(), numbers with std::format("{:.10g}").
	[[nodiscard]] std::vector<std::string> BuildPhononAnalysisArguments(const PhononAnalysisRequest &request);

	// The last JSON line of the script's stdout: {"ok": true, "out_dir", "s_total", "stable"} ->
	// summary; {"ok": false, "error": msg} -> error code "python.phonon.script_failed" whose
	// userMessage contains msg; no JSON line / malformed -> "python.phonon.invalid_json".
	[[nodiscard]] Result<PhononAnalysisRunSummary> ParsePhononAnalysisOutput(const std::string &standardOutput);

	class PhononAnalysisBridge final
	{
	public:
		// Errors: an input directory that does not exist or an empty output path
		// ("python.phonon.request_invalid", checked before Python starts), sigma/gamma <= 0 (same code),
		// then whatever ParsePhononAnalysisOutput reports. The script exits 1 with a JSON error line,
		// so a non-zero exit code is NOT by itself the error - its JSON message is.
		[[nodiscard]] Result<PhononAnalysisRunSummary> Run(
			const PhononAnalysisRequest &request, std::function<bool()> shouldCancel = {}) const;

	private:
		ScriptRunner m_ScriptRunner;
	};

	// PhononAnalysisBridge off the main thread; cancellation is wired into the script run.
	class PhononAnalysisJob final : public IJob
	{
	public:
		explicit PhononAnalysisJob(PhononAnalysisRequest request);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] std::optional<PhononAnalysisRunSummary> &GetResult() noexcept;

	private:
		PhononAnalysisRequest m_Request;
		PhononAnalysisBridge m_Bridge;
		std::optional<PhononAnalysisRunSummary> m_Result;
	};
} // namespace DefectStudio
