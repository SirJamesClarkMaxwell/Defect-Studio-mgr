#pragma once

#include <string>

#include "Core/Capabilities/Capability.hpp"
#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
}

namespace DefectStudio::Demo
{
	class DemoCapabilitiesPanel
	{
	public:
		DemoCapabilitiesPanel(Ref<CapabilityService> capabilityService, Ref<EventBus> eventBus);

		void Render();

	private:
		void renderRegistrySnapshot() const;
		void renderRegistrationDemo();
		void registerCapability(std::string name, bool available, std::string description);
		void requireCapability(const std::string &name);
		void notifyInfo(std::string title, std::string message);
		void notifyError(std::string title, std::string message);

		[[nodiscard]] static const char *categoryName(CapabilityCategory category);

	private:
		Ref<CapabilityService> m_CapabilityService;
		Ref<EventBus> m_EventBus;

		std::string m_LastCapabilityDemoAction = "No local capability registered yet.";
	};
} // namespace DefectStudio::Demo
