#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ASEBridge.hpp"

#include <nlohmann/json.hpp>

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"

namespace DefectStudio
{
	static std::string extractJsonLineFromOutput(const std::string &rawOutput)
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

	Result<ASEConvertResult> ASEBridge::ConvertFile(const ASEConvertRequest &request) const
	{
		if (request.inputPath.Empty() || request.outputPath.Empty())
		{
			return MakePythonExecutionError(
				"ASE conversion request is incomplete.",
				"inputPath and outputPath must both be specified.",
				"Provide explicit input and output paths before calling the ASE bridge.",
				"python.ase.request_incomplete");
		}

		ScriptRunOptions options;
		options.scriptPath = Path("scripts") / "python" / "examples" / "ase_convert.py";
		options.arguments = {
			request.inputPath.String(),
			request.outputPath.String(),
			request.inputFormat,
			request.outputFormat
		};
		options.workingDirectory = FileSystem::CurrentPath();

		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = extractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"ASE conversion script returned no output.",
				"Expected JSON output from scripts/python/examples/ase_convert.py.",
				"Ensure the ASE script writes JSON metadata on stdout.",
				"python.ase.empty_output");
		}

		nlohmann::json payload;
		try
		{
			payload = nlohmann::json::parse(jsonLine);
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"ASE script output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the script prints one JSON line describing conversion results.",
				"python.ase.invalid_json");
		}

		ASEConvertResult result;
		result.outputPath = payload.value("output_path", request.outputPath.String());
		result.chemicalFormula = payload.value("chemical_formula", std::string{});
		result.atomCount = payload.value("atom_count", 0);
		return result;
	}
} // namespace DefectStudio
