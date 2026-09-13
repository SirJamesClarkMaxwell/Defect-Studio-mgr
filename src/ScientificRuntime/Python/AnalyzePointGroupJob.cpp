#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/AnalyzePointGroupJob.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	AnalyzePointGroupJob::AnalyzePointGroupJob(PointGroupAnalysisRequest request)
		: m_Request(std::move(request))
	{
	}

	std::string AnalyzePointGroupJob::GetName() const
	{
		return "Analyze Point Group";
	}

	std::string AnalyzePointGroupJob::GetType() const
	{
		return "AnalyzePointGroupJob";
	}

	void AnalyzePointGroupJob::Execute(JobContext &context)
	{
		context.SetStage("point-group-analysis");
		context.SetMessage("Analyzing point group via groupy");
		context.SetProgress(0.0f, 1.0f);
		m_Result.reset();
		m_Error.reset();

		Result<PointGroupAnalysisResult> result = m_Bridge.Analyze(m_Request);
		if (!result)
		{
			m_Error = result.Error();
			throw std::runtime_error(result.Error().userMessage);
		}

		m_Result = std::move(result).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Point-group analysis complete");
	}

	const std::optional<PointGroupAnalysisResult> &AnalyzePointGroupJob::GetResult() const noexcept
	{
		return m_Result;
	}

	const std::optional<StructuredError> &AnalyzePointGroupJob::GetError() const noexcept
	{
		return m_Error;
	}
} // namespace DefectStudio
