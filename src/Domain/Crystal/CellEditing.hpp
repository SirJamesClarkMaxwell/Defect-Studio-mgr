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
} // namespace DefectStudio
