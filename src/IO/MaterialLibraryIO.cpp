#include "Core/dspch.hpp"

#include "IO/MaterialLibraryIO.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "Core/Utils/Uuid.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	namespace
	{
		// Not AssetManager-governed (that guard is for the app's own bundled assets, not scratch
		// files) - a plain temp file, removed after the subprocess reads it. Only AddMaterial needs
		// one; list/load/remove pass everything they need as plain command-line arguments.
		[[nodiscard]] Path MakeTempMaterialPayloadPath()
		{
			return Path(FileSystem::TempDirectoryPath()) /
				("ds_material_library_add_" + ToString(GenerateUuid()) + ".json");
		}

		[[nodiscard]] Result<void> WriteMaterialPayload(
			const Path &payloadPath,
			const CrystalStructure &structure,
			const std::string &name,
			const std::string &notes)
		{
			nlohmann::json lattice = nlohmann::json::array();
			for (const glm::vec3 &vector : structure.cell.vectors)
				lattice.push_back({vector.x, vector.y, vector.z});

			nlohmann::json sites = nlohmann::json::array();
			for (const AtomSite &atom : structure.atoms)
				sites.push_back({{"element", atom.species}, {"fractional", {atom.fractional.x, atom.fractional.y, atom.fractional.z}}});

			nlohmann::json payload = {{"lattice", lattice}, {"sites", sites}, {"name", name}, {"notes", notes}};

			std::ofstream stream(payloadPath.Native(), std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				return MakePythonExecutionError(
					"Could not write the material to a temp file.",
					"Failed to open " + payloadPath.String() + " for writing.",
					"Verify the OS temp directory is writable.",
					"python.ase.material_library.add.temp_write_failed");
			}
			stream << payload.dump();
			return {};
		}

		[[nodiscard]] ScriptRunOptions MakeRunOptions(const std::vector<std::string> &arguments)
		{
			ScriptRunOptions options;
			const PythonExampleScript script = ResolvePythonExampleScript("ase_material_library.py");
			options.scriptPath = script.scriptPath;
			options.arguments = arguments;
			options.workingDirectory = script.workingDirectory;
			return options;
		}
	} // namespace

	MaterialLibraryIO::MaterialLibraryIO(Path libraryPath)
		: m_LibraryPath(std::move(libraryPath))
	{
		// ase.db (sqlite3) creates the .db FILE lazily on first write, but it does not create
		// missing parent directories - the project-scoped default ("materials/materials.db") lives
		// in a subdirectory a fresh project never had a reason to create. Done once here (not just
		// in AddMaterial) so ListMaterials/LoadMaterial/RemoveMaterial also work against a
		// never-touched library path instead of failing with "unable to open database file".
		// Idempotent and cheap - unconditional on every construction, no need to check existence
		// first.
		std::error_code directoryError;
		FileSystem::CreateDirectories(m_LibraryPath.parent_path().Native(), directoryError);
	}

	Result<MaterialLibraryEntry> MaterialLibraryIO::AddMaterial(
		const CrystalStructure &structure,
		const std::string &name,
		const std::string &notes) const
	{
		const Path payloadPath = MakeTempMaterialPayloadPath();
		Result<void> writeResult = WriteMaterialPayload(payloadPath, structure, name, notes);
		if (!writeResult)
			return writeResult.Error();

		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(
			MakeRunOptions({m_LibraryPath.String(), "add", payloadPath.String()}));

		std::error_code removeError;
		FileSystem::Remove(payloadPath.Native(), removeError);
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Adding the material to the library returned no output.",
				"Expected a JSON entry in stdout but received an empty payload.",
				"Verify scripts/python/examples/ase_material_library.py output contract.",
				"python.ase.material_library.add.empty_output");
		}

		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			MaterialLibraryEntry entry;
			entry.id = payload.at("id").get<std::string>();
			entry.name = payload.value("name", std::string{});
			entry.reducedFormula = payload.value("reduced_formula", std::string{});
			entry.notes = payload.value("notes", std::string{});
			return entry;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Material-library add output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with the new entry.",
				"python.ase.material_library.add.invalid_json");
		}
	}

	Result<std::vector<MaterialLibraryEntry>> MaterialLibraryIO::ListMaterials() const
	{
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(
			MakeRunOptions({m_LibraryPath.String(), "list"}));
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Listing the material library returned no output.",
				"Expected a JSON entry list in stdout but received an empty payload.",
				"Verify scripts/python/examples/ase_material_library.py output contract.",
				"python.ase.material_library.list.empty_output");
		}

		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			std::vector<MaterialLibraryEntry> entries;
			for (const nlohmann::json &item : payload.at("entries"))
			{
				MaterialLibraryEntry entry;
				entry.id = item.at("id").get<std::string>();
				entry.name = item.value("name", std::string{});
				entry.reducedFormula = item.value("reduced_formula", std::string{});
				entry.notes = item.value("notes", std::string{});
				entries.push_back(std::move(entry));
			}
			return entries;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Material-library list output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with the entry list.",
				"python.ase.material_library.list.invalid_json");
		}
	}

	Result<CrystalStructure> MaterialLibraryIO::LoadMaterial(const std::string &entryId) const
	{
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(
			MakeRunOptions({m_LibraryPath.String(), "load", entryId}));
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Loading the material from the library returned no output.",
				"Expected a JSON structure in stdout but received an empty payload.",
				"Verify scripts/python/examples/ase_material_library.py output contract.",
				"python.ase.material_library.load.empty_output");
		}

		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			CrystalStructure structure;
			const nlohmann::json &lattice = payload.at("lattice");
			for (int row = 0; row < 3; ++row)
			{
				structure.cell.vectors[row] = glm::vec3(
					lattice.at(row).at(0).get<float>(),
					lattice.at(row).at(1).get<float>(),
					lattice.at(row).at(2).get<float>());
			}
			const glm::mat3 latticeMatrix = structure.cell.ToMatrix();

			const nlohmann::json &sites = payload.at("sites");
			structure.atoms.reserve(sites.size());
			int index = 0;
			for (const nlohmann::json &site : sites)
			{
				const nlohmann::json &fractional = site.at("fractional");
				// Wrap into the primary [0,1) cell before storing - same reasoning as
				// PymatgenConversion::ConvertPymatgenStructureToCrystalStructure: a stored structure
				// is not guaranteed to already be wrapped.
				glm::vec3 wrappedFractional(
					fractional.at(0).get<float>(),
					fractional.at(1).get<float>(),
					fractional.at(2).get<float>());
				wrappedFractional -= glm::floor(wrappedFractional);

				AtomSite atom;
				atom.species = site.at("element").get<std::string>();
				atom.fractional = wrappedFractional;
				atom.position = latticeMatrix * wrappedFractional;
				atom.index = index++;
				structure.atoms.push_back(std::move(atom));
			}
			return structure;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Material-library load output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with lattice/sites.",
				"python.ase.material_library.load.invalid_json");
		}
	}

	Result<void> MaterialLibraryIO::RemoveMaterial(const std::string &entryId) const
	{
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(
			MakeRunOptions({m_LibraryPath.String(), "remove", entryId}));
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Removing the material from the library returned no output.",
				"Expected a JSON acknowledgement in stdout but received an empty payload.",
				"Verify scripts/python/examples/ase_material_library.py output contract.",
				"python.ase.material_library.remove.empty_output");
		}

		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			const std::string removedId = payload.at("removed_id").get<std::string>();
			if (removedId != entryId)
			{
				return MakePythonExecutionError(
					"Material-library remove acknowledged the wrong entry.",
					"Requested id " + entryId + " but the bridge script acknowledged " + removedId + ".\nPayload: " + jsonLine,
					"Ensure the bridge script prints removed_id matching the requested entry.",
					"python.ase.material_library.remove.id_mismatch");
			}
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Material-library remove output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with removed_id.",
				"python.ase.material_library.remove.invalid_json");
		}
		return {};
	}
} // namespace DefectStudio
