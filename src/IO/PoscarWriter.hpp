#pragma once

#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	class PoscarWriter final
	{
	public:
		// Write CrystalStructure to POSCAR file via ase.io.write subprocess.
		// Returns success or StructuredError on failure (timeout, subprocess error, etc).
		//
		// `inputJsonPath` is the scratch file this hands to the Python script, and is required rather
		// than defaulted: it used to be one shared install/users/default/temp/poscar_input.json, which
		// two concurrent writes would overwrite under each other. Callers pass a path unique to their
		// attempt, and one that lives OUTSIDE any directory that will be committed - a failed cleanup
		// of this file must never be able to leak into a finished structure directory.
		//
		// Atoms are written sorted by species, then fractional x/y/z, so indices in the file can differ
		// from the in-memory order. `overwriteExisting` = false refuses an existing outputPath (new
		// structure directories); true atomically replaces it (Ctrl+S of an edited project structure).
		[[nodiscard]] static Result<void> Write(
			const CrystalStructure &structure,
			const Path &outputPath,
			const Path &inputJsonPath,
			bool overwriteExisting = false);
	};
} // namespace DefectStudio
