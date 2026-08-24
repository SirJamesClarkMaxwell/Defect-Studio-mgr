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

		template <typename T>
		[[nodiscard]] std::optional<T> GetOptional(const nlohmann::json &object, const char *key)
		{
			const nlohmann::json &value = object.at(key);
			if (value.is_null())
				return std::nullopt;
			return value.get<T>();
		}

		[[nodiscard]] VaspOutputSummaryData ParseSummaryJson(const nlohmann::json &summary)
		{
			VaspOutputSummaryData data;
			data.energyTrend = GetOptional<std::vector<double>>(summary, "energy_trend");
			data.finalEnergy = GetOptional<double>(summary, "final_energy");
			data.cpuTimeSeconds = GetOptional<double>(summary, "cpu_time");
			data.userTimeSeconds = GetOptional<double>(summary, "user_time");
			data.systemTimeSeconds = GetOptional<double>(summary, "system_time");
			data.elapsedTimeSeconds = GetOptional<double>(summary, "elapsed_time");
			if (const std::optional<std::vector<double>> drift = GetOptional<std::vector<double>>(summary, "total_drift");
				drift.has_value() && drift->size() == 3)
				data.totalDrift = std::array<double, 3>{(*drift)[0], (*drift)[1], (*drift)[2]};
			data.nelect = GetOptional<double>(summary, "nelect");
			data.ispin = GetOptional<int>(summary, "ispin");
			data.pressureKilobar = GetOptional<double>(summary, "pressure");
			if (const std::optional<std::vector<std::vector<double>>> stress =
					GetOptional<std::vector<std::vector<double>>>(summary, "stress_tensor");
				stress.has_value() && stress->size() == 3)
			{
				std::array<std::array<double, 3>, 3> tensor{};
				for (std::size_t row = 0; row < 3; ++row)
					for (std::size_t col = 0; col < 3 && col < (*stress)[row].size(); ++col)
						tensor[row][col] = (*stress)[row][col];
				data.stressTensorKilobar = tensor;
			}
			data.spaceGroupSymbol = GetOptional<std::string>(summary, "space_group_symbol");
			data.spaceGroupNumber = GetOptional<int>(summary, "space_group_number");
			data.pointGroupSymbol = GetOptional<std::string>(summary, "point_group_symbol");
			data.pointGroupSchoenflies = GetOptional<std::string>(summary, "point_group_schoenflies");
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

			data.summary = ParseSummaryJson(payload.at("summary"));

			return data;
		}
	} // namespace

	Result<VaspOutputData> VaspOutputBridge::LoadOutput(
		const Path &directory, int bandStart, int bandEnd, bool includeOrbitals) const
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
		options.arguments = {
			directory.String(), std::to_string(bandStart), std::to_string(bandEnd),
			includeOrbitals ? "1" : "0"};
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

		Result<VaspOutputData> parsed = ParseVaspOutputJson(jsonLine);
		if (parsed)
		{
			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				Time::NowSteady() - startTime).count();
			DS_LOG_DEBUG("VaspOutputBridge: parsed VASP output in {} ms", elapsedMs);
		}
		return parsed;
	}

	Result<VaspOutputData> ParseVaspOutputJson(const std::string &jsonPayload)
	{
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonPayload);
			return ParseVaspOutputPayloadJson(payload);
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"VASP output JSON parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() + "\nPayload: " + jsonPayload,
				"Ensure the payload matches vasp_output_load.py's output contract.",
				"python.vasp_output.parse.invalid_json");
		}
	}
} // namespace DefectStudio
