#pragma once

#include "App/ApplicationState.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class ApplicationConfigController;
	class ConfigManager;
	class EventBus;
	class EventQueue;
	class LogRegistry;

	class ApplicationEventController final
	{
	public:
		ApplicationEventController(
			Ref<EventBus> eventBus,
			Ref<ConfigManager> configManager,
			ApplicationConfig &config,
			ApplicationSpecification &specification,
			EventQueue &eventQueue,
			Ref<LogRegistry> logRegistry = {});
		~ApplicationEventController();

		ApplicationEventController(const ApplicationEventController &) = delete;
		ApplicationEventController &operator=(const ApplicationEventController &) = delete;
		ApplicationEventController(ApplicationEventController &&) = delete;
		ApplicationEventController &operator=(ApplicationEventController &&) = delete;

	private:
		Unique<ApplicationConfigController> m_ConfigController;
	};
} // namespace DefectStudio
