#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/SupercellBridge.hpp"

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
		// files) - a plain temp file, removed after the subprocess reads it. Shared by
		// SuggestSurfaceOrientedMatrix now and GetSymmetryInfo (Task 4) later - both send a unit
		// cell to a Python script the same way.
		[[nodiscard]] Path MakeTempStructurePayloadPath()
		{
			return Path(FileSystem::TempDirectoryPath()) /
				("ds_supercell_bridge_" + ToString(GenerateUuid()) + ".json");
		}

		[[nodiscard]] Result<void> WriteStructurePayload(const Path &payloadPath, const CrystalStructure &structure)
		{
			nlohmann::json lattice = nlohmann::json::array();
			for (const glm::vec3 &vector : structure.cell.vectors)
				lattice.push_back({vector.x, vector.y, vector.z});

			nlohmann::json sites = nlohmann::json::array();
			for (const AtomSite &atom : structure.atoms)
				sites.push_back({{"element", atom.species}, {"fractional", {atom.fractional.x, atom.fractional.y, atom.fractional.z}}});

			nlohmann::json payload = {{"lattice", lattice}, {"sites", sites}};

			std::ofstream file(payloadPath.Native(), std::ios::binary | std::ios::trunc);
			if (!file)
			{
				return MakePythonExecutionError(
					"Could not write the unit cell to a temp file.",
					"Failed to open " + payloadPath.String() + " for writing.",
					"Verify the OS temp directory is writable.",
					"python.ase.surface_supercell.temp_write_failed");
			}
			file << payload.dump();
			return {};
		}
	} // namespace

	Result<SupercellMatrix> SupercellBridge::SuggestSurfaceOrientedMatrix(
		const CrystalStructure &unitCell,
		MillerIndices hkl,
		int layers) const
	{
		const Path payloadPath = MakeTempStructurePayloadPath();
		Result<void> writeResult = WriteStructurePayload(payloadPath, unitCell);
		if (!writeResult)
			return writeResult.Error();

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("ase_surface_supercell.py");
		options.scriptPath = script.scriptPath;
		options.arguments = {
			payloadPath.String(),
			std::to_string(hkl.h), std::to_string(hkl.k), std::to_string(hkl.l),
			std::to_string(layers)};
		options.workingDirectory = script.workingDirectory;

		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		std::error_code removeError;
		FileSystem::Remove(payloadPath.Native(), removeError);
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"ASE surface-supercell suggestion returned no output.",
				"Expected a JSON matrix in stdout but received an empty payload.",
				"Verify scripts/python/examples/ase_surface_supercell.py output contract.",
				"python.ase.surface_supercell.empty_output");
		}

		SupercellMatrix matrix;
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			const auto &rows = payload.at("matrix");
			for (int row = 0; row < 3; ++row)
				matrix.rows[row] = glm::ivec3(rows[row][0].get<int>(), rows[row][1].get<int>(), rows[row][2].get<int>());
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"ASE surface-supercell output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with a 3x3 integer matrix.",
				"python.ase.surface_supercell.invalid_json");
		}
		return matrix;
	}

	Result<SymmetryInfo> SupercellBridge::GetSymmetryInfo(
		const CrystalStructure &structure,
		float symprecAngstrom) const
	{
		const Path payloadPath = MakeTempStructurePayloadPath();
		Result<void> writeResult = WriteStructurePayload(payloadPath, structure);
		if (!writeResult)
			return writeResult.Error();

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("spglib_symmetry_info.py");
		options.scriptPath = script.scriptPath;
		options.arguments = { payloadPath.String(), std::to_string(symprecAngstrom) };
		options.workingDirectory = script.workingDirectory;

		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		std::error_code removeError;
		FileSystem::Remove(payloadPath.Native(), removeError);
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"spglib symmetry info returned no output.",
				"Expected a JSON payload in stdout but received nothing.",
				"Verify scripts/python/examples/spglib_symmetry_info.py output contract.",
				"python.spglib.symmetry_info.empty_output");
		}

		SymmetryInfo info;
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			info.spacegroupNumber = payload.at("spacegroup_number").get<int>();
			info.spacegroupSymbol = payload.at("spacegroup_symbol").get<std::string>();
			info.pointGroupSymbol = payload.at("point_group_symbol").get<std::string>();
			info.wyckoffLetters = payload.at("wyckoffs").get<std::vector<std::string>>();
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"spglib symmetry info parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints spacegroup/point-group/wyckoffs as one JSON line.",
				"python.spglib.symmetry_info.invalid_json");
		}
		return info;
	}
} // namespace DefectStudio
