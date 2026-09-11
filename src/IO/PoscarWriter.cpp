#include "Core/dspch.hpp"

#include "IO/PoscarWriter.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	Result<void> PoscarWriter::Write(
		const CrystalStructure &structure,
		const Path &outputPath,
		const Path &inputJsonPath)
	{
		// Sort atoms by species (groups same elements together) for POSCAR output.
		// Defect-pattern key sorting (prototype + defect SET) deferred to when DefectConfiguration
		// is actively linked to structures; for now, group by species + position.
		std::vector<std::size_t> atomIndices;
		for (std::size_t i = 0; i < structure.atoms.size(); ++i)
			atomIndices.push_back(i);

		std::sort(atomIndices.begin(), atomIndices.end(), [&](std::size_t a, std::size_t b) {
			const auto &specA = structure.atoms[a].species;
			const auto &specB = structure.atoms[b].species;
			if (specA != specB)
				return specA < specB; // Sort by species alphabetically
			// Same species: sort by fractional position (x, y, z)
			const auto &posA = structure.atoms[a].fractional;
			const auto &posB = structure.atoms[b].fractional;
			if (posA.x != posB.x)
				return posA.x < posB.x;
			if (posA.y != posB.y)
				return posA.y < posB.y;
			return posA.z < posB.z;
		});

		// nlohmann::json, not hand-rolled string building - a Windows output path is full of
		// backslashes, and pasting it raw into a JSON string produced invalid escapes ("\U", "\N"),
		// which made every single save fail in json.load on the Python side.
		nlohmann::json species = nlohmann::json::array();
		nlohmann::json positions = nlohmann::json::array();
		for (const std::size_t index : atomIndices)
		{
			const AtomSite &atom = structure.atoms[index];
			species.push_back(atom.species);
			positions.push_back({atom.fractional.x, atom.fractional.y, atom.fractional.z});
		}

		nlohmann::json cell = nlohmann::json::array();
		for (const glm::vec3 &vector : structure.cell.vectors)
			cell.push_back({vector.x, vector.y, vector.z});

		const nlohmann::json payload = {
			{"output_path", outputPath.String()},
			{"species", std::move(species)},
			{"positions", std::move(positions)},
			{"cell", std::move(cell)},
			{"pbc", {true, true, true}}};

		// Write JSON to the caller-supplied scratch file (unique per attempt - see the header note).
		const std::string jsonStr = payload.dump();
		const Path jsonPath = inputJsonPath;
		if (!jsonPath.Native().parent_path().empty())
			FileSystem::CreateDirectories(jsonPath.Native().parent_path());

		std::ofstream jsonFile(jsonPath.String());
		if (!jsonFile)
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"Failed to write POSCAR input file",
				"Could not open: " + jsonPath.String(),
				"Check disk permissions");

		jsonFile << jsonStr;
		jsonFile.close();

		// Invoke Python script with JSON file as argument
		ScriptRunner runner;
		ScriptRunOptions options;
		options.scriptPath =
			Path::FromResolved(FileSystem::CurrentPath() / "install" / "users" / "default" / "scripts" / "write_poscar.py");
		options.arguments.push_back(jsonPath.String());
		options.workingDirectory = Path::FromResolved(FileSystem::CurrentPath());
		options.timeout = std::chrono::milliseconds(5000);
		options.requireZeroExitCode = true;

		auto result = runner.RunFile(options);
		FileSystem::Remove(jsonPath); // Clean up temp file

		if (!result.HasValue())
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"POSCAR write failed",
				result.Error().technicalDetails,
				result.Error().suggestion);

		const auto &scriptResult = result.Value();
		if (scriptResult.timedOut)
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"POSCAR write timed out",
				"Python subprocess exceeded 5 second limit",
				"Check if ase.io.write is slow on this structure");

		if (scriptResult.exitCode != 0)
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"POSCAR write subprocess error",
				scriptResult.standardError,
				"Verify ase is installed and structure is valid");

		return Result<void>{};
	}
} // namespace DefectStudio
