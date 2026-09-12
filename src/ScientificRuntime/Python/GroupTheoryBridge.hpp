#pragma once

#include "Domain/Symmetry/PointGroupAnalysis.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// Reduces a permutation representation into irreducible representations and projects the
	// symmetry-adapted basis, via groupy on its SymPy backend. Subprocess-only by design, same
	// justification as PuntukasBridge and ScipyAssignmentBridge - DS_PYTHON_CAPI_AVAILABLE=0 in this
	// build, and this runs once per user-triggered analysis, not in a loop.
	class GroupTheoryBridge final
	{
	public:
		// ONE call returns both the decomposition and every projected vector. Splitting those apart
		// would pay `import groupy` once per irrep instead of once per request - measured on this
		// machine at 680 ms cold, against 64 ms for bare Python. Same reason ScipyAssignmentBridge
		// solves every matrix in a single call.
		//
		// Returns a StructuredError - never an exception, never a crash - when: the point-group
		// label is unknown to groupy, the site list is empty, a site rotated by a group element
		// matches no site within request.matchTolerance (the basis is not closed under the group),
		// or groupy/SymPy cannot be imported.
		[[nodiscard]] Result<PointGroupReduction> ReduceRepresentation(
			const PermutationRepresentationRequest &request) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
