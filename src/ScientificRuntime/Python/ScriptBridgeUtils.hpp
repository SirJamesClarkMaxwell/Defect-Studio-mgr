#pragma once

#include <string>

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

	// Parses a single {path, reduced_formula, lattice, sites:[...]} payload - the JSON contract shared
	// by pymatgen_structure_load.py and puntukas_structure_load.py. Throws on schema mismatch (caller
	// wraps in try/catch, same as every other bridge JSON parse in this codebase).
	[[nodiscard]] PymatgenStructureData ParseStructurePayloadJson(const nlohmann::json &payload);
} // namespace DefectStudio
