#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/FnvCorrectionBridge.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	FnvCorrectionJob::FnvCorrectionJob(FnvProfileRequest request)
		: m_Request(std::move(request)) {}

	std::string FnvCorrectionJob::GetName() const { return "FNV correction"; }
	std::string FnvCorrectionJob::GetType() const { return "fnv"; }

	void FnvCorrectionJob::Execute(JobContext &context)
	{
		context.ThrowIfCancellationRequested();
		m_Result.reset();
		context.SetStage("fnv");
		context.SetMessage("Reading LOCPOTs and computing FNV profiles via pymatgen");
		context.SetProgress(0.0f, 1.0f);

		Result<FnvProfiles> result = m_Bridge.ComputeProfiles(m_Request);
		if (!result)
			throw std::runtime_error(result.Error().userMessage + "\n" + result.Error().technicalDetails);

		context.ThrowIfCancellationRequested();
		m_Result = std::move(result).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("FNV profiles computed");
	}

	std::optional<FnvProfiles> &FnvCorrectionJob::GetResult() noexcept { return m_Result; }
} // namespace DefectStudio
