#include "Core/dspch.hpp"

#include "App/Controllers/ApplicationEventController.hpp"

#include "App/Controllers/ApplicationConfigController.hpp"
#include "App/Events/ApplicationEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/EventQueue.hpp"
#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeApplicationEventController(
			EventBus &bus,
			ApplicationEventController &controller,
			void (ApplicationEventController::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &controller));
		}
	}

	ApplicationEventController::ApplicationEventController(
		Ref<EventBus> eventBus,
		Ref<ConfigManager> configManager,
		ApplicationConfig &config,
		ApplicationSpecification &specification,
		EventQueue &eventQueue,
		Ref<LogRegistry> logRegistry)
		: m_EventBus(std::move(eventBus)),
		  m_EventQueue(eventQueue)
	{
		m_ConfigController = CreateUnique<ApplicationConfigController>(
			m_EventBus,
			std::move(configManager),
			config,
			specification,
			eventQueue,
			std::move(logRegistry));
		bindEvents();
		DS_LOG_INFO("ApplicationEventController initialized");
	}

	ApplicationEventController::~ApplicationEventController()
	{
		ClearSubscriptions();
		m_ConfigController.reset();
		m_EventBus.reset();
		DS_LOG_INFO("ApplicationEventController shut down");
	}

	void ApplicationEventController::bindEvents()
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(subscribeApplicationEventController<AppEvents::ShutdownRequested>(
			*m_EventBus,
			*this,
			&ApplicationEventController::onShutdownRequested));
		AddSubscription(subscribeApplicationEventController<AppEvents::ProjectSaveRequested>(
			*m_EventBus,
			*this,
			&ApplicationEventController::onProjectSaveRequested));
	}

	void ApplicationEventController::onShutdownRequested(const AppEvents::ShutdownRequested &)
	{
		m_EventQueue.Add(WindowCloseEvent{});
		DS_LOG_INFO("Application shutdown requested through command system");
	}

	void ApplicationEventController::onProjectSaveRequested(const AppEvents::ProjectSaveRequested &)
	{
		DS_LOG_INFO("Project save requested through command system");
	}
} // namespace DefectStudio
