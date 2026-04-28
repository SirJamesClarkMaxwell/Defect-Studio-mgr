#include "Core/dspch.hpp"

#include "App/Controllers/ApplicationEventController.hpp"

#include "App/Controllers/ApplicationConfigController.hpp"
#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	ApplicationEventController::ApplicationEventController(
		Ref<EventBus> eventBus,
		Ref<ConfigManager> configManager,
		ApplicationConfig &config,
		ApplicationSpecification &specification,
		EventQueue &eventQueue)
	{
		m_ConfigController = CreateUnique<ApplicationConfigController>(
			std::move(eventBus),
			std::move(configManager),
			config,
			specification,
			eventQueue);
		DS_LOG_INFO("ApplicationEventController initialized");
	}

	ApplicationEventController::~ApplicationEventController()
	{
		m_ConfigController.reset();
		DS_LOG_INFO("ApplicationEventController shut down");
	}
} // namespace DefectStudio
