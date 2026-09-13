#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/GroupTheoryBridge.hpp"

#include <array>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

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

		[[nodiscard]] std::optional<std::string> ReadAnalysisErrorCode(const std::string &details)
		{
			std::istringstream input(details);
			std::vector<std::string> lines;
			for (std::string line; std::getline(input, line);)
				lines.push_back(std::move(line));
			for (auto line = lines.rbegin(); line != lines.rend(); ++line)
			{
				try
				{
					const nlohmann::json payload = nlohmann::json::parse(*line);
					if (payload.is_object() && payload.contains("error") && payload.at("error").is_string())
						return payload.at("error").get<std::string>();
				}
				catch (const nlohmann::json::exception &)
				{
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] StructuredError MakeAnalysisScriptError(const StructuredError &error)
		{
			const std::optional<std::string> scriptCode = ReadAnalysisErrorCode(error.technicalDetails);
			if (!scriptCode)
				return error;
			if (*scriptCode == "groupy_not_installed")
				return MakePythonUnavailableError(
					"groupy is not installed.", error.technicalDetails,
					"Install groupy into .venv (`uv pip install C:/Users/fzabi/Desktop/dev/groupy symengine`) and run "
					"`python scripts/python/prepare_app_python_runtime.py`.",
					"python.groupy.not_installed");

			const std::array<const char *, 6> knownCodes = {
				"unknown_point_group", "empty_basis", "frame_alignment_failed", "basis_not_closed",
				"invalid_active_space", "invalid_json"};
			for (const char *knownCode : knownCodes)
				if (*scriptCode == knownCode)
					return MakePythonExecutionError(
						"Group-theory analysis could not be completed.", error.technicalDetails,
						"Check the selected basis, point-group label, and active space.",
						std::string("python.groupy.analysis.") + knownCode);
			return error;
		}

		[[nodiscard]] ExactCoefficient ParseCoefficient(const nlohmann::json &value)
		{
			return {
				value.at("exact").get<std::string>(), value.at("numeric").get<double>(),
				value.value("numericImaginary", 0.0), value.value("latex", "")};
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
					vector.coefficients.push_back(ParseCoefficient(coefficient));
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

	Result<PointGroupAnalysisResult> GroupTheoryBridge::Analyze(const PointGroupAnalysisRequest &request) const
	{
		const Path payloadPath = Path(FileSystem::TempDirectoryPath()) /
			("ds_groupy_point_group_" + ToString(GenerateUuid()) + ".json");
		nlohmann::json sites = nlohmann::json::array();
		for (const BasisSite &site : request.sites)
			sites.push_back({
				{"label", site.label},
				{"element", site.element},
				{"position", {site.position.x, site.position.y, site.position.z}}});
		const nlohmann::json payload = {
			{"pointGroupLabel", request.pointGroupLabel},
			{"sites", std::move(sites)},
			{"symmetryTolerance", request.symmetryTolerance},
			{"activeOrbitalIrreps", request.activeOrbitalIrreps},
			{"activeElectronCount", request.activeElectronCount},
			{"activeOrbitalLabels", request.activeOrbitalLabels}};

		{
			std::ofstream stream(payloadPath.Native(), std::ios::binary | std::ios::trunc);
			if (!stream)
				return MakePythonExecutionError(
					"Could not write the group-theory request.",
					"Failed to open " + payloadPath.String() + " for writing.",
					"Verify the OS temp directory is writable.",
					"python.groupy.analysis.temp_write_failed");
			stream << payload.dump();
		}

		const PythonExampleScript script = ResolvePythonExampleScript("groupy_point_group_analysis.py");
		ScriptRunOptions options;
		options.scriptPath = script.scriptPath;
		options.arguments = {payloadPath.String()};
		options.workingDirectory = script.workingDirectory;
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		std::error_code removeError;
		FileSystem::Remove(payloadPath.Native(), removeError);
		if (!runResult)
			return MakeAnalysisScriptError(runResult.Error());

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
			return MakePythonExecutionError(
				"Group-theory analysis returned no output.",
				"Expected one JSON payload in stdout but received an empty payload.\nStderr:\n" +
					runResult->standardError,
				"Verify groupy_point_group_analysis.py output contract.",
				"python.groupy.analysis.invalid_json");

		try
		{
			const nlohmann::json json = nlohmann::json::parse(jsonLine);
			PointGroupAnalysisResult result;
			const auto &detection = json.at("detection");
			result.detection.ran = detection.at("ran").get<bool>();
			result.detection.determined = detection.at("determined").get<bool>();
			result.detection.pointGroupLabel = detection.at("pointGroupLabel").get<std::string>();
			result.detection.detectorSymbol = detection.at("detectorSymbol").get<std::string>();
			result.detection.tolerance = detection.at("tolerance").get<double>();
			result.detection.reason = detection.at("reason").get<std::string>();
			if (!result.detection.determined)
				return result;

			const auto &rotation = json.at("frameRotation");
			for (int row = 0; row < 3; ++row)
				for (int column = 0; column < 3; ++column)
					result.frameRotation[column][row] = rotation.at(row).at(column).get<double>();

			const auto &table = json.at("characterTable");
			result.characterTable.pointGroupLabel = table.at("pointGroupLabel").get<std::string>();
			result.characterTable.groupOrder = table.at("groupOrder").get<int>();
			result.characterTable.classLabels = table.at("classLabels").get<std::vector<std::string>>();
			result.characterTable.classSizes = table.at("classSizes").get<std::vector<int>>();
			result.characterTable.irrepLabels = table.at("irrepLabels").get<std::vector<std::string>>();
			result.characterTable.irrepDimensions = table.at("irrepDimensions").get<std::vector<int>>();
			for (const auto &row : table.at("characters"))
			{
				std::vector<ExactCoefficient> values;
				for (const auto &value : row)
					values.push_back(ParseCoefficient(value));
				result.characterTable.characters.push_back(std::move(values));
			}
			for (const auto &value : json.at("reducibleCharacters"))
				result.reducibleCharacters.push_back(ParseCoefficient(value));

			const auto &reduction = json.at("reduction");
			result.reduction.pointGroupLabel = reduction.at("pointGroupLabel").get<std::string>();
			result.reduction.siteLabels = reduction.at("siteLabels").get<std::vector<std::string>>();
			result.reduction.groupOrder = reduction.at("groupOrder").get<int>();
			for (const auto &entry : reduction.at("decomposition"))
				result.reduction.decomposition.push_back({entry.at("irrepLabel"), entry.at("multiplicity"), entry.at("dimension")});
			for (const auto &entry : reduction.at("projectedVectors"))
			{
				SymmetryAdaptedVector vector;
				vector.irrepLabel = entry.at("irrepLabel").get<std::string>();
				vector.occurrenceIndex = entry.at("occurrenceIndex").get<int>();
				vector.irrepRow = entry.at("irrepRow").get<int>();
				for (const auto &value : entry.at("coefficients"))
					vector.coefficients.push_back(ParseCoefficient(value));
				result.reduction.projectedVectors.push_back(std::move(vector));
			}
			for (const auto &entry : json.at("multiplets"))
				result.multiplets.push_back({entry.at("irrepLabel"), entry.at("spinMultiplicity"), entry.at("irrepDimension"), entry.at("countPerRow"), entry.at("totalStates")});
			result.multipletTotalStates = json.at("multipletTotalStates").get<int>();
			result.tensorPower = json.value("tensorPower", 0);
			for (const auto &entry : json.value("tensorPowerDecomposition", nlohmann::json::array()))
				result.tensorPowerDecomposition.push_back({entry.at("irrepLabel"), entry.at("multiplicity"), entry.at("dimension")});
			for (const auto &entry : json.value("activeShells", nlohmann::json::array()))
				result.activeShells.push_back({entry.at("irrepLabel"), entry.at("label"), entry.at("firstOrbital"), entry.at("dimension")});
			result.activeOrbitalLabels = json.value("activeOrbitalLabels", std::vector<std::string>{});
			result.wavefunctionsSkippedReason = json.value("wavefunctionsSkippedReason", "");
			for (const auto &entry : json.value("wavefunctions", nlohmann::json::array()))
			{
				MultipletWavefunction state;
				state.irrepLabel = entry.at("irrepLabel").get<std::string>();
				state.spinMultiplicity = entry.at("spinMultiplicity").get<int>();
				state.copyIndex = entry.at("copyIndex").get<int>();
				state.irrepRow = entry.at("irrepRow").get<int>();
				state.twiceMs = entry.at("twiceMs").get<int>();
				state.configuration = entry.at("configuration").get<std::vector<int>>();
				for (const auto &determinant : entry.at("determinants"))
				{
					DeterminantTerm term;
					term.coefficient = ParseCoefficient(determinant.at("coefficient"));
					for (const auto &orbital : determinant.at("occupied"))
						term.occupied.push_back({orbital.at("orbitalIndex"), orbital.at("spinUp")});
					state.determinants.push_back(std::move(term));
				}
				result.wavefunctions.push_back(std::move(state));
			}
			return result;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Group-theory analysis output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the groupy bridge script prints exactly one valid JSON result.",
				"python.groupy.analysis.invalid_json");
		}
	}
} // namespace DefectStudio
