#include "Core/dspch.hpp"

#include "ScientificRuntime/ScientificRuntimeLayer.hpp"

#include "Core/Capabilities/Capability.hpp"
#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/Utils/Logger.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"
#include "ScientificRuntime/Python/ScientificPythonRuntime.hpp"

namespace DefectStudio
{
	ScientificRuntimeLayer::ScientificRuntimeLayer() : Layer("ScientificRuntimeLayer")
	{
	}

	ScientificRuntimeLayer::~ScientificRuntimeLayer() = default;

	void ScientificRuntimeLayer::OnAttach()
	{
		m_PythonRuntime = CreateUnique<ScientificPythonRuntime>();
		Result<void> initializeResult = m_PythonRuntime->Initialize();
		m_PythonBridgeAvailable = initializeResult.HasValue();

		if (!m_PythonBridgeAvailable)
		{
			const StructuredError &error = initializeResult.Error();
			DS_LOG_WARN(
				"ScientificRuntimeLayer: Python bridge unavailable [{}] {}",
				error.code.empty() ? "python.bridge.unavailable" : error.code,
				error.technicalDetails);
		}
		else
		{
			DS_LOG_INFO("ScientificRuntimeLayer: Python bridge initialized");
		}

		DS_LOG_INFO("ScientificRuntimeLayer attached");
	}

	void ScientificRuntimeLayer::OnDetach()
	{
		if (m_PythonRuntime != nullptr)
		{
			m_PythonRuntime->Shutdown();
			m_PythonRuntime.reset();
		}

		DS_LOG_INFO("ScientificRuntimeLayer detached");
	}

	void ScientificRuntimeLayer::OnUpdate(float deltaTime)
	{
		(void)deltaTime;
	}

	void ScientificRuntimeLayer::RegisterCapability(CapabilityService &capabilityService, CapabilityEntry capability) const
	{
		capabilityService.RegisterCapability(std::move(capability));
	}

	CapabilityEntry ScientificRuntimeLayer::BuildPythonBridgeCapability() const
	{
		CapabilityEntry capability;
		capability.name = PythonBridgeCapabilityId;
		capability.category = CapabilityCategory::RuntimeDetected;
		capability.available = m_PythonBridgeAvailable;
		capability.description = "Scientific Python runtime bridge (nanobind + script adapters)";
		return capability;
	}
} // namespace DefectStudio
