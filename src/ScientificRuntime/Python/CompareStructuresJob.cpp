#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/CompareStructuresJob.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"
#include "ScientificRuntime/Python/PymatgenConversion.hpp"
#include "ScientificRuntime/Python/StructureComparisonSolver.hpp"

namespace DefectStudio
{
	CompareStructuresJob::CompareStructuresJob(
		CrystalStructure referenceStructure,
		Path comparisonFilePath,
		ElementPropertiesTable elementPropertiesTable)
		: m_ReferenceStructure(std::move(referenceStructure)),
		  m_ComparisonFilePath(std::move(comparisonFilePath)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable))
	{
	}

	std::string CompareStructuresJob::GetName() const
	{
		return "Compare Structures: " + m_ComparisonFilePath.filename().string();
	}

	std::string CompareStructuresJob::GetType() const
	{
		return "CompareStructuresJob";
	}

	void CompareStructuresJob::Execute(JobContext &context)
	{
		context.SetStage("compare-structures");
		context.SetMessage("Loading comparison structure via puntukas");
		context.SetProgress(0.0f, 3.0f);

		Result<PymatgenStructureData> loadResult = m_StructureBridge.LoadStructure(m_ComparisonFilePath);
		if (!loadResult)
			throw std::runtime_error(loadResult.Error().technicalDetails);
		const CrystalStructure comparisonStructure = ConvertPymatgenStructureToCrystalStructure(loadResult.Value());
		context.SetProgress(1.0f, 3.0f);

		context.SetMessage("Matching atoms (cost matrix + scipy assignment)");
		Result<StructureComparisonResult> comparisonResult = SolveStructureComparison(
			m_ReferenceStructure, comparisonStructure, m_ElementPropertiesTable, m_AssignmentBridge);
		if (!comparisonResult)
			throw std::runtime_error(comparisonResult.Error().technicalDetails);

		m_Result = std::move(comparisonResult).Value();
		context.SetProgress(3.0f, 3.0f);
		context.SetMessage("Comparison complete");
	}

	const Path &CompareStructuresJob::GetComparisonFilePath() const noexcept
	{
		return m_ComparisonFilePath;
	}

	const std::optional<StructureComparisonResult> &CompareStructuresJob::GetResult() const noexcept
	{
		return m_Result;
	}
} // namespace DefectStudio
