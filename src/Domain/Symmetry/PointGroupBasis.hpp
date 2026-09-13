#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Symmetry/PointGroupAnalysis.hpp"

namespace DefectStudio
{
	// The group-theory panel's "Use selection as basis": exactly the selected atoms, no neighbour
	// shell. Periodic structures are unwrapped by minimum image around `centre`, so a defect that
	// straddles a cell face still yields a compact cluster; non-periodic structures use plain
	// Cartesian positions.
	struct SelectionBasis
	{
		std::vector<BasisSite> sites;         // label = species + atom index ("N12"), element = species,
		                                      // position = unwrapped Cartesian minus centre
		std::vector<std::size_t> atomIndices; // parallel to sites, in selection order
		glm::dvec3 centre{0.0};
		bool periodicUnwrapped = false;
		// Stable over atom indices, elements and centred positions (quantized to 1e-6 Å). The panel
		// compares it to decide whether a result is stale.
		std::uint64_t hash = 0;
	};

	// Errors (StructuredError, category Validation):
	//   "symmetry.basis.empty_selection"   - atomIndices empty
	//   "symmetry.basis.index_out_of_range" - any index >= structure.atoms.size()
	//   "symmetry.basis.singular_lattice"  - periodic structure whose cell matrix is not invertible
	[[nodiscard]] Result<SelectionBasis> BuildSelectionBasis(
		const CrystalStructure &structure, const std::vector<std::size_t> &atomIndices, const glm::dvec3 &centre);
} // namespace DefectStudio
