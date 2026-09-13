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

		// Task 23. ONE subprocess per call: optional detection, frame alignment, character table,
		// reducible characters, decomposition, projected vectors and multiplets.
		//
		// An Undetermined detection is a SUCCESS with detection.determined == false and nothing else
		// filled. StructuredError codes:
		//   python.groupy.not_installed                   groupy / sympy import failed
		//   python.groupy.analysis.empty_basis            no sites
		//   python.groupy.analysis.unknown_point_group    manual label unknown to groupy
		//   python.groupy.analysis.frame_alignment_failed no rotation puts the sites in groupy's frame
		//   python.groupy.analysis.basis_not_closed       a rotated site hits no unique same-element site
		//   python.groupy.analysis.invalid_active_space   unknown active irrep or impossible electron count
		//   python.groupy.analysis.invalid_json           malformed script output
		[[nodiscard]] Result<PointGroupAnalysisResult> Analyze(const PointGroupAnalysisRequest &request) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
