#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "Domain/Crystal/BravaisLattice.hpp"
#include "Domain/Crystal/CrystalPrimitives.hpp"

namespace DefectStudio
{
	// A crystal structure is `lattice (x) basis`: the basis (motif) is the group of atoms attached
	// to ONE lattice point, and the centering says where the other lattice points of the
	// conventional cell are. Convolving the two is what produces the atoms that actually go to the
	// renderer and to POSCAR - diamond is the FCC lattice with a two-carbon motif at (0,0,0) and
	// (1/4,1/4,1/4), i.e. 2 basis rows, not 8 hand-typed ones.
	//
	// Only `species`, `fractional`, `label` and `index` are filled: the Cartesian `position` needs
	// the lattice matrix, which is the caller's (it built the LatticeCell). Fractional coordinates
	// are wrapped into [0,1). Result size is exactly `translations * basis.size()`, in basis-major
	// order (every translation of row 0, then of row 1, ...) - so a caller can map an expanded atom
	// back to the row it came from with `index / translationCount`.
	[[nodiscard]] std::vector<AtomSite> ExpandBasisOverLattice(
		std::span<const AtomSite> basis,
		BravaisCenteringPreset centering);

	// Indices of atoms that share a site with another atom, periodic images included (a basis row
	// at (1/2,1/2,0) under F centering lands on top of its own translated copy). Sorted, unique.
	//
	// Deliberately a REPORT, not a deduplication: the user may be mid-edit, and silently deleting
	// the atom they are typing is worse than showing them the collision.
	[[nodiscard]] std::vector<std::size_t> FindCoincidentAtomIndices(
		std::span<const AtomSite> atoms,
		float tolerance = 1e-4f);
} // namespace DefectStudio
