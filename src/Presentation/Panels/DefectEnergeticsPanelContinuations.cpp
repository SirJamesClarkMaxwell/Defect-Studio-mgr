#include "Core/dspch.hpp"
#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <algorithm>
#include <format>
#include <imgui.h>
#include <imgui_internal.h>

namespace DefectStudio
{
	void DefectEnergeticsPanel::drawMuCombo()
	{
		const auto &sets = m_Session->m_Model.muSets;
		if (sets.empty())
			return;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##ActiveMu", sets[static_cast<std::size_t>(m_Session->m_MuSet)].name.c_str()))
		{
			for (std::size_t i = 0; i < sets.size(); ++i)
				if (ImGui::Selectable(sets[i].name.c_str(), m_Session->m_MuSet == static_cast<int>(i)))
				{
					m_Session->m_MuSet = static_cast<int>(i);
					m_Session->InvalidateInputs();
				}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Aktywny zestaw μ: tabela energii, wykres i koncentracje bez linii μ.");
	}

	void DefectEnergeticsPanel::drawContinuations()
	{
		if (ImGui::Button("Kontynuacje"))
			ImGui::OpenPopup("##Continuations");
		if (!ImGui::BeginPopup("##Continuations"))
			return;
		const auto &model = m_Session->m_Model;
		const auto &file = *m_Session->m_File;
		auto &selected = m_Session->m_Continuations;
		const auto &set = model.muSets[static_cast<std::size_t>(m_Session->m_MuSet)];
		const auto mu = AbsoluteChemicalPotentials(model.chemicalPotentials, set.deltaMu);
		const auto eligible = [&](const EnergeticsRow &row) {
			const auto state = EnergeticsRowState(file, row);
			return row.included && state && FormationEnergy(*state, model.reference, mu, 0.0).has_value();
		};
		for (std::size_t first = 0; first < model.rows.size();)
		{
			const auto &defect = model.rows[first].defect;
			std::size_t last = first, count = 0, on = 0;
			while (last < model.rows.size() && model.rows[last].defect == defect)
			{
				if (eligible(model.rows[last]))
				{
					++count;
					on += selected.contains({defect, model.rows[last].charge}) ? 1 : 0;
				}
				++last;
			}
			ImGui::PushID(defect.c_str());
			bool all = count > 0 && on == count;
			ImGui::BeginDisabled(count == 0);
			ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, on > 0 && on < count);
			if (ImGui::Checkbox("##all", &all))
				for (std::size_t i = first; i < last; ++i)
					if (eligible(model.rows[i]))
					{
						const auto key = std::pair{defect, model.rows[i].charge};
						if (on == count)
							selected.erase(key);
						else
							selected.insert(key);
					}
			ImGui::PopItemFlag();
			ImGui::EndDisabled();
			if (count == 0)
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Brak uwzględnionych, policzalnych stanów.");
			ImGui::SameLine();
			if (ImGui::TreeNode(defect.c_str()))
			{
				for (std::size_t i = first; i < last; ++i)
				{
					const auto &row = model.rows[i];
					const bool usable = eligible(row);
					bool onState = selected.contains({defect, row.charge});
					ImGui::BeginDisabled(!usable);
					if (ImGui::Checkbox(std::format("q = {:+d}", row.charge).c_str(), &onState))
					{
						const auto key = std::pair{defect, row.charge};
						if (onState)
							selected.insert(key);
						else
							selected.erase(key);
					}
					ImGui::EndDisabled();
					if (!usable)
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
							ImGui::SetTooltip("%s", !row.included ? "Stan wykluczony z obliczeń."
								: !row.problems.empty() ? row.problems.front().c_str() : "Brak energii lub referencji μ.");
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
			first = last;
		}
		ImGui::EndPopup();
	}
} // namespace DefectStudio
