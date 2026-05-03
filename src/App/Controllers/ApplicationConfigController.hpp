#pragma once

#include <string>

#include "App/ApplicationState.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class ConfigManager;
	class EventBus;
	class EventQueue;
	class LogRegistry;

	namespace AppEvents::Config
	{
		struct ApplyRequested;
		struct SaveUserRequested;
		struct SaveDefaultsRequested;
	}

	class ApplicationConfigController final : public EventReceiver
	{
	public:
		ApplicationConfigController(
			Ref<EventBus> eventBus,
			Ref<ConfigManager> configManager,
			ApplicationConfig &config,
			ApplicationSpecification &specification,
			EventQueue &eventQueue,
			Ref<LogRegistry> logRegistry = {});
		~ApplicationConfigController() override;

		ApplicationConfigController(const ApplicationConfigController &) = delete;
		ApplicationConfigController &operator=(const ApplicationConfigController &) = delete;
		ApplicationConfigController(ApplicationConfigController &&) = delete;
		ApplicationConfigController &operator=(ApplicationConfigController &&) = delete;

	private:
		void bindEvents();
		void initializeLogger() const;

		[[nodiscard]] ApplicationConfig normalizeConfigSnapshot(const ApplicationConfig &config) const;
		[[nodiscard]] bool applyConfigRequest(const ApplicationConfig &config, ApplicationConfig &appliedConfig, std::string &error);

		void onConfigApplyRequested(const AppEvents::Config::ApplyRequested &event);
		void onUserConfigSaveRequested(const AppEvents::Config::SaveUserRequested &event);
		void onDefaultsSaveRequested(const AppEvents::Config::SaveDefaultsRequested &event);

	private:
		Ref<EventBus> m_EventBus;
		Ref<ConfigManager> m_ConfigManager;
		ApplicationConfig &m_Config;
		ApplicationSpecification &m_Specification;
		EventQueue &m_EventQueue;
		Ref<LogRegistry> m_LogRegistry;
	};
} // namespace DefectStudio
