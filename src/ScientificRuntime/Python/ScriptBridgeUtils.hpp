#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PymatgenBridge.hpp"

namespace DefectStudio
{
	std::string ExtractJsonLineFromOutput(const std::string &rawOutput);

	struct PythonExampleScript
	{
		Path scriptPath;
		Path workingDirectory;
	};

	// Walks up from the current working directory looking for scripts/python/examples/<fileName>.
	[[nodiscard]] PythonExampleScript ResolvePythonExampleScript(const char *fileName);

	// Reads the raw float32 grid a loader script wrote to a temp file (vasp_orbital_grid_load.py,
	// vasp_density_grid_load.py). Fails when the file is missing or shorter than expectedCount.
	// Does not delete the file - the caller owns that.
	[[nodiscard]] Result<std::vector<float>> ReadFloat32GridFile(const Path &gridPath, std::size_t expectedCount);

	// Parses a single {path, reduced_formula, lattice, sites:[...]} payload - the JSON contract shared
	// by pymatgen_structure_load.py and puntukas_structure_load.py. Throws on schema mismatch (caller
	// wraps in try/catch, same as every other bridge JSON parse in this codebase).
	[[nodiscard]] PymatgenStructureData ParseStructurePayloadJson(const nlohmann::json &payload);
} // namespace DefectStudio
