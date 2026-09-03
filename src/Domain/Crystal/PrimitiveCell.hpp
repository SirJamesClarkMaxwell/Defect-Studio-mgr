#pragma once

#include <optional>

#include <glm/glm.hpp>

#include "Domain/Crystal/BravaisLattice.hpp"

namespace DefectStudio
{
	// The primitive cell of a CENTRED conventional cell, as three lattice vectors (rows, matching
	// LatticeCell::ToMatrix). Returns nullopt for Primitive centering, where the conventional cell
	// already IS the primitive cell and an overlay would just redraw the same box.
	//
	// This is a textbook transformation of the lattice VECTORS, not of the structure: the atoms do
	// not move, and nothing here reduces a hand-built basis to its own primitive cell. That
	// direction needs spglib, which lives behind a Python subprocess - so it is a job, not a
	// checkbox, and the two must not be confused: the overlay only means "here is the smaller cell
	// this centring implies", which is exactly true whenever the centring is a chosen input rather
	// than something inferred from the atoms.
	[[nodiscard]] std::optional<glm::mat3> PrimitiveCellVectors(
		const glm::mat3 &conventionalLattice,
		BravaisCenteringPreset centering);

	// Maps prototypes.yaml's centering spelling ("Face-centered", ...) onto the enum. Returns
	// nullopt for an unrecognised string rather than guessing Primitive, which would silently hide
	// the overlay for a typo'd prototype.
	[[nodiscard]] std::optional<BravaisCenteringPreset> ParseCenteringName(const std::string &name);
} // namespace DefectStudio
