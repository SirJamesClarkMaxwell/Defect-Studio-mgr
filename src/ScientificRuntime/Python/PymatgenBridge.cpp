#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PymatgenBridge.hpp"

#include <chrono>

#include <nlohmann/json.hpp>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	[[nodiscard]] static PymatgenStructureData ParsePymatgenStructurePayload(const nlohmann::json &payload)
	{
		PymatgenStructureData result;
		result.reducedFormula = payload.value("reduced_formula", std::string{});

		const auto lattice = payload.at("lattice");
		for (int row = 0; row < 3; ++row)
		{
			const auto vector = lattice.at(row);
			result.lattice[row] = glm::vec3(
				vector.at(0).get<float>(),
				vector.at(1).get<float>(),
				vector.at(2).get<float>());
		}

		const auto sites = payload.at("sites");
		result.sites.reserve(sites.size());
		for (const auto &site : sites)
		{
			const auto fractional = site.at("fractional");
			const auto cartesian = site.at("cartesian");

			PymatgenStructureSite parsedSite;
			parsedSite.element = site.value("element", std::string{});
			parsedSite.fractionalPosition = glm::vec3(
				fractional.at(0).get<float>(),
				fractional.at(1).get<float>(),
				fractional.at(2).get<float>());
			parsedSite.cartesianPosition = glm::vec3(
				cartesian.at(0).get<float>(),
				cartesian.at(1).get<float>(),
				cartesian.at(2).get<float>());
			result.sites.push_back(std::move(parsedSite));
		}

		return result;
	}

	[[nodiscard]] static Result<nlohmann::json> RunPymatgenStructureLoadScript(
		const std::vector<Path> &filePaths,
		const ScriptRunner &scriptRunner)
	{
		ScriptRunOptions options;
		options.scriptPath = Path("scripts") / "python" / "examples" / "pymatgen_structure_load.py";
		options.arguments.reserve(filePaths.size());
		for (const Path &filePath : filePaths)
			options.arguments.push_back(filePath.String());
		options.workingDirectory = FileSystem::CurrentPath();

		const auto startTime = Time::NowSteady();
		DS_LOG_DEBUG("PymatgenBridge: loading {} structure(s)", filePaths.size());
		Result<ScriptRunResult> runResult = scriptRunner.RunFile(options);
		if (!runResult)
			return runResult.Error();
		const auto scriptMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
			Time::NowSteady() - startTime).count();
		DS_LOG_DEBUG(
			"PymatgenBridge: script finished for {} structure(s) in {} ms (stdout={} bytes, stderr={} bytes)",
			filePaths.size(),
			scriptMilliseconds,
			runResult->standardOutput.size(),
			runResult->standardError.size());

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Pymatgen structure loader returned no output.",
				"Expected JSON metadata in stdout but received an empty payload.",
				"Verify scripts/python/examples/pymatgen_structure_load.py output contract.",
				"python.pymatgen.load.empty_output");
		}

		nlohmann::json payload;
		try
		{
			payload = nlohmann::json::parse(jsonLine);
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Pymatgen structure loader output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with structure data.",
				"python.pymatgen.load.invalid_json");
		}

		return payload;
	}

	Result<PymatgenStructureData> PymatgenBridge::LoadStructure(const Path &filePath) const
	{
		Result<std::vector<PymatgenStructureData>> structures = LoadStructures({filePath});
		if (!structures)
			return structures.Error();
		if (structures->empty())
		{
			return MakePythonExecutionError(
				"Pymatgen structure loader returned no structures.",
				"Expected one parsed structure but got an empty result.",
				"Verify scripts/python/examples/pymatgen_structure_load.py output contract.",
				"python.pymatgen.load.empty_structures");
		}
		return structures->front();
	}

	Result<std::vector<PymatgenStructureData>> PymatgenBridge::LoadStructures(const std::vector<Path> &filePaths) const
	{
		if (filePaths.empty())
		{
			return MakePythonExecutionError(
				"Pymatgen structure load request is incomplete.",
				"Expected at least one filePath to be set.",
				"Provide valid crystal structure paths before invoking the bridge.",
				"python.pymatgen.load.request_incomplete");
		}

		for (const Path &filePath : filePaths)
		{
			if (filePath.Empty())
			{
				return MakePythonExecutionError(
					"Pymatgen structure load request is incomplete.",
					"Expected every filePath to be set.",
					"Provide valid crystal structure paths before invoking the bridge.",
					"python.pymatgen.load.request_incomplete");
			}
		}

		const auto startTime = Time::NowSteady();
		Result<nlohmann::json> payloadResult = RunPymatgenStructureLoadScript(filePaths, m_ScriptRunner);
		if (!payloadResult)
			return payloadResult.Error();

		std::vector<PymatgenStructureData> structures;
		try
		{
			const nlohmann::json &payload = payloadResult.Value();
			if (payload.contains("structures"))
			{
				const auto payloadStructures = payload.at("structures");
				structures.reserve(payloadStructures.size());
				for (const auto &structurePayload : payloadStructures)
					structures.push_back(ParsePymatgenStructurePayload(structurePayload));
			}
			else
			{
				structures.push_back(ParsePymatgenStructurePayload(payload));
			}
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Pymatgen structure loader output validation failed.",
				std::string("JSON schema error: ") + exception.what(),
				"Ensure the bridge script prints lattice and sites for every structure.",
				"python.pymatgen.load.invalid_payload");
		}

		const auto totalMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
			Time::NowSteady() - startTime).count();
		DS_LOG_DEBUG(
			"PymatgenBridge: parsed {} structure(s) in {} ms",
			structures.size(),
			totalMilliseconds);
		return structures;
	}

	Result<PymatgenRoundtripResult> PymatgenBridge::RoundtripPoscar(const PymatgenRoundtripRequest &request) const
	{
		if (request.inputPoscarPath.Empty() || request.outputPoscarPath.Empty())
		{
			return MakePythonExecutionError(
				"Pymatgen POSCAR roundtrip request is incomplete.",
				"Expected both inputPoscarPath and outputPoscarPath to be set.",
				"Provide valid POSCAR input/output paths before invoking the bridge.",
				"python.pymatgen.request_incomplete");
		}

		ScriptRunOptions options;
		options.scriptPath = Path("scripts") / "python" / "examples" / "pymatgen_poscar_roundtrip.py";
		options.arguments = { request.inputPoscarPath.String(), request.outputPoscarPath.String() };
		options.workingDirectory = FileSystem::CurrentPath();

		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"Pymatgen script returned no output.",
				"Expected JSON metadata in stdout but received an empty payload.",
				"Verify scripts/python/examples/pymatgen_poscar_roundtrip.py output contract.",
				"python.pymatgen.empty_output");
		}

		nlohmann::json payload;
		try
		{
			payload = nlohmann::json::parse(jsonLine);
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"Pymatgen script output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with metadata.",
				"python.pymatgen.invalid_json");
		}

		PymatgenRoundtripResult result;
		result.outputPoscarPath = payload.value("output_path", request.outputPoscarPath.String());
		result.reducedFormula = payload.value("reduced_formula", std::string{});
		result.siteCount = payload.value("site_count", 0);
		return result;
	}
} // namespace DefectStudio
