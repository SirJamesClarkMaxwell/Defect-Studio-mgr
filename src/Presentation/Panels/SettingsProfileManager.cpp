#include "Core/dspch.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>

#include <imgui.h>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/Panels/SettingsProfileManager.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeSettingsProfileManager(
			EventBus &bus,
			SettingsProfileManager &manager,
			void (SettingsProfileManager::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &manager));
		}
	}

	void SettingsProfileManager::Bind(Ref<EventBus> eventBus)
	{
		if (m_EventBus == eventBus)
			return;

		ClearSubscriptions();
		m_EventBus = std::move(eventBus);
		m_Initialized = false;

		if (m_EventBus == nullptr)
			return;

		using namespace AppEvents::Config;
		AddSubscription(subscribeSettingsProfileManager<ProfileListLoaded>(*m_EventBus, *this, &SettingsProfileManager::onProfileListLoaded));
		AddSubscription(subscribeSettingsProfileManager<ProfileListFailed>(*m_EventBus, *this, &SettingsProfileManager::onProfileListFailed));
		AddSubscription(subscribeSettingsProfileManager<ProfileSaved>(*m_EventBus, *this, &SettingsProfileManager::onProfileSaved));
		AddSubscription(subscribeSettingsProfileManager<ProfileSaveFailed>(*m_EventBus, *this, &SettingsProfileManager::onProfileSaveFailed));
		AddSubscription(subscribeSettingsProfileManager<ProfileLoaded>(*m_EventBus, *this, &SettingsProfileManager::onProfileLoaded));
		AddSubscription(subscribeSettingsProfileManager<ProfileLoadFailed>(*m_EventBus, *this, &SettingsProfileManager::onProfileLoadFailed));
		AddSubscription(subscribeSettingsProfileManager<ProfileExported>(*m_EventBus, *this, &SettingsProfileManager::onProfileExported));
		AddSubscription(subscribeSettingsProfileManager<ProfileExportFailed>(*m_EventBus, *this, &SettingsProfileManager::onProfileExportFailed));
	}

	bool SettingsProfileManager::Render(EditorUiState *uiState, const ApplicationConfig &currentConfig)
	{
		bool appliedProfile = false;
		refreshIfNeeded();

		ImGui::TextUnformatted("Configuration profiles");
		ImGui::TextWrapped("Profiles are YAML snapshots of application settings. Files stay in the portable user config folder.");

		ImGui::InputText("Profile name", m_ProfileNameBuffer.data(), m_ProfileNameBuffer.size());
		if (ImGui::Button("Save current"))
		{
			if (m_EventBus != nullptr)
			{
				m_EventBus->Queue(AppEvents::Config::ProfileSaveRequested{m_ProfileNameBuffer.data(), currentConfig});
				m_StatusMessage = "Profile save queued: " + std::string(m_ProfileNameBuffer.data());
			}
			else
			{
				m_StatusMessage = "Profile save failed: EventBus unavailable.";
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Refresh"))
		{
			m_Initialized = false;
			refreshIfNeeded();
		}

		if (m_Entries.empty())
		{
			ImGui::TextDisabled("No profiles yet.");
		}
		else
		{
			m_SelectedProfileIndex = std::clamp(m_SelectedProfileIndex, 0, static_cast<int>(m_Entries.size()) - 1);
			const std::string currentName = m_Entries[static_cast<std::size_t>(m_SelectedProfileIndex)].name;
			if (ImGui::BeginCombo("Existing profiles", currentName.c_str()))
			{
				for (std::size_t index = 0; index < m_Entries.size(); ++index)
				{
					const bool selected = static_cast<int>(index) == m_SelectedProfileIndex;
					if (ImGui::Selectable(m_Entries[index].name.c_str(), selected))
					{
						m_SelectedProfileIndex = static_cast<int>(index);
						std::snprintf(m_ProfileNameBuffer.data(), m_ProfileNameBuffer.size(), "%s", m_Entries[index].name.c_str());
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			if (ImGui::Button("Load selected"))
			{
				using namespace AppEvents::Config;

				const ConfigProfileEntry *profile = selectedProfile();
				const std::string profileName = profile != nullptr ? profile->name : m_ProfileNameBuffer.data();
				if (profile != nullptr && m_EventBus != nullptr)
				{
					m_EventBus->Queue(ProfileLoadRequested{profileName, profile->path});
					m_StatusMessage = "Profile apply queued: " + profileName;
					appliedProfile = true;
					if (uiState != nullptr)
						uiState->appearanceStatusMessage = m_StatusMessage;
				}
				else
				{
					m_StatusMessage = m_EventBus == nullptr
						? "Profile load failed: EventBus unavailable."
						: "Profile load failed: profile unavailable.";
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Export selected"))
			{
				const ConfigProfileEntry *profile = selectedProfile();
				const std::string profileName = profile != nullptr ? profile->name : m_ProfileNameBuffer.data();
				if (profile != nullptr && m_EventBus != nullptr)
				{
					m_EventBus->Queue(AppEvents::Config::ProfileExportRequested{profileName, profile->path});
					m_StatusMessage = "Profile export queued: " + profileName;
				}
				else
				{
					m_StatusMessage = m_EventBus == nullptr
						? "Profile export failed: EventBus unavailable."
						: "Profile export failed: profile unavailable.";
				}
			}
		}

		if (!m_StatusMessage.empty())
			ImGui::TextWrapped("%s", m_StatusMessage.c_str());

		return appliedProfile;
	}

	void SettingsProfileManager::refreshIfNeeded()
	{
		if (m_Initialized)
			return;

		if (m_EventBus != nullptr)
			m_EventBus->Queue(AppEvents::Config::ProfileListRequested{});
		m_Initialized = true;
	}

	const ConfigProfileEntry *SettingsProfileManager::selectedProfile() const
	{
		if (!m_Entries.empty() && m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < static_cast<int>(m_Entries.size()))
			return &m_Entries[static_cast<std::size_t>(m_SelectedProfileIndex)];

		return nullptr;
	}

	void SettingsProfileManager::onProfileListLoaded(const AppEvents::Config::ProfileListLoaded &event)
	{
		m_Entries = event.entries;
		m_SelectedProfileIndex = std::clamp(m_SelectedProfileIndex, 0, std::max(0, static_cast<int>(m_Entries.size()) - 1));
	}

	void SettingsProfileManager::onProfileListFailed(const AppEvents::Config::ProfileListFailed &event)
	{
		m_StatusMessage = "Profile refresh failed: " + event.error;
	}

	void SettingsProfileManager::onProfileSaved(const AppEvents::Config::ProfileSaved &event)
	{
		m_StatusMessage = "Profile saved: " + event.name;
		m_Initialized = false;
	}

	void SettingsProfileManager::onProfileSaveFailed(const AppEvents::Config::ProfileSaveFailed &event)
	{
		m_StatusMessage = "Profile save failed: " + event.error;
	}

	void SettingsProfileManager::onProfileLoaded(const AppEvents::Config::ProfileLoaded &event)
	{
		(void)event.config;
		m_StatusMessage = "Profile apply queued: " + event.name;
	}

	void SettingsProfileManager::onProfileLoadFailed(const AppEvents::Config::ProfileLoadFailed &event)
	{
		m_StatusMessage = "Profile load failed: " + event.error;
	}

	void SettingsProfileManager::onProfileExported(const AppEvents::Config::ProfileExported &event)
	{
		m_StatusMessage = "Profile exported: " + event.name;
	}

	void SettingsProfileManager::onProfileExportFailed(const AppEvents::Config::ProfileExportFailed &event)
	{
		m_StatusMessage = "Profile export failed: " + event.error;
	}
} // namespace DefectStudio
