#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScientificPythonRuntime.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PymatgenConversion.hpp"

namespace DefectStudio
{
	Result<void> ScientificPythonRuntime::Initialize()
	{
		Result<void> startResult = m_Interpreter.Start();
		if (!startResult)
		{
			const StructuredError &error = startResult.Error();
			DS_LOG_INFO(
				"ScientificPythonRuntime: embedded interpreter unavailable; subprocess fallback active. [{}] {}",
				error.code,
				error.technicalDetails);
		}

		Result<void> warmUpResult = m_PymatgenBridge.WarmUp();
		if (!warmUpResult)
		{
			DS_LOG_WARN(
				"ScientificPythonRuntime: PymatgenBridge warm-up skipped; subprocess fallback active. [{}] {}",
				warmUpResult.Error().code,
				warmUpResult.Error().technicalDetails);
		}

		m_IsReady = true;
		return {};
	}

	void ScientificPythonRuntime::Shutdown()
	{
		m_Interpreter.Stop();
		m_IsReady = false;
	}

	bool ScientificPythonRuntime::IsReady() const
	{
		return m_IsReady;
	}

	Result<ScriptRunResult> ScientificPythonRuntime::RunBridgeRoundtripDemo() const
	{
		if (!m_IsReady)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"Python runtime is not initialized.",
				"ScientificPythonRuntime::RunBridgeRoundtripDemo called before Initialize().",
				"Initialize ScientificPythonRuntime in ScientificRuntimeLayer before running bridge scripts.",
				"ScientificRuntime/Python",
				"python.runtime.not_initialized"};
		}

		ScriptRunOptions options;
		options.scriptPath = Path("scripts") / "python" / "examples" / "bridge_roundtrip_demo.py";
		options.workingDirectory = FileSystem::CurrentPath();
		return m_ScriptRunner.RunFile(options);
	}

	Result<CrystalStructure> ScientificPythonRuntime::LoadCrystalStructure(const Path &filePath) const
	{
		if (!m_IsReady)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"Python runtime is not initialized.",
				"ScientificPythonRuntime::LoadCrystalStructure called before Initialize().",
				"Initialize ScientificPythonRuntime in ScientificRuntimeLayer before bridge usage.",
				"ScientificRuntime/Python",
				"python.runtime.not_initialized"};
		}

		Result<PymatgenStructureData> structure = m_PymatgenBridge.LoadStructure(filePath);
		if (!structure)
			return structure.Error();
		return ConvertPymatgenStructureToCrystalStructure(structure.Value());
	}

	Result<std::vector<CrystalStructure>> ScientificPythonRuntime::LoadCrystalStructures(const std::vector<Path> &filePaths) const
	{
		if (!m_IsReady)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"Python runtime is not initialized.",
				"ScientificPythonRuntime::LoadCrystalStructures called before Initialize().",
				"Initialize ScientificPythonRuntime in ScientificRuntimeLayer before bridge usage.",
				"ScientificRuntime/Python",
				"python.runtime.not_initialized"};
		}

		Result<std::vector<PymatgenStructureData>> structures = m_PymatgenBridge.LoadStructures(filePaths);
		if (!structures)
			return structures.Error();

		std::vector<CrystalStructure> crystals;
		crystals.reserve(structures->size());
		for (const PymatgenStructureData &structure : structures.Value())
			crystals.push_back(ConvertPymatgenStructureToCrystalStructure(structure));
		return crystals;
	}

	Result<PymatgenRoundtripResult> ScientificPythonRuntime::RoundtripPoscar(const PymatgenRoundtripRequest &request) const
	{
		if (!m_IsReady)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"Python runtime is not initialized.",
				"ScientificPythonRuntime::RoundtripPoscar called before Initialize().",
				"Initialize ScientificPythonRuntime in ScientificRuntimeLayer before bridge usage.",
				"ScientificRuntime/Python",
				"python.runtime.not_initialized"};
		}

		return m_PymatgenBridge.RoundtripPoscar(request);
	}

	Result<ASEConvertResult> ScientificPythonRuntime::ConvertWithASE(const ASEConvertRequest &request) const
	{
		if (!m_IsReady)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"Python runtime is not initialized.",
				"ScientificPythonRuntime::ConvertWithASE called before Initialize().",
				"Initialize ScientificPythonRuntime in ScientificRuntimeLayer before bridge usage.",
				"ScientificRuntime/Python",
				"python.runtime.not_initialized"};
		}

		return m_ASEBridge.ConvertFile(request);
	}
} // namespace DefectStudio
