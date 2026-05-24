#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PymatgenBridge.hpp"

#include <nlohmann/json.hpp>

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"

namespace DefectStudio
{
	static std::string extractJsonLine(const std::string &rawOutput)
	{
		std::istringstream input(rawOutput);
		std::string line;
		std::string lastNonEmptyLine;
		while (std::getline(input, line))
		{
			if (!line.empty())
				lastNonEmptyLine = line;
		}
		return lastNonEmptyLine;
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

		const std::string jsonLine = extractJsonLine(runResult->standardOutput);
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
