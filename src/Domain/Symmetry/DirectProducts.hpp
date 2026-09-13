#pragma once

#include <vector>

#include "Domain/Symmetry/PointGroupAnalysis.hpp"

namespace DefectStudio
{
	// Γ_i ⊗ Γ_j for every irrep pair of a character table, from the characters alone:
	// n_k = (1/h) Σ_c size_c · χ_i(c) · χ_j(c) · conj(χ_k(c)), evaluated on the numeric (complex)
	// characters and rounded. products[i][j] lists the irreps with multiplicity > 0, in table order,
	// with their dimensions. Empty for an empty table.
	[[nodiscard]] std::vector<std::vector<std::vector<IrrepMultiplicity>>> ComputeDirectProducts(const CharacterTable &table);
} // namespace DefectStudio
