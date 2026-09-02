#pragma once

#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	class POTCARWriter final
	{
	public:
		// Write POTCAR file via ase.calculators.vasp subprocess.
		// pseudopotentialDir should point to a directory containing POTCAR files per element.
		// Returns success or StructuredError on failure (invalid pseudodir, subprocess error, etc).
		[[nodiscard]] static Result<void> Write(
			const CrystalStructure &structure,
			const Path &outputPath,
			const Path &pseudopotentialDir);
	};
} // namespace DefectStudio
