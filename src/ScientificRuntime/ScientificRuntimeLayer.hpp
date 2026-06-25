#pragma once

#include "Core/Capabilities/Capability.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CapabilityService;
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

		void RegisterCapability(CapabilityService &capabilityService, CapabilityEntry capability) const;
		[[nodiscard]] CapabilityEntry BuildPythonBridgeCapability() const;
		[[nodiscard]] bool IsPythonBridgeAvailable() const noexcept;
		[[nodiscard]] PymatgenBridge *GetPymatgenBridge() const;

	private:
		Unique<ScientificPythonRuntime> m_PythonRuntime;
		bool m_PythonBridgeAvailable = false;
	};
} // namespace DefectStudio
