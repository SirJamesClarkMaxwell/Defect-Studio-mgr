#pragma once

#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/Supercell.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct MillerIndices
	{
		int h = 1, k = 0, l = 0;
	};

	struct SymmetryInfo
	{
		int spacegroupNumber = 0;
		std::string spacegroupSymbol;
		std::string pointGroupSymbol;
		std::vector<std::string> wyckoffLetters; // one per atom, same order as CrystalStructure::atoms
	};

	// ASE/spglib-backed suggestions - both are read-only "tell me a matrix" / "tell me the
	// symmetry", never structure builders themselves. BuildSupercell (Domain/Crystal/Supercell.hpp)
	// is the only code path that actually replicates atoms - keeps exactly one implementation of
	// supercell expansion in the whole app instead of one in C++ and a second, subtly different one
	// in Python.
	class SupercellBridge final
	{
	public:
		[[nodiscard]] Result<SupercellMatrix> SuggestSurfaceOrientedMatrix(
			const CrystalStructure &unitCell,
			MillerIndices hkl,
			int layers) const;
		[[nodiscard]] Result<SymmetryInfo> GetSymmetryInfo(
			const CrystalStructure &structure,
			float symprecAngstrom) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
