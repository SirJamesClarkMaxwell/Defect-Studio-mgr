#pragma once

#include <array>
#include <string>
#include <vector>

#include "Core/Configuration/ConfigProfile.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
	struct ApplicationConfig;
	struct EditorUiState;

	namespace AppEvents::Config
	{
		struct ProfileListLoaded;
		struct ProfileListFailed;
		struct ProfileSaved;
		struct ProfileSaveFailed;
		struct ProfileLoaded;
		struct ProfileLoadFailed;
		struct ProfileExported;
		struct ProfileExportFailed;
	}

	class SettingsProfileManager final : public EventReceiver
	{
	public:
		void Bind(Ref<EventBus> eventBus);
		[[nodiscard]] bool Render(EditorUiState *uiState, const ApplicationConfig &currentConfig);

	private:
		void refreshIfNeeded();
		[[nodiscard]] const ConfigProfileEntry *selectedProfile() const;
		void onProfileListLoaded(const AppEvents::Config::ProfileListLoaded &event);
		void onProfileListFailed(const AppEvents::Config::ProfileListFailed &event);
		void onProfileSaved(const AppEvents::Config::ProfileSaved &event);
		void onProfileSaveFailed(const AppEvents::Config::ProfileSaveFailed &event);
		void onProfileLoaded(const AppEvents::Config::ProfileLoaded &event);
		void onProfileLoadFailed(const AppEvents::Config::ProfileLoadFailed &event);
		void onProfileExported(const AppEvents::Config::ProfileExported &event);
		void onProfileExportFailed(const AppEvents::Config::ProfileExportFailed &event);

	private:
		Ref<EventBus> m_EventBus;
		std::vector<ConfigProfileEntry> m_Entries;
		std::array<char, 96> m_ProfileNameBuffer = {"default"};
		int m_SelectedProfileIndex = 0;
		bool m_Initialized = false;
		std::string m_StatusMessage;
	};
} // namespace DefectStudio
