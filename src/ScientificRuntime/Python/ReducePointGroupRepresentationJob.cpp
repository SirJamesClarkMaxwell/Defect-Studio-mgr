#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ReducePointGroupRepresentationJob.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	ReducePointGroupRepresentationJob::ReducePointGroupRepresentationJob(
		PermutationRepresentationRequest request)
		: m_Request(std::move(request))
	{
	}

	std::string ReducePointGroupRepresentationJob::GetName() const
	{
		return "Reduce Point-Group Representation: " + m_Request.pointGroupLabel;
	}

	std::string ReducePointGroupRepresentationJob::GetType() const
	{
		return "ReducePointGroupRepresentationJob";
	}

	void ReducePointGroupRepresentationJob::Execute(JobContext &context)
	{
		context.SetStage("point-group-reduction");
		context.SetMessage("Reducing permutation representation via groupy");
		context.SetProgress(0.0f, 1.0f);

		Result<PointGroupReduction> result = m_Bridge.ReduceRepresentation(m_Request);
		if (!result)
			throw std::runtime_error(result.Error().technicalDetails);

		m_Result = std::move(result).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Point-group reduction complete");
	}

	const std::optional<PointGroupReduction> &ReducePointGroupRepresentationJob::GetResult() const noexcept
	{
		return m_Result;
	}
} // namespace DefectStudio
