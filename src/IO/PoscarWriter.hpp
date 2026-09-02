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
		[[nodiscard]] static Result<void> Write(const CrystalStructure &structure, const Path &outputPath);
	};
} // namespace DefectStudio
