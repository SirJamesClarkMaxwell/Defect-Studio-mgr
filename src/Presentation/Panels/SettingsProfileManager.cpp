#include "Core/dspch.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "App/Application.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/Panels/SettingsProfileManager.hpp"

namespace DefectStudio
{
	void SettingsProfileManager::Bind(WeakRef<ConfigManager> configManager)
	{
		m_Store.Bind(std::move(configManager));
		m_Initialized = false;
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
			std::string error;
			if (m_Store.Save(m_ProfileNameBuffer.data(), currentConfig, error))
				m_StatusMessage = "Profile saved: " + std::string(m_ProfileNameBuffer.data());
			else
				m_StatusMessage = "Profile save failed: " + error;
		}

		ImGui::SameLine();
		if (ImGui::Button("Refresh"))
		{
			m_Initialized = false;
			refreshIfNeeded();
		}

		const auto &entries = m_Store.Entries();
		if (entries.empty())
		{
			ImGui::TextDisabled("No profiles yet.");
		}
		else
		{
			m_SelectedProfileIndex = std::clamp(m_SelectedProfileIndex, 0, static_cast<int>(entries.size()) - 1);
			const std::string currentName = entries[static_cast<std::size_t>(m_SelectedProfileIndex)].name;
			if (ImGui::BeginCombo("Existing profiles", currentName.c_str()))
			{
				for (std::size_t index = 0; index < entries.size(); ++index)
				{
					const bool selected = static_cast<int>(index) == m_SelectedProfileIndex;
					if (ImGui::Selectable(entries[index].name.c_str(), selected))
					{
						m_SelectedProfileIndex = static_cast<int>(index);
						std::snprintf(m_ProfileNameBuffer.data(), m_ProfileNameBuffer.size(), "%s", entries[index].name.c_str());
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			if (ImGui::Button("Load selected"))
			{
				ApplicationConfig config;
				std::string error;
				const ConfigProfileEntry *profile = selectedProfile();
				const std::string profileName = profile != nullptr ? profile->name : m_ProfileNameBuffer.data();
				if (profile != nullptr && m_Store.Load(profile->path, config, error) && Application::Get().ApplyConfigFromSettings(config, error, true))
				{
					m_StatusMessage = "Profile loaded: " + profileName;
					appliedProfile = true;
					if (uiState != nullptr)
						uiState->appearanceStatusMessage = m_StatusMessage;
				}
				else
				{
					m_StatusMessage = "Profile load failed: " + error;
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Export selected"))
			{
				std::string error;
				const ConfigProfileEntry *profile = selectedProfile();
				const std::string profileName = profile != nullptr ? profile->name : m_ProfileNameBuffer.data();
				if (profile != nullptr && m_Store.Export(profile->path, error))
					m_StatusMessage = "Profile exported: " + profileName;
				else
					m_StatusMessage = "Profile export failed: " + error;
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

		(void)m_Store.Refresh();
		m_SelectedProfileIndex = std::clamp(m_SelectedProfileIndex, 0, std::max(0, static_cast<int>(m_Store.Entries().size()) - 1));
		m_Initialized = true;
	}

	const ConfigProfileEntry *SettingsProfileManager::selectedProfile() const
	{
		const auto &entries = m_Store.Entries();
		if (!entries.empty() && m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < static_cast<int>(entries.size()))
			return &entries[static_cast<std::size_t>(m_SelectedProfileIndex)];

		return nullptr;
	}
} // namespace DefectStudio
