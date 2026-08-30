#pragma once

#include <vector>

#include "Domain/Crystal/StructureComparison.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// Solves the optimal (minimum total cost) one-to-one assignment between comparison-structure
	// atoms and reference-structure atoms via scipy.optimize.linear_sum_assignment. Subprocess-only
	// by design (no embedded/nanobind fast path), same justification as PuntukasBridge - this runs
	// once per user-triggered "Compare structures" action, not a hot loop.
	class ScipyAssignmentBridge final
	{
	public:
		// Solves every supplied matrix in ONE subprocess call (one JSON payload in, one JSON payload
		// out) instead of one call per matrix - local spatial matching (StructureComparison.hpp's
		// BuildLocalMatchingPlan) can produce many small independent components, and a Python cold
		// start (import scipy) per component would dominate over the actual solve. Result[i]
		// corresponds to costMatrices[i]: comparisonToReferenceAssignment[comparisonIndex] =
		// referenceIndex, or -1 for a comparison row scipy's rectangular assignment couldn't map to
		// any column (only possible when that matrix's referenceCount < comparisonCount). Feed
		// straight to BuildComparisonResultFromLocalPlan (StructureComparison.hpp).
		[[nodiscard]] Result<std::vector<std::vector<int>>> SolveAssignments(
			const std::vector<DisplacementCostMatrix> &costMatrices) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
