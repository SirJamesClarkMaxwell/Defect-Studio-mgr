#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/StructureComparisonSolver.hpp"

#include <vector>

#include "Core/Logging/Logger.hpp"

namespace DefectStudio
{
	Result<StructureComparisonResult> SolveStructureComparison(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		ScipyAssignmentBridge &assignmentBridge)
	{
		// Pure, cannot fail (see BuildLocalMatchingPlan doc comment) - only the batched Hungarian
		// solve below is a fallible subprocess call.
		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, elementPropertiesTable);

		std::vector<DisplacementCostMatrix> componentMatrices;
		componentMatrices.reserve(plan.assignableComponents.size());
		for (const LocalMatchingComponent &component : plan.assignableComponents)
			componentMatrices.push_back(component.costMatrix);

		Result<std::vector<std::vector<int>>> assignmentsResult = assignmentBridge.SolveAssignments(componentMatrices);
		if (!assignmentsResult)
			return assignmentsResult.Error();

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, assignmentsResult.Value());

		// 2026-08-28 spec: verify a large structure doesn't secretly become one global N x N
		// problem again - candidateEdgeCount/componentCount/largestComponentSize confirm the
		// matching stayed local, not just that a result came back.
		DS_LOG_INFO(
			"StructureComparison: reference={} comparison={} candidateEdges={} components={} "
			"largestComponent={} isolatedReference={} isolatedComparison={} -> matched={} vacancy={} "
			"interstitial={}",
			reference.atoms.size(), comparison.atoms.size(), plan.candidateEdgeCount,
			plan.assignableComponents.size(), plan.largestComponentSize, plan.isolatedReferenceIndices.size(),
			plan.isolatedComparisonIndices.size(), result.matches.size(), result.unmatchedReferenceAtomIndices.size(),
			result.unmatchedComparisonAtoms.size());

		return result;
	}
} // namespace DefectStudio
