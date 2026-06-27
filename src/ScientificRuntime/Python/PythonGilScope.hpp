#pragma once

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#endif

namespace DefectStudio
{
	class PythonGilAcquireScope final : public IJobExecutionGuard
	{
	public:
		PythonGilAcquireScope();
		~PythonGilAcquireScope() override;

		[[nodiscard]] bool IsActive() const;

	private:
#if DS_PYTHON_CAPI_AVAILABLE
		Ref<nanobind::gil_scoped_acquire> m_Guard;
#endif
		bool m_IsActive = false;
	};
} // namespace DefectStudio
