#pragma once

#include "App/ApplicationState.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class ApplicationConfigController;
	class ConfigManager;
	class EventBus;
	class EventQueue;
	class LogRegistry;

	namespace AppEvents
	{
		struct ProjectSaveRequested;
		struct ShutdownRequested;
	}

	class ApplicationEventController final : public EventReceiver
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
		void bindEvents();
		void onShutdownRequested(const AppEvents::ShutdownRequested &event);
		void onProjectSaveRequested(const AppEvents::ProjectSaveRequested &event);

	private:
		Ref<EventBus> m_EventBus;
		EventQueue &m_EventQueue;
		Unique<ApplicationConfigController> m_ConfigController;
	};
} // namespace DefectStudio
