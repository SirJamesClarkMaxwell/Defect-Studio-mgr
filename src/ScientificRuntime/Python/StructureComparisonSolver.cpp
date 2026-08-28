#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/StructureComparisonSolver.hpp"

namespace DefectStudio
{
	Result<StructureComparisonResult> SolveStructureComparison(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		ScipyAssignmentBridge &assignmentBridge)
	{
		Result<DisplacementCostMatrix> costMatrixResult =
			BuildDisplacementCostMatrix(reference, comparison, elementPropertiesTable);
		if (!costMatrixResult)
			return costMatrixResult.Error();

		Result<std::vector<int>> assignmentResult = assignmentBridge.SolveAssignment(costMatrixResult.Value());
		if (!assignmentResult)
			return assignmentResult.Error();

		return BuildComparisonResultFromAssignment(
			reference, comparison, costMatrixResult.Value(), assignmentResult.Value());
	}
} // namespace DefectStudio
