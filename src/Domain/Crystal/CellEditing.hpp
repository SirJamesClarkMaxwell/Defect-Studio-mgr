#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/BravaisLattice.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	enum class CellEditPositions { KeepFractional, KeepCartesian };

	[[nodiscard]] Result<CrystalStructure> WithCell(const CrystalStructure &structure, const LatticeCell &cell,
		CellEditPositions positions);
	// Invalid parameters produce a non-finite cell, rejected by WithCell.
	[[nodiscard]] LatticeCell CellFromParameters(const LatticeCell &current, const LatticeParameters &parameters);
	[[nodiscard]] LatticeParameters ParametersOf(const LatticeCell &cell);
	[[nodiscard]] CrystalStructure WrapIntoCell(const CrystalStructure &structure, std::vector<std::size_t> &outCoincident);
	// task/112d: `species` on every lattice point of `centering` shifted by the fractional `offset` (wrapped into
	// [0,1)), appended to a copy. outCoincident: indices in the result of atoms sharing a site - reported, kept.
	[[nodiscard]] CrystalStructure AddSublattice(const CrystalStructure &structure, const std::string &species,
		BravaisCenteringPreset centering, const glm::vec3 &offset, std::vector<std::size_t> &outCoincident);
	// Periodic structure with this cell and no atoms, the start of a cell built from sublattices.
	[[nodiscard]] CrystalStructure EmptyCell(const LatticeCell &cell);
} // namespace DefectStudio
