#pragma once

#include <vector>

#include "Core/Capabilities/Capability.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	class CapabilityService;
	class JobSystem;
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
		[[nodiscard]] Result<CrystalStructure> LoadCrystalStructure(const Path &filePath) const;
		[[nodiscard]] Result<std::vector<CrystalStructure>> LoadCrystalStructures(const std::vector<Path> &filePaths) const;

	private:
		void registerJobSystemHooks();

		Unique<ScientificPythonRuntime> m_PythonRuntime;
		WeakRef<JobSystem> m_JobSystem;
		bool m_PythonBridgeAvailable = false;
		bool m_JobHookRegistered = false;
	};
} // namespace DefectStudio
