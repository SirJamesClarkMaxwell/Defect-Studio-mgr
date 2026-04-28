#pragma once

#include <array>
#include <string>

#include "App/Managers/ConfigProfileStore.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
	struct EditorUiState;

	class SettingsProfileManager
	{
	public:
		void Bind(WeakRef<ConfigManager> configManager, Ref<EventBus> eventBus);
		[[nodiscard]] bool Render(EditorUiState *uiState, const ApplicationConfig &currentConfig);

	private:
		void refreshIfNeeded();
		[[nodiscard]] const ConfigProfileEntry *selectedProfile() const;

	private:
		ConfigProfileStore m_Store;
		Ref<EventBus> m_EventBus;
		std::array<char, 96> m_ProfileNameBuffer = {"default"};
		int m_SelectedProfileIndex = 0;
		bool m_Initialized = false;
		std::string m_StatusMessage;
	};
} // namespace DefectStudio
