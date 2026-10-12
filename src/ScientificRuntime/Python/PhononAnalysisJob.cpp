#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	PhononAnalysisJob::PhononAnalysisJob(PhononAnalysisRequest request)
		: m_Request(std::move(request)) {}

	std::string PhononAnalysisJob::GetName() const { return "Phonon analysis"; }
	std::string PhononAnalysisJob::GetType() const { return "phonon"; }

	void PhononAnalysisJob::Execute(JobContext &context)
	{
		context.ThrowIfCancellationRequested();
		m_Result.reset();
		context.SetStage("phonon");
		context.SetMessage("Computing phonon modes and vibronic spectra via dephonopy");
		context.SetProgress(0.0f, 1.0f);

		auto result = m_Bridge.Run(m_Request, [&context] { return context.IsCancellationRequested(); });
		context.ThrowIfCancellationRequested();
		if (!result)
			throw std::runtime_error(result.Error().userMessage + "\n" + result.Error().technicalDetails);

		m_Result = std::move(result).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Phonon analysis completed");
	}

	std::optional<PhononAnalysisRunSummary> &PhononAnalysisJob::GetResult() noexcept { return m_Result; }
} // namespace DefectStudio
