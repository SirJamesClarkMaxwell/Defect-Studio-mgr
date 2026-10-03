#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Symmetry/PointGroupAnalysis.hpp"

namespace DefectStudio
{
	// task/54: the bridge's trust check on a reduction coming back from Python - the invariant
	// documented on SymmetryAdaptedVector and PointGroupReduction::realPairVectors. Errors
	// (StructuredError, category Validation, source "Domain/Symmetry/SymmetryAdaptedBasis"):
	//   "symmetry.salc.unknown_irrep"      - a projectedVectors entry's irrepLabel is not in
	//                                         decomposition
	//   "symmetry.salc.incomplete_copy"    - an irrep's projected vectors are not exactly
	//                                         occurrences 0..m-1 x rows 0..d-1 (missing or duplicate)
	//   "symmetry.salc.order"              - projectedVectors not irrep (decomposition order) ->
	//                                         occurrence -> row
	//   "symmetry.salc.coefficient_count"  - any vector (either list) whose coefficients.size()
	//                                         != siteLabels.size()
	//   "symmetry.salc.real_pair"          - a realPairVectors entry with an empty
	//                                         conjugateIrrepLabel, Γ or Γ* missing from decomposition,
	//                                         unequal multiplicities of Γ and Γ*, not exactly
	//                                         components {0, 1} per occurrence 0..m-1 of the pair, or
	//                                         any |numericImaginary| >= 1e-12
	// Checks run in the order listed; the first failure is returned. A reduction with empty
	// projectedVectors and realPairVectors is valid (nothing projected).
	[[nodiscard]] Result<void> ValidateSymmetryAdaptedBasis(const PointGroupReduction &reduction);
} // namespace DefectStudio
