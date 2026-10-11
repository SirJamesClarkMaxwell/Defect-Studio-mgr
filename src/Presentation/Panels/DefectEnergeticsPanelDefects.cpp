#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <imgui.h>

#include "Core/Platform/FileDialog.hpp"

namespace DefectStudio
{
	void DefectEnergeticsPanel::drawDefectSet()
	{
		if (ImGui::Button("Dodaj defekt z folderu..."))
		{
			const auto picked = Platform::PickFolder(m_Session->m_File->root);
			if (!picked)
				m_Session->m_Message = picked.Error().userMessage;
			else if (picked->has_value())
				m_Session->AddDefectFolder(**picked);
		}

		const auto &roots = m_Session->m_File->extraRoots;
		for (std::size_t i = 0; i < roots.size(); ++i)
		{
			const std::string label = roots[i].Utf8();
			ImGui::PushID(label.c_str());
			const bool remove = ImGui::SmallButton("x");
			ImGui::SameLine();
			ImGui::TextWrapped("%s", label.c_str());
			ImGui::PopID();
			if (remove)
			{
				m_Session->RemoveExtraRoot(roots[i]);
				break;
			}
		}

		const auto &hidden = m_Session->m_File->hiddenDefects;
		if (!hidden.empty())
			ImGui::TextUnformatted("Ukryte defekty:");
		for (std::size_t i = 0; i < hidden.size(); ++i)
		{
			ImGui::PushID(hidden[i].c_str());
			const bool restore = ImGui::SmallButton("przywróć");
			ImGui::SameLine();
			ImGui::TextUnformatted(hidden[i].c_str());
			ImGui::PopID();
			if (restore)
			{
				m_Session->RestoreDefect(hidden[i]);
				break;
			}
		}
	}

	void DefectEnergeticsPanel::drawDefectContextMenu(const std::string &defect)
	{
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Usuń z analizy"))
				m_Session->HideDefect(defect);
			ImGui::EndPopup();
		}
	}
} // namespace DefectStudio
