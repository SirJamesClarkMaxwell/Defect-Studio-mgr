#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PythonInterpreter.hpp"

#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "Core/Utils/Logger.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"

namespace DefectStudio
{
	std::atomic_bool g_PythonRuntimeInitialized{false};

#if DS_PYTHON_CAPI_AVAILABLE
	static void configureEmbeddedPythonHome()
	{
		const Path pythonHome = Platform::ResolveEmbeddedPythonHome();
		if (pythonHome.Empty())
			return;

		Platform::SetPythonHomeEnvironment(pythonHome);
		DS_LOG_INFO("PythonInterpreter using app-local runtime at {}", pythonHome.String());
	}
#endif

	PythonInterpreter::~PythonInterpreter()
	{
		Stop();
	}

	Result<void> PythonInterpreter::Start()
	{
		if (m_IsRunning)
			return {};

#if DS_PYTHON_CAPI_AVAILABLE
		if (!Py_IsInitialized())
			configureEmbeddedPythonHome();

		if (!Py_IsInitialized())
			Py_InitializeEx(0);

		if (!Py_IsInitialized())
		{
			return MakePythonUnavailableError(
				"Python interpreter could not be initialized.",
				"Py_InitializeEx() returned but Py_IsInitialized() is false.",
				"Verify install/app/python runtime content and regenerate projects after setup.",
				"python.embed.initialize_failed");
		}

		m_MainThreadState = PyEval_SaveThread();
		if (m_MainThreadState == nullptr)
		{
			return MakePythonUnavailableError(
				"Python interpreter lock setup failed.",
				"PyEval_SaveThread() returned null thread state.",
				"Ensure the interpreter is initialized on the main thread before background usage.",
				"python.embed.save_thread_failed");
		}

		m_IsRunning = true;
		g_PythonRuntimeInitialized.store(true, std::memory_order_relaxed);
		DS_LOG_INFO("PythonInterpreter started (embedded mode)");
		return {};
#else
		return MakePythonUnavailableError(
			"Embedded Python runtime is not available in this build.",
			"Build-time flag DS_PYTHON_CAPI_AVAILABLE is 0.",
			"Prepare install/app/python runtime and regenerate project files to enable Python embedding.",
			"python.embed.unavailable");
#endif
	}

	void PythonInterpreter::Stop()
	{
		if (!m_IsRunning)
			return;

#if DS_PYTHON_CAPI_AVAILABLE
		if (m_MainThreadState != nullptr)
		{
			PyEval_RestoreThread(m_MainThreadState);
			m_MainThreadState = nullptr;
		}

		if (Py_IsInitialized())
		{
			const int finalizeCode = Py_FinalizeEx();
			if (finalizeCode < 0)
				DS_LOG_WARN("PythonInterpreter finalize reported an error code: {}", finalizeCode);
		}
#endif

		m_IsRunning = false;
		g_PythonRuntimeInitialized.store(false, std::memory_order_relaxed);
		DS_LOG_INFO("PythonInterpreter stopped");
	}

	bool PythonInterpreter::IsRunning() const
	{
		return m_IsRunning;
	}

	bool PythonInterpreter::IsEmbeddingAvailable()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		return true;
#else
		return false;
#endif
	}

	bool PythonInterpreter::IsRuntimeInitialized()
	{
		return g_PythonRuntimeInitialized.load(std::memory_order_relaxed);
	}
} // namespace DefectStudio
