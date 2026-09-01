#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Domain/Crystal/CrystalPrimitives.hpp"

namespace DefectStudio
{
	// 7 crystal systems constrain cell SHAPE (which of a/b/c/angles are independent). The 14
	// Bravais lattices differ only in lattice-point centering (P/I/F/C/R) within a system's cell
	// shape, not in the shape itself (e.g. Cubic P/I/F all have a=b=c, all angles 90 deg) - see
	// "Decyzja projektowa" in the plan this file implements
	// (docs/work/project/plans/2026-08-30-supercell-generation.md).
	enum class CrystalSystem
	{
		Cubic,
		Tetragonal,
		Orthorhombic,
		Hexagonal,
		Trigonal, // rhombohedral axes: a=b=c, alpha=beta=gamma != 90
		Monoclinic,
		Triclinic
	};

	struct LatticeParameters
	{
		float a = 1.0f, b = 1.0f, c = 1.0f; // Angstrom
		float alphaDegrees = 90.0f, betaDegrees = 90.0f, gammaDegrees = 90.0f;
	};

	// Which fields a UI should disable for a given system - does not affect BuildLatticeCell,
	// which always derives locked fields itself regardless of what the caller passed in (so it
	// stays a total function with no invalid-input error path).
	struct LatticeFieldConstraints
	{
		bool bLocked = false;
		bool cLocked = false;
		bool alphaLocked = false;
		bool betaLocked = false;
		bool gammaLocked = false;
		float lockedAngleDegrees = 90.0f; // meaningful only where *Locked is true and system != Trigonal
	};

	[[nodiscard]] LatticeFieldConstraints GetFieldConstraints(CrystalSystem system);

	// Builds the cell vectors for `system`, deriving every locked field (per
	// GetFieldConstraints) from the free ones instead of validating caller input - always succeeds
	// for finite a/b/c > 0. Convention: a along +X, b in the XY plane, c completes the set from the
	// angles (same convention pymatgen/ASE use for Lattice.from_parameters).
	[[nodiscard]] LatticeCell BuildLatticeCell(CrystalSystem system, const LatticeParameters &params);

	// Starting-point atomic basis for the classic centering types, as fractional coordinates in
	// the CONVENTIONAL cell - a convenience preset the New Structure wizard inserts before the
	// user edits further, not a constraint BuildLatticeCell enforces. Species/labels are left to
	// the caller - this only returns positions.
	enum class BravaisCenteringPreset
	{
		Primitive,    // 1 point: (0,0,0)
		BodyCentered, // 2 points: + (0.5,0.5,0.5)
		FaceCentered, // 4 points: (0,0,0) + 3 face centers
		BaseCentered  // 2 points: (0,0,0) + (0.5,0.5,0)
	};
	[[nodiscard]] std::vector<glm::vec3> GetCenteringPresetBasis(BravaisCenteringPreset preset);

	// Which centering presets the New Structure wizard should offer as a starting basis for a
	// given crystal system - a UI convenience gate, not a physical Bravais-lattice-count authority
	// (e.g. Cubic offers all four including base-centered, which doesn't preserve cubic symmetry
	// as a distinct Bravais class, but is still a valid convenience starting point per
	// GetCenteringPresetBasis's own doc comment above).
	[[nodiscard]] bool IsPresetSupportedFor(CrystalSystem system, BravaisCenteringPreset preset);
} // namespace DefectStudio
