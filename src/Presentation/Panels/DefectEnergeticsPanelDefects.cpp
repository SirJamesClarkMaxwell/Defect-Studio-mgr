#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <algorithm>

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
				const Path root = roots[i]; // a copy: the call erases the element
				m_Session->RemoveExtraRoot(root);
				break;
			}
		}

		ImGui::SameLine();
		ImGui::TextDisabled("(albo prawy klik na folderze w drzewie projektu)");
		if (const auto &removed = m_Session->m_File->removedDefects; !removed.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("· usunięte: %zu", removed.size());
			if (ImGui::BeginItemTooltip())
			{
				for (const auto &name : removed)
					ImGui::BulletText("%s", name.c_str());
				ImGui::TextDisabled("Dodaj ich folder ponownie, by przywrócić.");
				ImGui::EndTooltip();
			}
		}
	}

	void DefectEnergeticsPanel::drawDefectContextMenu(const std::string &defect)
	{
		if (!ImGui::BeginPopupContextItem())
			return;
		const auto &hidden = m_Session->m_File->hiddenDefects;
		if (std::find(hidden.begin(), hidden.end(), defect) != hidden.end())
		{
			if (ImGui::MenuItem("Pokaż"))
				m_Session->RestoreDefect(defect);
		}
		else if (ImGui::MenuItem("Ukryj"))
			m_Session->HideDefect(defect);
		ImGui::SetItemTooltip("Zostaje w tabeli (przygaszony, na dole), poza wykresami, koncentracjami i eksportem.");
		if (ImGui::MenuItem("Usuń z analizy"))
			m_Session->RemoveDefect(defect);
		ImGui::SetItemTooltip("Znika z tabeli. Przywrócisz go, dodając jego folder ponownie (drzewo projektu albo przycisk).");
		ImGui::EndPopup();
	}
} // namespace DefectStudio
