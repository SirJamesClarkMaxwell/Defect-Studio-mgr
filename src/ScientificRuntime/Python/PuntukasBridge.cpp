#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PuntukasBridge.hpp"

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
	} // namespace

	Result<PymatgenStructureData> PuntukasBridge::LoadStructure(const Path &filePath) const
	{
		if (filePath.Empty())
		{
			return MakePythonExecutionError(
				"Puntukas structure load request is incomplete.",
				"Expected filePath to be set.",
				"Provide a valid CONTCAR/POSCAR path before invoking the bridge.",
				"python.puntukas.load.request_incomplete");
		}

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("puntukas_structure_load.py");
		options.scriptPath = script.scriptPath;
		options.arguments = {filePath.String()};
		options.workingDirectory = script.workingDirectory;

		const auto startTime = Time::NowSteady();
		DS_LOG_DEBUG("PuntukasBridge: loading structure from {}", filePath.String());
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
				"Puntukas structure loader returned no output.",
				"Expected JSON metadata in stdout but received an empty payload.",
				"Verify scripts/python/examples/puntukas_structure_load.py output contract.",
				"python.puntukas.load.empty_output");
		}

		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			PymatgenStructureData structureData = ParseStructurePayloadJson(payload);

			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				Time::NowSteady() - startTime).count();
			DS_LOG_DEBUG("PuntukasBridge: parsed structure in {} ms", elapsedMs);
			return structureData;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Puntukas structure loader output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with structure data.",
				"python.puntukas.load.invalid_json");
		}
	}
} // namespace DefectStudio
