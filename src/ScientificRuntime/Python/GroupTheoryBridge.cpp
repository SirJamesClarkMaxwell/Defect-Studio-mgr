#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/GroupTheoryBridge.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "Core/Utils/Uuid.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] Path MakeTempPayloadPath()
		{
			return Path(FileSystem::TempDirectoryPath()) /
				("ds_groupy_representation_" + ToString(GenerateUuid()) + ".json");
		}
	}

	Result<PointGroupReduction> GroupTheoryBridge::ReduceRepresentation(
		const PermutationRepresentationRequest &request) const
	{
		const Path payloadPath = MakeTempPayloadPath();
		nlohmann::json sites = nlohmann::json::array();
		for (const BasisSite &site : request.sites)
		{
			sites.push_back({
				{"label", site.label},
				{"position", {site.position.x, site.position.y, site.position.z}}});
		}
		nlohmann::json payload = {
			{"pointGroupLabel", request.pointGroupLabel},
			{"sites", std::move(sites)},
			{"matchTolerance", request.matchTolerance}};

		{
			std::ofstream stream(payloadPath.Native(), std::ios::binary | std::ios::trunc);
			if (!stream)
				return MakePythonExecutionError(
					"Could not write the group-theory request.",
					"Failed to open " + payloadPath.String() + " for writing.",
					"Verify the OS temp directory is writable.",
					"python.groupy.representation.temp_write_failed");
			stream << payload.dump();
		}

		const PythonExampleScript script = ResolvePythonExampleScript("groupy_reduce_representation.py");
		ScriptRunOptions options;
		options.scriptPath = script.scriptPath;
		options.arguments = {payloadPath.String()};
		options.workingDirectory = script.workingDirectory;
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);

		std::error_code removeError;
		FileSystem::Remove(payloadPath.Native(), removeError);
		if (!runResult)
		{
			const StructuredError &error = runResult.Error();
			if (error.technicalDetails.find("groupy_not_installed") != std::string::npos)
				return MakePythonUnavailableError(
					"groupy is not installed.",
					error.technicalDetails,
					"Reinstall the app's Python environment (groupy_symmetry is a declared dependency).",
					"python.groupy.not_installed");
			return error;
		}

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
			return MakePythonExecutionError(
				"Group-theory analysis returned no output.",
				"Expected one JSON payload in stdout but received an empty payload.\nStderr:\n" +
					runResult->standardError,
				"Verify scripts/python/examples/groupy_reduce_representation.py output contract.",
				"python.groupy.representation.empty_output");

		try
		{
			const nlohmann::json resultJson = nlohmann::json::parse(jsonLine);
			PointGroupReduction result;
			result.pointGroupLabel = resultJson.at("pointGroupLabel").get<std::string>();
			result.siteLabels = resultJson.at("siteLabels").get<std::vector<std::string>>();
			result.groupOrder = resultJson.at("groupOrder").get<int>();
			for (const auto &entry : resultJson.at("decomposition"))
				result.decomposition.push_back({entry.at("irrepLabel"), entry.at("multiplicity"), entry.at("dimension")});
			for (const auto &entry : resultJson.at("projectedVectors"))
			{
				SymmetryAdaptedVector vector;
				vector.irrepLabel = entry.at("irrepLabel").get<std::string>();
				vector.occurrenceIndex = entry.at("occurrenceIndex").get<int>();
				vector.irrepRow = entry.at("irrepRow").get<int>();
				for (const auto &coefficient : entry.at("coefficients"))
					vector.coefficients.push_back({coefficient.at("exact"), coefficient.at("numeric")});
				result.projectedVectors.push_back(std::move(vector));
			}
			return result;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Group-theory analysis output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the groupy bridge script prints exactly one valid JSON result.",
				"python.groupy.representation.invalid_json");
		}
	}
} // namespace DefectStudio
