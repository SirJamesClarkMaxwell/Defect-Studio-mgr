#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/VaspOrbitalGridBridge.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Time.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError MakePuntukasUnavailableError(const std::string &technicalDetails)
		{
			return MakePythonUnavailableError(
				"puntukas is not installed.",
				technicalDetails,
				R"(Install it into the app's Python environment: uv pip install -e "C:\Users\fzabi\punktukas-tools\puntukas_tools")",
				"python.puntukas.not_installed");
		}

		[[nodiscard]] Result<std::vector<float>> ReadGridFile(const Path &gridPath, std::size_t expectedCount)
		{
			std::ifstream file(gridPath.Native(), std::ios::binary);
			if (!file)
			{
				return MakePythonExecutionError(
					"Orbital grid file could not be opened.",
					"Path: " + gridPath.String(),
					"Verify scripts/python/examples/vasp_orbital_grid_load.py wrote the grid file before exiting.",
					"python.vasp_orbital_grid.file_missing");
			}

			std::vector<float> values(expectedCount);
			file.read(reinterpret_cast<char *>(values.data()),
				static_cast<std::streamsize>(expectedCount * sizeof(float)));
			if (!file)
			{
				return MakePythonExecutionError(
					"Orbital grid file was shorter than the reported dimensions.",
					"Path: " + gridPath.String() + ", expected " + std::to_string(expectedCount) + " float32 values.",
					"Verify the grid dimensions match the written file size.",
					"python.vasp_orbital_grid.truncated_file");
			}
			return values;
		}
	} // namespace

	Result<VaspOrbitalGridData> VaspOrbitalGridBridge::LoadOrbitalGrid(
		const Path &calculationDirectory,
		int spinChannel,
		int kpointIndex,
		int bandIndex) const
	{
		if (calculationDirectory.Empty())
		{
			return MakePythonExecutionError(
				"Orbital grid load request is incomplete.",
				"Expected calculationDirectory to be set.",
				"Provide a valid calculation directory (containing WAVECAR) before invoking the bridge.",
				"python.vasp_orbital_grid.request_incomplete");
		}

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("vasp_orbital_grid_load.py");
		options.scriptPath = script.scriptPath;
		options.arguments = {
			calculationDirectory.String(),
			std::to_string(spinChannel),
			std::to_string(kpointIndex),
			std::to_string(bandIndex)};
		options.workingDirectory = script.workingDirectory;

		const auto startTime = Time::NowSteady();
		DS_LOG_DEBUG("VaspOrbitalGridBridge: loading orbital grid from {} (spin={}, kpoint={}, band={})",
			calculationDirectory.String(), spinChannel, kpointIndex, bandIndex);
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		if (!runResult)
		{
			const StructuredError &error = runResult.Error();
			if (error.technicalDetails.find("puntukas_not_installed") != std::string::npos)
				return MakePuntukasUnavailableError(error.technicalDetails);
			return error;
		}

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Orbital grid loader returned no output.",
				"Expected JSON metadata in stdout but received an empty payload.",
				"Verify scripts/python/examples/vasp_orbital_grid_load.py output contract.",
				"python.vasp_orbital_grid.empty_output");
		}

		Path gridPath;
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			VaspOrbitalGridData data;

			const std::vector<int> dims = payload.at("dims").get<std::vector<int>>();
			if (dims.size() != 3)
				throw std::runtime_error("Expected \"dims\" to have exactly 3 entries.");
			data.dimensions = glm::ivec3(dims[0], dims[1], dims[2]);

			const std::vector<std::vector<float>> cellRows =
				payload.at("cell").get<std::vector<std::vector<float>>>();
			if (cellRows.size() != 3)
				throw std::runtime_error("Expected \"cell\" to have exactly 3 rows.");
			for (int row = 0; row < 3; ++row)
				data.cell[row] = glm::vec3(cellRows[row][0], cellRows[row][1], cellRows[row][2]);

			data.energy = payload.at("energy").get<float>();
			data.occupation = payload.at("occupation").get<float>();

			gridPath = Path(payload.at("gridPath").get<std::string>());
			const std::size_t expectedCount =
				static_cast<std::size_t>(data.dimensions.x) *
				static_cast<std::size_t>(data.dimensions.y) *
				static_cast<std::size_t>(data.dimensions.z);

			Result<std::vector<float>> gridResult = ReadGridFile(gridPath, expectedCount);
			FileSystem::Remove(gridPath);
			if (!gridResult)
				return gridResult.Error();
			data.values = std::move(gridResult).Value();

			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				Time::NowSteady() - startTime).count();
			DS_LOG_DEBUG("VaspOrbitalGridBridge: parsed {}x{}x{} orbital grid in {} ms",
				data.dimensions.x, data.dimensions.y, data.dimensions.z, elapsedMs);
			return data;
		}
		catch (const std::exception &exception)
		{
			if (!gridPath.Empty())
				FileSystem::Remove(gridPath);
			return MakePythonExecutionError(
				"Orbital grid loader output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with grid metadata.",
				"python.vasp_orbital_grid.invalid_json");
		}
	}
} // namespace DefectStudio
