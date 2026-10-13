#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct CcdAnalysisRequest
	{
		Path gsDir, esDir, tripletDir, gsAtEsDir, esAtGsDir;
		Path ccdDir, phononResultDir;
		std::string zplFormula;
		double sigmaMeV = 6.0;
		double temperatureK = 0.0;
		std::string label;
		Path outputDirectory;
	};

	struct CcdAnalysisRunSummary
	{
		Path outputDirectory;
		double zplEv = 0.0;
	};

	// ccd_analysis.py flags; unset paths/text omitted, Utf8 paths, {:.10g} numbers.
	[[nodiscard]] std::vector<std::string> BuildCcdAnalysisArguments(const CcdAnalysisRequest &request);
	[[nodiscard]] Result<CcdAnalysisRunSummary> ParseCcdAnalysisOutput(const std::string &standardOutput);

	class CcdAnalysisBridge final
	{
	public:
		[[nodiscard]] Result<CcdAnalysisRunSummary> Run(const CcdAnalysisRequest &request,
			std::function<bool()> shouldCancel = {}) const;
	private:
		ScriptRunner m_ScriptRunner;
	};

	// Executes off the main thread; result is read only after JobSystem reports completion.
	class CcdAnalysisJob final : public IJob
	{
	public:
		explicit CcdAnalysisJob(CcdAnalysisRequest request);
		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;
		[[nodiscard]] const std::optional<CcdAnalysisRunSummary> &GetResult() const noexcept { return m_Result; }
	private:
		CcdAnalysisRequest m_Request;
		CcdAnalysisBridge m_Bridge;
		std::optional<CcdAnalysisRunSummary> m_Result;
	};
}
