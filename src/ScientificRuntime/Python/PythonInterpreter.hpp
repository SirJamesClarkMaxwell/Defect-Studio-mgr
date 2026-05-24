#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include <Python.h>
#endif

namespace DefectStudio
{
	class PythonInterpreter final
	{
	public:
		PythonInterpreter() = default;
		~PythonInterpreter();

		[[nodiscard]] Result<void> Start();
		void Stop();

		[[nodiscard]] bool IsRunning() const;
		[[nodiscard]] static bool IsEmbeddingAvailable();
		[[nodiscard]] static bool IsRuntimeInitialized();

	private:
#if DS_PYTHON_CAPI_AVAILABLE
		PyThreadState *m_MainThreadState = nullptr;
#endif
		bool m_IsRunning = false;
	};
} // namespace DefectStudio
