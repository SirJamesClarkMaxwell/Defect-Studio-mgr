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
		[[nodiscard]] Path MakeTempBatchPayloadPath()
		{
			return Path(FileSystem::TempDirectoryPath()) /
				("ds_displacement_cost_matrices_" + ToString(GenerateUuid()) + ".json");
		}
	} // namespace

	Result<std::vector<std::vector<int>>> ScipyAssignmentBridge::SolveAssignments(
		const std::vector<DisplacementCostMatrix> &costMatrices) const
	{
		if (costMatrices.empty())
			return std::vector<std::vector<int>>{};

		const Path payloadPath = MakeTempBatchPayloadPath();
		{
			nlohmann::json matricesJson = nlohmann::json::array();
			for (const DisplacementCostMatrix &costMatrix : costMatrices)
			{
				nlohmann::json matrixJson;
				matrixJson["comparison_count"] = costMatrix.comparisonCount;
				matrixJson["reference_count"] = costMatrix.referenceCount;
				matrixJson["costs"] = costMatrix.costs;
				matricesJson.push_back(std::move(matrixJson));
			}
			nlohmann::json payload;
			payload["matrices"] = std::move(matricesJson);

			std::ofstream stream(payloadPath.Native(), std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				return MakePythonExecutionError(
					"Could not write the displacement cost matrices to a temp file.",
					"Failed to open " + payloadPath.String() + " for writing.",
					"Verify the OS temp directory is writable.",
					"python.scipy.assignment.temp_write_failed");
			}
			stream << payload.dump();
		}

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("scipy_hungarian_assignment.py");
		options.scriptPath = script.scriptPath;
		options.arguments = {payloadPath.String()};
		options.workingDirectory = script.workingDirectory;

		DS_LOG_DEBUG("ScipyAssignmentBridge: solving {} local assignment component(s)", costMatrices.size());
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);

		std::error_code removeError;
		FileSystem::Remove(payloadPath.Native(), removeError);

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
				"Expected JSON with assignments in stdout but received an empty payload.",
				"Verify scripts/python/examples/scipy_hungarian_assignment.py output contract.",
				"python.scipy.assignment.empty_output");
		}

		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			std::vector<std::vector<int>> assignments = payload.at("assignments").get<std::vector<std::vector<int>>>();
			if (assignments.size() != costMatrices.size())
			{
				return MakePythonExecutionError(
					"Assignment solver returned the wrong number of results.",
					"Expected " + std::to_string(costMatrices.size()) + " assignment(s), got " +
						std::to_string(assignments.size()) + ".\nPayload: " + jsonLine,
					"Verify scripts/python/examples/scipy_hungarian_assignment.py output contract.",
					"python.scipy.assignment.batch_size_mismatch");
			}
			return assignments;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Assignment solver output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with assignments.",
				"python.scipy.assignment.invalid_json");
		}
	}
} // namespace DefectStudio
