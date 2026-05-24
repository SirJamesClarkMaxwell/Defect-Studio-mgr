#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PythonGilScope.hpp"

#include "ScientificRuntime/Python/PythonInterpreter.hpp"

namespace DefectStudio
{
	PythonGilAcquireScope::PythonGilAcquireScope()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		if (PythonInterpreter::IsRuntimeInitialized())
		{
			m_Guard = CreateRef<nanobind::gil_scoped_acquire>();
			m_IsActive = true;
		}
#endif
	}

	PythonGilAcquireScope::~PythonGilAcquireScope() = default;

	bool PythonGilAcquireScope::IsActive() const
	{
		return m_IsActive;
	}
} // namespace DefectStudio
