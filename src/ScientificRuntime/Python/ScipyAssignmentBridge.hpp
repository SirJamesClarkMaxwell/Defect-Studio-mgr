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
		// Returns comparisonToReferenceAssignment[comparisonIndex] = referenceIndex, or -1 for a
		// comparison row scipy's rectangular assignment couldn't map to any column (only possible
		// when referenceCount < comparisonCount). Feed the result straight to
		// BuildComparisonResultFromAssignment (StructureComparison.hpp).
		[[nodiscard]] Result<std::vector<int>> SolveAssignment(const DisplacementCostMatrix &costMatrix) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
