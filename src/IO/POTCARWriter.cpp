#include "Core/dspch.hpp"

#include "IO/POTCARWriter.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <set>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	Result<void> POTCARWriter::Write(
		const CrystalStructure &structure,
		const Path &outputPath,
		const Path &pseudopotentialDir)
	{
		// Validate pseudopotentialDir exists
		if (pseudopotentialDir.Empty() || !FileSystem::Exists(pseudopotentialDir.Native()))
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"POTCAR export failed: pseudopotential directory not configured or missing",
				"pseudodir: " + pseudopotentialDir.String(),
				"Configure pseudopotential directory in Settings > UI > Pseudopotential Dir");

		// Collect unique species in structure
		std::set<std::string> uniqueSpecies;
		for (const auto &atom : structure.atoms)
			uniqueSpecies.insert(atom.species);

		// Build JSON input for Python script: species list, pseudodir, output path
		std::ostringstream json;
		json << "{\n";
		json << "  \"output_path\": \"" << outputPath.string() << "\",\n";
		json << "  \"pseudopotential_dir\": \"" << pseudopotentialDir.string() << "\",\n";

		// Species list (unique elements)
		json << "  \"species\": [";
		bool first = true;
		for (const auto &species : uniqueSpecies)
		{
			if (!first)
				json << ", ";
			json << "\"" << species << "\"";
			first = false;
		}
		json << "]\n";
		json << "}\n";

		// Write JSON to temp file
		const std::string jsonStr = json.str();
		const Path tempDir = Path::FromResolved(FileSystem::CurrentPath() / "install" / "users" / "default" / "temp");
		FileSystem::CreateDirectories(tempDir);
		const Path jsonPath = tempDir / "potcar_input.json";

		std::ofstream jsonFile(jsonPath.String());
		if (!jsonFile)
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"Failed to write POTCAR input file",
				"Could not open: " + jsonPath.String(),
				"Check disk permissions");

		jsonFile << jsonStr;
		jsonFile.close();

		// Invoke Python script with JSON file as argument
		ScriptRunner runner;
		ScriptRunOptions options;
		options.scriptPath =
			Path::FromResolved(FileSystem::CurrentPath() / "install" / "users" / "default" / "scripts" / "write_potcar.py");
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
				"POTCAR write failed",
				result.Error().technicalDetails,
				result.Error().suggestion);

		const auto &scriptResult = result.Value();
		if (scriptResult.timedOut)
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"POTCAR write timed out",
				"Python subprocess exceeded 5 second limit",
				"Check if POTCAR concatenation is slow");

		if (scriptResult.exitCode != 0)
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"POTCAR write subprocess error",
				scriptResult.standardError,
				"Verify pseudopotentials exist in directory for all species");

		return Result<void>{};
	}
} // namespace DefectStudio
