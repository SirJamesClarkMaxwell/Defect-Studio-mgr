#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/CopyWindowStateJob.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"
#include "ScientificRuntime/Python/StructureComparisonSolver.hpp"

namespace DefectStudio
{
	CopyWindowStateJob::CopyWindowStateJob(
		CrystalStructure sourceStructure,
		CrystalStructure targetStructure,
		ElementPropertiesTable elementPropertiesTable)
		: m_SourceStructure(std::move(sourceStructure)),
		  m_TargetStructure(std::move(targetStructure)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable))
	{
	}

	std::string CopyWindowStateJob::GetName() const
	{
		return "Copy View + Visibility";
	}

	std::string CopyWindowStateJob::GetType() const
	{
		return "CopyWindowStateJob";
	}

	void CopyWindowStateJob::Execute(JobContext &context)
	{
		context.SetStage("copy-window-state");
		context.SetMessage("Matching atoms (cost matrix + scipy assignment)");
		context.SetProgress(0.0f, 1.0f);

		Result<StructureComparisonResult> comparisonResult = SolveStructureComparison(
			m_SourceStructure, m_TargetStructure, m_ElementPropertiesTable, m_AssignmentBridge);
		if (!comparisonResult)
			throw std::runtime_error(comparisonResult.Error().technicalDetails);

		m_Result = std::move(comparisonResult).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Match complete");
	}

	const std::optional<StructureComparisonResult> &CopyWindowStateJob::GetResult() const noexcept
	{
		return m_Result;
	}
} // namespace DefectStudio
