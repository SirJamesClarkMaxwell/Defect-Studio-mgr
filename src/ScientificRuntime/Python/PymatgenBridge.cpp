#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PymatgenBridge.hpp"

#include <chrono>

#include <nlohmann/json.hpp>

#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
namespace nb = nanobind;
#endif

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/PythonGilScope.hpp"
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

	Result<void> PymatgenBridge::WarmUp()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		if (m_WarmedUp)
			return {};

		PythonGilAcquireScope gil;
		if (!gil.IsActive())
		{
			return MakePythonUnavailableError(
				"Cannot warm up PymatgenBridge: GIL is not available.",
				"PythonInterpreter must be started before calling WarmUp().",
				"Call ScientificPythonRuntime::Initialize() before using the bridge.",
				"python.pymatgen.warmup.no_gil");
		}

		try
		{
			nb::module_::import_("pymatgen.core");
			m_WarmedUp = true;
			DS_LOG_INFO("PymatgenBridge: warm-up complete, pymatgen.core cached in sys.modules");
			return {};
		}
		catch (const nb::python_error &e)
		{
			return MakePythonExecutionError(
				"PymatgenBridge warm-up failed: cannot import pymatgen.core.",
				e.what(),
				"Ensure pymatgen is installed in the embedded Python environment at install/app/python.",
				"python.pymatgen.warmup.import_failed");
		}
#else
		DS_LOG_WARN("PymatgenBridge::WarmUp: DS_PYTHON_CAPI_AVAILABLE=0; subprocess fallback will be used for all loads.");
		return {};
#endif
	}

	bool PymatgenBridge::IsWarmedUp() const noexcept
	{
		return m_WarmedUp;
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

#if DS_PYTHON_CAPI_AVAILABLE
		if (m_WarmedUp)
			return LoadStructuresEmbedded(filePaths);
#endif
		return LoadStructuresViaSubprocess(filePaths);
	}

	Result<std::vector<PymatgenStructureData>> PymatgenBridge::LoadStructuresEmbedded(
		const std::vector<Path> &filePaths) const
	{
#if DS_PYTHON_CAPI_AVAILABLE
		PythonGilAcquireScope gil;
		if (!gil.IsActive())
		{
			DS_LOG_WARN("PymatgenBridge: embedded path requested but GIL unavailable; falling back to subprocess");
			return LoadStructuresViaSubprocess(filePaths);
		}

		const auto startTime = Time::NowSteady();
		try
		{
			nb::object pymatgen = nb::module_::import_("pymatgen.core");
			nb::object Structure = pymatgen.attr("Structure");

			std::vector<PymatgenStructureData> results;
			results.reserve(filePaths.size());

			for (const Path &filePath : filePaths)
			{
				nb::object pyStructure = Structure.attr("from_file")(filePath.String());

				PymatgenStructureData data;
				data.reducedFormula = nb::cast<std::string>(
					pyStructure.attr("composition").attr("reduced_formula"));

				nb::object matrix = pyStructure.attr("lattice").attr("matrix");
				for (int row = 0; row < 3; ++row)
				{
					nb::object vec = matrix[nb::int_(row)];
					data.lattice[row] = glm::vec3(
						nb::cast<float>(vec[nb::int_(0)]),
						nb::cast<float>(vec[nb::int_(1)]),
						nb::cast<float>(vec[nb::int_(2)]));
				}

				nb::object sites = pyStructure.attr("sites");
				int siteCount = static_cast<int>(nb::len(sites));
				data.sites.reserve(siteCount);
				for (int i = 0; i < siteCount; ++i)
				{
					nb::object site = sites[nb::int_(i)];

					PymatgenStructureSite siteData;
					siteData.element = nb::cast<std::string>(
						site.attr("specie").attr("symbol"));

					nb::object frac = site.attr("frac_coords");
					siteData.fractionalPosition = glm::vec3(
						nb::cast<float>(frac[nb::int_(0)]),
						nb::cast<float>(frac[nb::int_(1)]),
						nb::cast<float>(frac[nb::int_(2)]));

					nb::object cart = site.attr("coords");
					siteData.cartesianPosition = glm::vec3(
						nb::cast<float>(cart[nb::int_(0)]),
						nb::cast<float>(cart[nb::int_(1)]),
						nb::cast<float>(cart[nb::int_(2)]));

					data.sites.push_back(std::move(siteData));
				}
				results.push_back(std::move(data));
			}

			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				Time::NowSteady() - startTime).count();
			DS_LOG_DEBUG(
				"PymatgenBridge: embedded loaded {} structure(s) in {} ms",
				results.size(),
				elapsedMs);
			return results;
		}
		catch (const nb::python_error &e)
		{
			DS_LOG_WARN(
				"PymatgenBridge: embedded path threw Python exception; falling back to subprocess: {}",
				e.what());
			return LoadStructuresViaSubprocess(filePaths);
		}
#else
		return LoadStructuresViaSubprocess(filePaths);
#endif
	}

	Result<std::vector<PymatgenStructureData>> PymatgenBridge::LoadStructuresViaSubprocess(
		const std::vector<Path> &filePaths) const
	{
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
