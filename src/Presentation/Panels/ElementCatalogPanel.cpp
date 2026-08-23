#include "Core/dspch.hpp"

#include "Presentation/Panels/ElementCatalogPanel.hpp"

#include <algorithm>
#include <vector>

#include <imgui.h>

#include "IO/AtomStyleIO.hpp"

namespace DefectStudio
{
	ElementCatalogPanel::ElementCatalogPanel(
		RendererLayer &layer, AtomStyleTable atomStyleTable, Path atomStylesPath, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_AtomStyleTable(std::move(atomStyleTable)),
		  m_AtomStylesPath(std::move(atomStylesPath))
	{
	}

	Ref<IPanel> ElementCatalogPanel::Clone() const
	{
		return CreateRef<ElementCatalogPanel>(*this);
	}

	// Colors/radii are baked into RendererStructureData::atoms[i]/bonds[i] at build time (see
	// StructureRendererDataBuilder.cpp), not read live from AtomStyleTable per frame - so a style
	// edit needs to explicitly refresh every already-open window's cached values by element symbol,
	// same formulas BuildRendererBonds uses for bond radius/gradient. No domain round-trip needed:
	// style has no domain representation, RendererAtomData already carries the element symbol.
	void ElementCatalogPanel::applyToLiveTable()
	{
		m_AtomStyleTable.ReplaceStyles(m_EditBuffer, m_AtomStyleTable.GetVacancyStyle());

		for (RendererWindowState &windowState : m_Layer.GetWindows())
		{
			for (RendererAtomData &atom : windowState.structure.atoms)
			{
				atom.color = m_AtomStyleTable.Color(atom.element);
				atom.radius = m_AtomStyleTable.DisplayRadius(atom.element);
			}
			for (RendererBondData &bond : windowState.structure.bonds)
			{
				if (bond.firstAtomIndex >= windowState.structure.atoms.size() ||
					bond.secondAtomIndex >= windowState.structure.atoms.size())
					continue;
				const RendererAtomData &atomA = windowState.structure.atoms[bond.firstAtomIndex];
				const RendererAtomData &atomB = windowState.structure.atoms[bond.secondAtomIndex];
				bond.radius = std::max(0.05f, 0.22f * std::min(atomA.radius, atomB.radius));
				bond.gradient.start = atomA.color;
				bond.gradient.finish = atomB.color;
			}
		}
	}

	void ElementCatalogPanel::Render()
	{
		if (!IsVisible())
			return;

		if (!m_Loaded)
		{
			m_EditBuffer = m_AtomStyleTable.AllStyles();
			m_Loaded = true;
		}

		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		const auto &symbols = m_Layer.GetPeriodicTableSymbols();
		std::vector<std::string> availableToAdd;
		for (const std::string &symbol : symbols)
			if (!m_EditBuffer.contains(symbol))
				availableToAdd.push_back(symbol);

		if (!availableToAdd.empty())
		{
			if (m_AddElementIndex < 0 || static_cast<std::size_t>(m_AddElementIndex) >= availableToAdd.size())
				m_AddElementIndex = 0;
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::BeginCombo("##AddElement", availableToAdd[static_cast<std::size_t>(m_AddElementIndex)].c_str()))
			{
				for (std::size_t i = 0; i < availableToAdd.size(); ++i)
				{
					const bool selected = static_cast<int>(i) == m_AddElementIndex;
					if (ImGui::Selectable(availableToAdd[i].c_str(), selected))
						m_AddElementIndex = static_cast<int>(i);
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("Add element"))
				m_EditBuffer[availableToAdd[static_cast<std::size_t>(m_AddElementIndex)]] = AtomRenderStyle{};
		}

		ImGui::Separator();

		std::vector<std::string> sortedSymbols;
		sortedSymbols.reserve(m_EditBuffer.size());
		for (const auto &[symbol, style] : m_EditBuffer)
			sortedSymbols.push_back(symbol);
		std::sort(sortedSymbols.begin(), sortedSymbols.end());

		if (ImGui::BeginTable(
				"##ElementStyleTable", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
				ImVec2(0.0f, 320.0f)))
		{
			ImGui::TableSetupColumn("Element", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 160.0f);
			ImGui::TableSetupColumn("Radius", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableHeadersRow();

			std::string symbolToRemove;
			for (const std::string &symbol : sortedSymbols)
			{
				AtomRenderStyle &style = m_EditBuffer[symbol];
				ImGui::PushID(symbol.c_str());

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(symbol.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::ColorEdit3("##color", &style.color.x, ImGuiColorEditFlags_NoInputs);

				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##radius", &style.displayRadius, 0.01f, 0.05f, 3.0f, "%.2f");

				ImGui::TableSetColumnIndex(3);
				if (ImGui::Button("X"))
					symbolToRemove = symbol;

				ImGui::PopID();
			}
			if (!symbolToRemove.empty())
				m_EditBuffer.erase(symbolToRemove);

			ImGui::EndTable();
		}

		ImGui::Separator();
		if (ImGui::Button("Apply"))
		{
			applyToLiveTable();
			m_StatusMessage = "Applied to open viewports.";
		}
		ImGui::SameLine();
		if (ImGui::Button("Save to file"))
		{
			applyToLiveTable();
			std::string error;
			if (AtomStyleIO::SaveToFile(m_AtomStylesPath, m_EditBuffer, m_AtomStyleTable.GetVacancyStyle(), error))
				m_StatusMessage = "Saved to " + m_AtomStylesPath.String();
			else
				m_StatusMessage = "Save failed: " + error;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload from live table"))
		{
			m_EditBuffer = m_AtomStyleTable.AllStyles();
			m_StatusMessage.clear();
		}

		if (!m_StatusMessage.empty())
			ImGui::TextDisabled("%s", m_StatusMessage.c_str());

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
