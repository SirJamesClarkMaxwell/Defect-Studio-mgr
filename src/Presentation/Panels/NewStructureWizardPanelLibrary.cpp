// The From Library mode: material picker, polytype/functional selection and the formula to
// site-species mapping. Split out of NewStructureWizardPanel.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/NewStructureWizardPanel.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "Domain/Crystal/FormulaParser.hpp"
#include "Domain/Crystal/PrototypeMatcher.hpp"

namespace DefectStudio
{
	void NewStructureWizardPanel::drawMaterialSection()
	{
		if (!m_CatalogError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_CatalogError.c_str());
		if (m_Catalog.materials.empty())
			return;

		ImGui::TextUnformatted("Material:");
		const char *materialLabel = m_SelectedMaterialIndex >= 0
			? m_Catalog.materials[static_cast<std::size_t>(m_SelectedMaterialIndex)].name.c_str()
			: "(none)";
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##material", materialLabel))
		{
			for (std::size_t i = 0; i < m_Catalog.materials.size(); ++i)
			{
				const MaterialDefinition &material = m_Catalog.materials[i];
				if (ImGui::Selectable(material.name.c_str(), m_SelectedMaterialIndex == static_cast<int>(i)))
				{
					m_SelectedMaterialIndex = static_cast<int>(i);
					m_SelectedPolytype = material.polytypes.empty() ? std::string{} : material.polytypes.begin()->first;
					applySelectedMaterial();
				}
			}
			ImGui::EndCombo();
		}

		if (m_SelectedMaterialIndex < 0)
			return;
		const MaterialDefinition &material = m_Catalog.materials[static_cast<std::size_t>(m_SelectedMaterialIndex)];

		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::BeginCombo("Polytype", m_SelectedPolytype.c_str()))
		{
			for (const auto &polytypeEntry : material.polytypes)
			{
				if (ImGui::Selectable(polytypeEntry.first.c_str(), polytypeEntry.first == m_SelectedPolytype))
				{
					m_SelectedPolytype = polytypeEntry.first;
					applySelectedMaterial();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::BeginCombo("Functional", m_SelectedFunctional.c_str()))
		{
			for (const auto &constantsEntry : material.constants)
			{
				if (ImGui::Selectable(constantsEntry.first.c_str(), constantsEntry.first == m_SelectedFunctional))
				{
					m_SelectedFunctional = constantsEntry.first;
					applySelectedMaterial();
				}
			}
			ImGui::EndCombo();
		}
	}

	void NewStructureWizardPanel::drawFormulaAndMappingSection()
	{
		if (m_Catalog.prototypes.empty())
			return;

		ImGui::TextUnformatted("Prototype:");
		const PrototypeDefinition *prototype = selectedPrototype();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##prototype", prototype != nullptr ? prototype->name.c_str() : "(none)"))
		{
			for (std::size_t i = 0; i < m_Catalog.prototypes.size(); ++i)
			{
				if (ImGui::Selectable(m_Catalog.prototypes[i].name.c_str(), m_SelectedPrototypeIndex == static_cast<int>(i)))
				{
					m_SelectedPrototypeIndex = static_cast<int>(i);
					m_SelectedMaterialIndex = -1; // a bare prototype carries no chemistry
					m_SiteSpecies.assign(m_Catalog.prototypes[i].sites.size(), std::string{});
					applyPrototypeToBasis();
				}
			}
			ImGui::EndCombo();
		}

		prototype = selectedPrototype();
		if (prototype == nullptr)
			return;
		if (!prototype->description.empty())
			ImGui::TextDisabled("%s", prototype->description.c_str());

		ImGui::TextUnformatted("Chemical Formula:");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##formula", m_FormulaBuffer.data(), m_FormulaBuffer.size()))
		{
			// Re-matched on every edit. This used to sit behind a function-local `static bool`,
			// which is per-process, not per-panel or per-formula: the mapping table was filled once
			// for the first formula the app ever saw and never updated again.
			const std::vector<Element> elements = FormulaParser::Parse(m_FormulaBuffer.data());
			if (const std::optional<SiteAssignment> matched =
					PrototypeMatcher::MatchFormulaToPrototype(elements, *prototype))
			{
				m_SiteSpecies = matched->species;
				applyPrototypeToBasis();
			}
		}

		if (m_FormulaBuffer[0] != '\0')
		{
			const std::vector<Element> elements = FormulaParser::Parse(m_FormulaBuffer.data());
			const std::optional<SiteAssignment> matched =
				PrototypeMatcher::MatchFormulaToPrototype(elements, *prototype);
			if (!matched)
			{
				int siteTotal = 0;
				for (const SiteDefinition &site : prototype->sites)
					siteTotal += site.Multiplicity();
				ImGui::TextColored(
					ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
					"Formula does not divide into the %d sites of %s - set the mapping below instead.",
					siteTotal,
					prototype->name.c_str());
			}
			else if (matched->isAmbiguous)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
					"Every site has the same multiplicity, so formula order decided the mapping - check it.");
			}
		}

		m_SiteSpecies.resize(prototype->sites.size());

		ImGui::TextUnformatted("Site Mapping:");
		if (ImGui::BeginTable("site_mapping", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Site", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Atoms", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Species", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (std::size_t i = 0; i < prototype->sites.size(); ++i)
			{
				ImGui::TableNextRow();
				ImGui::PushID(static_cast<int>(i));

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(prototype->sites[i].name.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", prototype->sites[i].Multiplicity());

				ImGui::TableSetColumnIndex(2);
				const bool speciesEmpty = m_SiteSpecies[i].empty();
				if (ImGui::Button(speciesEmpty ? "(select)" : m_SiteSpecies[i].c_str(), ImVec2(-1.0f, 0.0f)))
					ImGui::OpenPopup("##SiteElementPicker");
				std::string picked = m_SiteSpecies[i];
				drawElementPickerPopup("##SiteElementPicker", picked);
				if (picked != m_SiteSpecies[i])
				{
					m_SiteSpecies[i] = picked;
					applyPrototypeToBasis();
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}
} // namespace DefectStudio
