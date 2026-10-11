#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/FnvCorrectionBridge.hpp"

#include <cmath>
#include <format>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] double ReadFiniteDouble(const nlohmann::json &value)
		{
			if (!value.is_number())
				throw std::runtime_error("Expected a numeric FNV profile value.");
			const double number = value.get<double>();
			if (!std::isfinite(number))
				throw std::runtime_error("FNV profile values must be finite.");
			return number;
		}
	} // namespace

	Result<FnvProfiles> FnvCorrectionBridge::ComputeProfiles(const FnvProfileRequest &request) const
	{
		if (request.charge == 0 || !std::isfinite(request.dielectric) || request.dielectric <= 0.0 ||
			!std::isfinite(request.centreFractional.x) || !std::isfinite(request.centreFractional.y) ||
			!std::isfinite(request.centreFractional.z) || !std::isfinite(request.energyCutoff))
		{
			return MakePythonExecutionError(
				"FNV correction request is invalid.",
				"Expected nonzero charge, positive finite dielectric, finite centre coordinates and a finite cutoff.",
				"Check the charge, dielectric, defect centre and energy cutoff.",
				"python.fnv.request_invalid");
		}
		if (request.defectLocpot.Empty() || request.bulkLocpot.Empty() ||
			!FileSystem::Exists(request.defectLocpot.Native()) || !FileSystem::Exists(request.bulkLocpot.Native()))
		{
			return MakePythonExecutionError(
				"An FNV LOCPOT file was not found.",
				"Defect LOCPOT: " + request.defectLocpot.Utf8() + "\nBulk LOCPOT: " + request.bulkLocpot.Utf8(),
				"Pick existing defect and bulk LOCPOT files.",
				"python.fnv.locpot_missing");
		}

		const PythonExampleScript script = ResolvePythonExampleScript("fnv_freysoldt_profiles.py");
		ScriptRunOptions options;
		options.scriptPath = script.scriptPath;
		options.workingDirectory = script.workingDirectory;
		options.arguments = {
			request.defectLocpot.Utf8(), request.bulkLocpot.Utf8(), std::format("{:.10g}", static_cast<double>(request.charge)),
			std::format("{:.10g}", request.dielectric),
			std::format("{:.10g},{:.10g},{:.10g}", request.centreFractional.x, request.centreFractional.y, request.centreFractional.z),
			std::format("{:.10g}", request.energyCutoff)};
		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		if (!runResult)
		{
			const StructuredError &error = runResult.Error();
			if (error.technicalDetails.find("pymatgen_defects_not_installed") != std::string::npos)
			{
				return MakePythonUnavailableError(
					"pymatgen-analysis-defects is not installed.", error.technicalDetails,
					"uv pip install pymatgen-analysis-defects==2026.3.20",
					"python.pymatgen_defects.not_installed");
			}
			return error;
		}

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			FnvProfiles profiles;
			profiles.charge = request.charge;
			profiles.dielectric = request.dielectric;
			profiles.centreFractional = request.centreFractional;
			profiles.energyCutoff = ReadFiniteDouble(payload.at("energyCutoff"));
			profiles.latticeEnergy = ReadFiniteDouble(payload.at("electrostatic"));
			profiles.modelPotentialMean = ReadFiniteDouble(payload.at("modelPotentialMean"));
			if (profiles.energyCutoff <= 0.0)
				throw std::runtime_error("FNV energy cutoff must be positive.");
			const auto &axes = payload.at("axes");
			if (!axes.is_array() || axes.size() != profiles.axes.size())
				throw std::runtime_error("Expected exactly three FNV axes.");
			for (std::size_t a = 0; a < profiles.axes.size(); ++a)
			{
				const auto &entry = axes.at(a);
				FnvAxisProfile &axis = profiles.axes[a];
				axis.length = ReadFiniteDouble(entry.at("length"));
				const auto &deltaV = entry.at("deltaV");
				const auto &model = entry.at("modelPotential");
				if (axis.length <= 0.0 || !deltaV.is_array() || !model.is_array() ||
					deltaV.size() < 2 || deltaV.size() != model.size())
					throw std::runtime_error("Expected positive axis length and matching FNV profiles with at least two points.");
				const std::size_t count = deltaV.size();
				axis.position.reserve(count);
				axis.deltaV.reserve(count);
				axis.modelPotential.reserve(count);
				for (std::size_t k = 0; k < count; ++k)
				{
					axis.position.push_back(static_cast<double>(k) * axis.length / static_cast<double>(count));
					axis.deltaV.push_back(ReadFiniteDouble(deltaV.at(k)));
					axis.modelPotential.push_back(ReadFiniteDouble(model.at(k)));
				}
			}
			return profiles;
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"FNV profile output parsing failed.",
				std::string("JSON parse/schema error: ") + exception.what() +
					"\nStdout: " + runResult->standardOutput + "\nStderr: " + runResult->standardError,
				"Ensure fnv_freysoldt_profiles.py prints one JSON line with three matching planar profiles.",
				"python.fnv.invalid_json");
		}
	}
} // namespace DefectStudio
