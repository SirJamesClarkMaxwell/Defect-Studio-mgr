#include "Core/dspch.hpp"

#include "ScientificRuntime/ScientificRuntimeLayer.hpp"

#include "Core/Capabilities/Capability.hpp"
#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Logging/Logger.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"
#include "ScientificRuntime/Python/PythonGilScope.hpp"
#include "ScientificRuntime/Python/PythonScriptJob.hpp"
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

		registerJobSystemHooks();
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

	void ScientificRuntimeLayer::BindJobSystem(WeakRef<JobSystem> jobSystem)
	{
		m_JobSystem = std::move(jobSystem);
		registerJobSystemHooks();
	}

	void ScientificRuntimeLayer::OnUpdate(float deltaTime)
	{
		(void)deltaTime;
	}

	void ScientificRuntimeLayer::registerJobSystemHooks()
	{
		if (m_JobHookRegistered)
			return;

		auto jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		jobSystem->RegisterPreExecuteHook([](const IJob &job) -> Unique<IJobExecutionGuard> {
			const auto *pythonJob = dynamic_cast<const PythonScriptJob *>(&job);
			if (pythonJob != nullptr && pythonJob->UsesPythonRuntime())
				return CreateUnique<PythonGilAcquireScope>();
			return {};
		});
		m_JobHookRegistered = true;
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

	bool ScientificRuntimeLayer::IsPythonBridgeAvailable() const noexcept
	{
		return m_PythonBridgeAvailable;
	}

	Result<CrystalStructure> ScientificRuntimeLayer::LoadCrystalStructure(const Path &filePath) const
	{
		if (!m_PythonBridgeAvailable || m_PythonRuntime == nullptr)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"Python bridge is unavailable.",
				"ScientificRuntimeLayer::LoadCrystalStructure called without an initialized Python runtime.",
				"Ensure the bundled Python environment initializes successfully before loading structures.",
				"ScientificRuntime",
				"python.bridge.unavailable"};
		}
		return m_PythonRuntime->LoadCrystalStructure(filePath);
	}

	Result<std::vector<CrystalStructure>> ScientificRuntimeLayer::LoadCrystalStructures(
		const std::vector<Path> &filePaths) const
	{
		if (!m_PythonBridgeAvailable || m_PythonRuntime == nullptr)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"Python bridge is unavailable.",
				"ScientificRuntimeLayer::LoadCrystalStructures called without an initialized Python runtime.",
				"Ensure the bundled Python environment initializes successfully before loading structures.",
				"ScientificRuntime",
				"python.bridge.unavailable"};
		}
		return m_PythonRuntime->LoadCrystalStructures(filePaths);
	}
} // namespace DefectStudio
