#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	class PoscarWriter final
	{
	public:
		// Write a VASP 5 POSCAR natively, with Direct coordinates and round-trip precision.
		// Atoms are written sorted by species, then fractional x/y/z, so indices in the file can differ
		// from the in-memory order. `overwriteExisting` = false refuses an existing outputPath (new
		// structure directories); true atomically replaces it (Ctrl+S of an edited project structure).
		[[nodiscard]] static Result<void> Write(
			const CrystalStructure &structure,
			const Path &outputPath,
			bool overwriteExisting = false);
	};
} // namespace DefectStudio
