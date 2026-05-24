#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScientificPythonRuntime.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	Result<void> ScientificPythonRuntime::Initialize()
	{
		Result<void> startResult = m_Interpreter.Start();
		if (!startResult)
			return startResult.Error();

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
