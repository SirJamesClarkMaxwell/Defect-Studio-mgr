#pragma once

#include "Domain/Crystal/ElementProperties.hpp"
#include "Domain/Crystal/StructureComparison.hpp"
#include "ScientificRuntime/Python/ScipyAssignmentBridge.hpp"

namespace DefectStudio
{
	// Shared by CompareStructuresJob (displacement-arrow comparison against a file loaded off-disk)
	// and CopyWindowStateJob (matching two already-open windows' atoms to copy view+visibility
	// between them) - builds the cost matrix (Domain, pure), solves it via scipy (subprocess),
	// assembles the result (Domain, pure). The two jobs only differ in how `comparison` was obtained
	// before calling this - keeping the actual matching pipeline in one place.
	[[nodiscard]] Result<StructureComparisonResult> SolveStructureComparison(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		ScipyAssignmentBridge &assignmentBridge);
} // namespace DefectStudio
