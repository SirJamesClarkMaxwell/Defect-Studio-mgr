#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/VaspOutputBridge.hpp"

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

		[[nodiscard]] VaspOrbitalChannelData ParseOrbitalChannel(const nlohmann::json &channel)
		{
			VaspOrbitalChannelData data;
			data.energy = channel.at("energy").get<float>();
			data.occupation = channel.at("occupation").get<float>();
			data.localization = channel.at("localization").get<float>();
			const nlohmann::json &irrep = channel.at("irrep");
			if (!irrep.is_null())
				data.irrep = irrep.get<std::string>();
			return data;
		}

		[[nodiscard]] VaspOutputData ParseVaspOutputPayloadJson(const nlohmann::json &payload)
		{
			VaspOutputData data;
			data.path = Path(payload.at("path").get<std::string>());

			const nlohmann::json &gap = payload.at("gap");
			if (!gap.is_null())
			{
				VaspBandGapData gapData;
				gapData.bandgap = gap.at("bandgap").get<float>();
				gapData.homo = gap.at("homo").get<float>();
				gapData.lumo = gap.at("lumo").get<float>();
				data.gap = gapData;
			}

			const nlohmann::json &orbitals = payload.at("orbitals");
			if (!orbitals.is_null())
			{
				std::vector<VaspOrbitalRecord> records;
				records.reserve(orbitals.size());
				for (const nlohmann::json &entry : orbitals)
				{
					VaspOrbitalRecord record;
					record.band = entry.at("band").get<int>();
					record.up = ParseOrbitalChannel(entry.at("up"));
					record.down = ParseOrbitalChannel(entry.at("down"));
					records.push_back(std::move(record));
				}
				data.orbitals = std::move(records);
			}

			const nlohmann::json &orbitalsError = payload.at("orbitals_error");
			if (!orbitalsError.is_null())
				data.orbitalsError = orbitalsError.get<std::string>();

			return data;
		}
	} // namespace

	Result<VaspOutputData> VaspOutputBridge::LoadOutput(const Path &directory, int bandStart, int bandEnd) const
	{
		if (directory.Empty())
		{
			return MakePythonExecutionError(
				"VASP output load request is incomplete.",
				"Expected directory to be set.",
				"Provide a valid calculation directory (containing OUTCAR/vasprun.xml) before invoking the bridge.",
				"python.vasp_output.load.request_incomplete");
		}

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("vasp_output_load.py");
		options.scriptPath = script.scriptPath;
		options.arguments = {directory.String(), std::to_string(bandStart), std::to_string(bandEnd)};
		options.workingDirectory = script.workingDirectory;

		const auto startTime = Time::NowSteady();
		DS_LOG_DEBUG("VaspOutputBridge: loading VASP output from {}", directory.String());
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
				"VASP output loader returned no output.",
				"Expected JSON metadata in stdout but received an empty payload.",
				"Verify scripts/python/examples/vasp_output_load.py output contract.",
				"python.vasp_output.load.empty_output");
		}

		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			VaspOutputData outputData = ParseVaspOutputPayloadJson(payload);

			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				Time::NowSteady() - startTime).count();
			DS_LOG_DEBUG("VaspOutputBridge: parsed VASP output in {} ms", elapsedMs);
			return outputData;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"VASP output loader output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with output data.",
				"python.vasp_output.load.invalid_json");
		}
	}
} // namespace DefectStudio
