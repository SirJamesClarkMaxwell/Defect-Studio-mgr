#pragma once

#include <string>
#include <utility>

#include "App/ApplicationState.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"

namespace DefectStudio::AppEvents::Config
{
	struct ApplyRequested final : public BusEvent
	{
		ApplyRequested(ApplicationConfig applicationConfig, bool shouldPersist)
			: config(std::move(applicationConfig)), persist(shouldPersist)
		{
		}

		ApplicationConfig config;
		bool persist = false;
	};

	struct Applied final : public BusEvent
	{
		Applied(ApplicationConfig applicationConfig, bool wasPersisted)
			: config(std::move(applicationConfig)), persisted(wasPersisted)
		{
		}

		ApplicationConfig config;
		bool persisted = false;
	};

	struct ApplyFailed final : public BusEvent
	{
		ApplyFailed(ApplicationConfig applicationConfig, bool shouldPersist, std::string failureReason)
			: config(std::move(applicationConfig)), persist(shouldPersist), error(std::move(failureReason))
		{
		}

		ApplicationConfig config;
		bool persist = false;
		std::string error;
	};

	struct SaveUserRequested final : public BusEvent
	{
		explicit SaveUserRequested(ApplicationConfig applicationConfig)
			: config(std::move(applicationConfig))
		{
		}

		ApplicationConfig config;
	};

	struct UserSaved final : public BusEvent
	{
		explicit UserSaved(ApplicationConfig applicationConfig)
			: config(std::move(applicationConfig))
		{
		}

		ApplicationConfig config;
	};

	struct UserSaveFailed final : public BusEvent
	{
		UserSaveFailed(ApplicationConfig applicationConfig, std::string failureReason)
			: config(std::move(applicationConfig)), error(std::move(failureReason))
		{
		}

		ApplicationConfig config;
		std::string error;
	};

	struct SaveDefaultsRequested final : public BusEvent
	{
		explicit SaveDefaultsRequested(ApplicationConfig applicationConfig)
			: config(std::move(applicationConfig))
		{
		}

		ApplicationConfig config;
	};

	struct DefaultsSaved final : public BusEvent
	{
		explicit DefaultsSaved(ApplicationConfig applicationConfig)
			: config(std::move(applicationConfig))
		{
		}

		ApplicationConfig config;
	};

	struct DefaultsSaveFailed final : public BusEvent
	{
		DefaultsSaveFailed(ApplicationConfig applicationConfig, std::string failureReason)
			: config(std::move(applicationConfig)), error(std::move(failureReason))
		{
		}

		ApplicationConfig config;
		std::string error;
	};
} // namespace DefectStudio::AppEvents::Config
