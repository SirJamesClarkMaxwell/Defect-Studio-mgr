#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScipyAssignmentBridge.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Uuid.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError MakeScipyUnavailableError(const std::string &technicalDetails)
		{
			return MakePythonUnavailableError(
				"scipy is not installed.",
				technicalDetails,
				"Reinstall the app's Python environment (scipy is a declared scientific-core dependency).",
				"python.scipy.not_installed");
		}

		// Not AssetManager-governed (that guard is for the app's own bundled assets, not scratch
		// files) - a plain temp file, removed after the subprocess reads it.
		[[nodiscard]] Path MakeTempCostMatrixPath()
		{
			return Path(FileSystem::TempDirectoryPath()) /
				("ds_displacement_cost_matrix_" + ToString(GenerateUuid()) + ".json");
		}
	} // namespace

	Result<std::vector<int>> ScipyAssignmentBridge::SolveAssignment(const DisplacementCostMatrix &costMatrix) const
	{
		const Path matrixPath = MakeTempCostMatrixPath();
		{
			nlohmann::json payload;
			payload["comparison_count"] = costMatrix.comparisonCount;
			payload["reference_count"] = costMatrix.referenceCount;
			payload["costs"] = costMatrix.costs;

			std::ofstream stream(matrixPath.Native(), std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				return MakePythonExecutionError(
					"Could not write the displacement cost matrix to a temp file.",
					"Failed to open " + matrixPath.String() + " for writing.",
					"Verify the OS temp directory is writable.",
					"python.scipy.assignment.temp_write_failed");
			}
			stream << payload.dump();
		}

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("scipy_hungarian_assignment.py");
		options.scriptPath = script.scriptPath;
		options.arguments = {matrixPath.String()};
		options.workingDirectory = script.workingDirectory;

		DS_LOG_DEBUG(
			"ScipyAssignmentBridge: solving {}x{} assignment",
			costMatrix.comparisonCount,
			costMatrix.referenceCount);
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);

		std::error_code removeError;
		FileSystem::Remove(matrixPath.Native(), removeError);

		if (!runResult)
		{
			const StructuredError &error = runResult.Error();
			if (error.technicalDetails.find("scipy_not_installed") != std::string::npos)
				return MakeScipyUnavailableError(error.technicalDetails);
			return error;
		}

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Assignment solver returned no output.",
				"Expected JSON with comparison_to_reference in stdout but received an empty payload.",
				"Verify scripts/python/examples/scipy_hungarian_assignment.py output contract.",
				"python.scipy.assignment.empty_output");
		}

		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			return payload.at("comparison_to_reference").get<std::vector<int>>();
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Assignment solver output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with comparison_to_reference.",
				"python.scipy.assignment.invalid_json");
		}
	}
} // namespace DefectStudio
