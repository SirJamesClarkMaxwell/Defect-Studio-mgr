#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/VaspDensityGridBridge.hpp"

#include <nlohmann/json.hpp>

#include "Core/Logging/Logger.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	Result<DensityGrid> VaspDensityGridBridge::LoadDensityGrid(
		const Path &chgcarPath, const DensityComponent component, const Path &referencePath) const
	{
		if (chgcarPath.Empty())
		{
			return MakePythonExecutionError(
				"Density load request is incomplete.",
				"Expected a CHGCAR path.",
				"Pick a CHGCAR file before loading a density.",
				"python.vasp_density_grid.request_incomplete");
		}

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("vasp_density_grid_load.py");
		options.scriptPath = script.scriptPath;
		options.arguments = {chgcarPath.Utf8(), DensityComponentKey(component)};
		if (!referencePath.Empty())
			options.arguments.push_back(referencePath.Utf8());
		options.workingDirectory = script.workingDirectory;

		DS_LOG_DEBUG("VaspDensityGridBridge: loading {} of {} (reference: {})",
			DensityComponentKey(component), chgcarPath.Utf8(), referencePath.Empty() ? "none" : referencePath.Utf8());
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		if (!runResult)
		{
			const StructuredError &error = runResult.Error();
			if (error.technicalDetails.find("puntukas_not_installed") != std::string::npos)
			{
				return MakePythonUnavailableError(
					"puntukas is not installed.",
					error.technicalDetails,
					"Install it into the app's Python environment (uv pip install -e Vendor/puntukas_tools2).",
					"python.puntukas.not_installed");
			}
			return error;
		}

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Density loader returned no output.",
				"Expected JSON metadata in stdout but received an empty payload.\nstderr: " + runResult->standardError,
				"Verify scripts/python/examples/vasp_density_grid_load.py output contract.",
				"python.vasp_density_grid.empty_output");
		}

		Path gridPath;
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			DensityGrid density;
			OrbitalGridData &grid = density.grid;

			const std::vector<int> dims = payload.at("dims").get<std::vector<int>>();
			if (dims.size() != 3)
				throw std::runtime_error("Expected \"dims\" to have exactly 3 entries.");
			grid.dimensions = glm::ivec3(dims[0], dims[1], dims[2]);

			const auto cellRows = payload.at("cell").get<std::vector<std::vector<float>>>();
			if (cellRows.size() != 3 || cellRows[0].size() != 3 || cellRows[1].size() != 3 || cellRows[2].size() != 3)
				throw std::runtime_error("Expected \"cell\" to be 3x3.");
			for (int row = 0; row < 3; ++row)
				grid.cell[row] = glm::vec3(cellRows[row][0], cellRows[row][1], cellRows[row][2]);

			density.statistics.integral = payload.at("integral").get<float>();
			density.statistics.absIntegral = payload.at("absIntegral").get<float>();
			density.statistics.minimum = payload.at("min").get<float>();
			density.statistics.maximum = payload.at("max").get<float>();
			density.statistics.atomCount = payload.at("atomCount").get<int>();

			gridPath = Path(payload.at("gridPath").get<std::string>());
			const std::size_t expectedCount = static_cast<std::size_t>(grid.dimensions.x) *
				static_cast<std::size_t>(grid.dimensions.y) * static_cast<std::size_t>(grid.dimensions.z);
			Result<std::vector<float>> values = ReadFloat32GridFile(gridPath, expectedCount);
			FileSystem::Remove(gridPath);
			if (!values)
				return values.Error();
			grid.values = std::move(values).Value();
			return density;
		}
		catch (const std::exception &exception)
		{
			if (!gridPath.Empty())
				FileSystem::Remove(gridPath);
			return MakePythonExecutionError(
				"Density loader output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the loader prints exactly one JSON line with grid metadata.",
				"python.vasp_density_grid.invalid_json");
		}
	}
} // namespace DefectStudio
