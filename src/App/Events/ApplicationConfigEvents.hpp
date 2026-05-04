#pragma once

#include <string>
#include <utility>
#include <vector>

#include "App/ApplicationState.hpp"
#include "Core/Configuration/ConfigProfile.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::AppEvents::Config
{
	enum class PersistKind
	{
		UserSettings,
		Defaults,
	};

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

	struct PersistRequested final : public BusEvent
	{
		PersistRequested(PersistKind persistKind, ApplicationConfig applicationConfig, Path targetPath, std::string fileContents)
			: kind(persistKind),
			  config(std::move(applicationConfig)),
			  path(std::move(targetPath)),
			  contents(std::move(fileContents))
		{
		}

		PersistKind kind = PersistKind::UserSettings;
		ApplicationConfig config;
		Path path;
		std::string contents;
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

	struct ProfileListRequested final : public BusEvent
	{
	};

	struct ProfileListLoaded final : public BusEvent
	{
		explicit ProfileListLoaded(std::vector<ConfigProfileEntry> profileEntries)
			: entries(std::move(profileEntries))
		{
		}

		std::vector<ConfigProfileEntry> entries;
	};

	struct ProfileListFailed final : public BusEvent
	{
		explicit ProfileListFailed(std::string failureReason)
			: error(std::move(failureReason))
		{
		}

		std::string error;
	};

	struct ProfileSaveRequested final : public BusEvent
	{
		ProfileSaveRequested(std::string profileName, ApplicationConfig applicationConfig)
			: name(std::move(profileName)), config(std::move(applicationConfig))
		{
		}

		std::string name;
		ApplicationConfig config;
	};

	struct ProfileSaved final : public BusEvent
	{
		ProfileSaved(std::string profileName, Path profilePath)
			: name(std::move(profileName)), path(std::move(profilePath))
		{
		}

		std::string name;
		Path path;
	};

	struct ProfileSaveFailed final : public BusEvent
	{
		ProfileSaveFailed(std::string profileName, std::string failureReason)
			: name(std::move(profileName)), error(std::move(failureReason))
		{
		}

		std::string name;
		std::string error;
	};

	struct ProfileLoadRequested final : public BusEvent
	{
		ProfileLoadRequested(std::string profileName, Path profilePath)
			: name(std::move(profileName)), path(std::move(profilePath))
		{
		}

		std::string name;
		Path path;
	};

	struct ProfileLoaded final : public BusEvent
	{
		ProfileLoaded(std::string profileName, ApplicationConfig applicationConfig)
			: name(std::move(profileName)), config(std::move(applicationConfig))
		{
		}

		std::string name;
		ApplicationConfig config;
	};

	struct ProfileLoadFailed final : public BusEvent
	{
		ProfileLoadFailed(std::string profileName, std::string failureReason)
			: name(std::move(profileName)), error(std::move(failureReason))
		{
		}

		std::string name;
		std::string error;
	};

	struct ProfileExportRequested final : public BusEvent
	{
		ProfileExportRequested(std::string profileName, Path profilePath)
			: name(std::move(profileName)), path(std::move(profilePath))
		{
		}

		std::string name;
		Path path;
	};

	struct ProfileExported final : public BusEvent
	{
		ProfileExported(std::string profileName, Path exportPath)
			: name(std::move(profileName)), path(std::move(exportPath))
		{
		}

		std::string name;
		Path path;
	};

	struct ProfileExportFailed final : public BusEvent
	{
		ProfileExportFailed(std::string profileName, std::string failureReason)
			: name(std::move(profileName)), error(std::move(failureReason))
		{
		}

		std::string name;
		std::string error;
	};

} // namespace DefectStudio::AppEvents::Config
