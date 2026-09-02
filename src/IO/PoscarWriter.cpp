#include "Core/dspch.hpp"

#include "IO/PoscarWriter.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	Result<void> PoscarWriter::Write(const CrystalStructure &structure, const Path &outputPath)
	{
		// Build JSON input for Python script: species, positions, cell
		std::ostringstream json;
		json << "{\n";
		json << "  \"output_path\": \"" << outputPath.string() << "\",\n";

		// Species list (element symbols)
		json << "  \"species\": [";
		for (std::size_t i = 0; i < structure.atoms.size(); ++i)
		{
			if (i > 0)
				json << ", ";
			json << "\"" << structure.atoms[i].species << "\"";
		}
		json << "],\n";

		// Positions (fractional coordinates)
		json << "  \"positions\": [";
		for (std::size_t i = 0; i < structure.atoms.size(); ++i)
		{
			if (i > 0)
				json << ", ";
			const auto &pos = structure.atoms[i].fractional;
			json << "[" << pos.x << ", " << pos.y << ", " << pos.z << "]";
		}
		json << "],\n";

		// Cell vectors (Angstrom)
		json << "  \"cell\": [\n";
		for (int i = 0; i < 3; ++i)
		{
			const auto &vec = structure.cell.vectors[i];
			json << "    [" << vec.x << ", " << vec.y << ", " << vec.z << "]";
			if (i < 2)
				json << ",";
			json << "\n";
		}
		json << "  ],\n";

		// PBC (periodic boundary conditions)
		json << "  \"pbc\": [true, true, true]\n";
		json << "}\n";

		// Write JSON to temp file
		const std::string jsonStr = json.str();
		const Path tempDir = Path::FromResolved(FileSystem::CurrentPath() / "install" / "users" / "default" / "temp");
		FileSystem::CreateDirectories(tempDir);
		const Path jsonPath = tempDir / "poscar_input.json";

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
