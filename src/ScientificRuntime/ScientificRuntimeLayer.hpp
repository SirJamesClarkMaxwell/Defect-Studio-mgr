#pragma once

#include "Core/Capabilities/Capability.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CapabilityService;
	class JobSystem;
	class PymatgenBridge;
	class ScientificPythonRuntime;

	class ScientificRuntimeLayer final : public Layer
	{
	public:
		ScientificRuntimeLayer();
		~ScientificRuntimeLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

		void BindJobSystem(WeakRef<JobSystem> jobSystem);
		void RegisterCapability(CapabilityService &capabilityService, CapabilityEntry capability) const;
		[[nodiscard]] CapabilityEntry BuildPythonBridgeCapability() const;
		[[nodiscard]] bool IsPythonBridgeAvailable() const noexcept;
		[[nodiscard]] PymatgenBridge *GetPymatgenBridge() const;

	private:
		void registerJobSystemHooks();

		Unique<ScientificPythonRuntime> m_PythonRuntime;
		WeakRef<JobSystem> m_JobSystem;
		bool m_PythonBridgeAvailable = false;
		bool m_JobHookRegistered = false;
	};
} // namespace DefectStudio
