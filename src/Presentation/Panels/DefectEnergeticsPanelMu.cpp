#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>

#include <imgui.h>
#include <implot.h>

namespace DefectStudio
{
	namespace
	{
		constexpr ImVec4 kWarningColor(0.95f, 0.75f, 0.30f, 1.0f);

		// "Si3N4" -> {Si: 3, N: 4}; nullopt for anything else.
		std::optional<SpeciesCounts> ParseFormula(const std::string &text)
		{
			SpeciesCounts formula;
			std::size_t i = 0;
			while (i < text.size())
			{
				if (!std::isupper(static_cast<unsigned char>(text[i])))
					return std::nullopt;
				std::string symbol(1, text[i++]);
				while (i < text.size() && std::islower(static_cast<unsigned char>(text[i])))
					symbol += text[i++];
				int count = 0;
				while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
					count = 10 * count + (text[i++] - '0');
				formula[symbol] += count == 0 ? 1 : count;
			}
			return formula.empty() ? std::nullopt : std::optional<SpeciesCounts>(formula);
		}

		std::string FormulaText(const SpeciesCounts &formula)
		{
			std::string text;
			for (const auto &[species, count] : formula)
				text += species + (count == 1 ? "" : std::to_string(count));
			return text;
		}
	} // namespace

	void DefectEnergeticsPanel::drawChemicalPotentials()
	{
		if (ImGui::SmallButton("Pokaż wszystkie okna energetyki"))
			m_Session->ShowViews();
		ImGui::SetItemTooltip("Energie tworzenia, Poziomy przejść, Koncentracje i FNV Correction.\nZamknięcie tego okna zamyka pozostałe okna energetyki.");
		if (!m_Session->m_Model.chemicalPotentialError.empty())
			ImGui::TextColored(kWarningColor, "%s", m_Session->m_Model.chemicalPotentialError.c_str());
		if (!ImGui::BeginTabBar("##Conditions"))
			return;
		if (ImGui::BeginTabItem("Obliczenia"))
		{
			drawCalculations();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Źródło / bulk"))
		{
			drawSource();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Referencje μ"))
		{
			drawMuReferences();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Fazy"))
		{
			drawMuPhases();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Zestawy μ"))
		{
			drawMuSets();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	void DefectEnergeticsPanel::drawMuReferences()
	{
		const ChemicalPotentialSystem &system = m_Session->m_Model.chemicalPotentials;
		// mu_ref per element: which run it comes from.
		ImGui::SeparatorText("Referencje μ_ref (eV/atom)");
		if (ImGui::BeginTable("##MuReferences", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Pierwiastek", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("μ_ref", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Obliczenie", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
			for (const auto &species : system.species)
			{
				ImGui::PushID(species.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(species.c_str());
				ImGui::TableNextColumn();
				if (const auto found = system.references.find(species); found != system.references.end())
					ImGui::Text("%.6f", found->second);
				else
					ImGui::TextColored(kWarningColor, "brak");
				ImGui::TableNextColumn();
				const auto chosen = m_Session->m_File->references.find(species);
				const auto chosenRun = chosen == m_Session->m_File->references.end() ? m_Session->m_File->runs.end()
					: std::find_if(m_Session->m_File->runs.begin(), m_Session->m_File->runs.end(), [&](const VaspRunRecord &run) { return run.directory == chosen->second; });
				const std::string preview = chosen == m_Session->m_File->references.end() ? "auto"
					: chosenRun != m_Session->m_File->runs.end() ? m_Session->runLabel(static_cast<std::size_t>(chosenRun - m_Session->m_File->runs.begin()))
					: chosen->second.Utf8();
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##reference", preview.c_str()))
				{
					if (ImGui::Selectable("auto", chosen == m_Session->m_File->references.end()))
					{
						m_Session->m_File->references.erase(species);
						m_Session->Changed();
					}
					for (std::size_t run = 0; run < m_Session->m_File->runs.size(); ++run)
					{
						const SpeciesCounts composition = CalculationComposition(m_Session->m_File->runs[run]);
						if (composition.size() != 1 || composition.begin()->first != species)
							continue;
						if (ImGui::Selectable(m_Session->runLabel(run).c_str()))
						{
							m_Session->m_File->references[species] = m_Session->m_File->runs[run].directory;
							m_Session->Changed();
						}
						if (m_Session->m_File->runs[run].totalEnergy)
							ImGui::SetItemTooltip("%d atomów, %.6f eV/atom", composition.begin()->second,
								*m_Session->m_File->runs[run].totalEnergy / composition.begin()->second);
					}
					if (ImGui::Selectable("z folderu..."))
						pickReference(species);
					ImGui::SetItemTooltip("Obliczenie spoza folderu obliczeń, np. O:/Potencjaly_chemiczne/Ge/...");
					ImGui::EndCombo();
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

	}

	void DefectEnergeticsPanel::drawMuPhases()
	{
		const ChemicalPotentialSystem &system = m_Session->m_Model.chemicalPotentials;
		// Competing phases: each one an inequality on the Delta mu of its elements.
		ImGui::SeparatorText("Fazy konkurencyjne");
		ImGui::TextDisabled("Bez nich jedynym ograniczeniem jest Δμ_i ≤ 0 (praca inż.). Np. SiC, Si3N4, Ge3N4.");
		int removePhase = -1;
		for (std::size_t p = 0; p < m_Session->m_File->competing.size(); ++p)
		{
			EnergeticsPhase &phase = m_Session->m_File->competing[p];
			ImGui::PushID(static_cast<int>(p));
			ImGui::Text("%s", FormulaText(phase.formula).c_str());
			ImGui::SameLine(80.0f);
			const auto computed = std::find_if(system.competing.begin(), system.competing.end(),
				[&](const ChemicalPhase &candidate) { return candidate.name == phase.name; });
			if (computed != system.competing.end())
				if (const auto enthalpy = FormationEnthalpy(*computed, system.references))
				{
					ImGui::Text("ΔH_f = %.4f eV/f.u.", *enthalpy);
					ImGui::SameLine();
				}
			ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 8.0f));
			if (ImGui::BeginCombo("##phaseRun", phase.run.Empty() ? "wybierz obliczenie" : phase.run.Utf8().c_str()))
			{
				for (std::size_t run = 0; run < m_Session->m_File->runs.size(); ++run)
				{
					const SpeciesCounts composition = CalculationComposition(m_Session->m_File->runs[run]);
					bool matches = !composition.empty();
					for (const auto &[species, count] : composition)
						matches = matches && phase.formula.contains(species);
					if (matches && ImGui::Selectable(m_Session->runLabel(run).c_str(), phase.run == m_Session->m_File->runs[run].directory))
					{
						phase.run = m_Session->m_File->runs[run].directory;
						m_Session->Changed();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("x"))
				removePhase = static_cast<int>(p);
			ImGui::PopID();
		}
		if (removePhase >= 0)
		{
			m_Session->m_File->competing.erase(m_Session->m_File->competing.begin() + removePhase);
			m_Session->Changed();
		}
		ImGui::SetNextItemWidth(120.0f);
		ImGui::InputTextWithHint("##newPhase", "wzór, np. Si3N4", m_NewPhaseFormula.data(), m_NewPhaseFormula.size());
		ImGui::SameLine();
		const auto formula = ParseFormula(m_NewPhaseFormula.data());
		ImGui::BeginDisabled(!formula);
		if (ImGui::Button("Dodaj fazę"))
		{
			m_Session->m_File->competing.push_back({m_NewPhaseFormula.data(), *formula, Path()});
			m_NewPhaseFormula.fill('\0');
			m_Session->Changed();
		}
		ImGui::EndDisabled();

	}

	void DefectEnergeticsPanel::drawMuSets()
	{
		const ChemicalPotentialSystem &system = m_Session->m_Model.chemicalPotentials;
		// The sets: corners of the allowed region (computed) and the user's own.
		ImGui::SeparatorText("Zestawy μ (Δμ w eV/atom)");
		const int columns = 3 + static_cast<int>(system.species.size());
		int removeSet = -1;
		if (ImGui::BeginTable("##MuSets", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Zestaw", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			for (const auto &species : system.species)
				ImGui::TableSetupColumn(("Δμ_" + species).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			const std::size_t vertices = static_cast<std::size_t>(std::count_if(m_Session->m_Model.muSets.begin(), m_Session->m_Model.muSets.end(),
				[](const auto &set) { return set.vertex; }));
			for (std::size_t s = 0; s < m_Session->m_Model.muSets.size(); ++s)
			{
				const EnergeticsMuSetView &set = m_Session->m_Model.muSets[s];
				ImGui::PushID(static_cast<int>(s));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::RadioButton("##use", &m_Session->m_MuSet, static_cast<int>(s)))
					m_Session->InvalidateInputs();
				ImGui::SetItemTooltip("Ten zestaw na wykresie i w tabeli E_f(0).");
				ImGui::TableNextColumn();
				if (set.vertex)
					ImGui::Text("bogate: %s", set.name.c_str());
				else
					ImGui::TextUnformatted(set.name.c_str());
				if (set.vertex)
					ImGui::SetItemTooltip("Wierzchołek obszaru dozwolonego; ograniczenia aktywne: %s", set.name.c_str());
				for (const auto &species : system.species)
				{
					ImGui::TableNextColumn();
					const auto value = set.deltaMu.find(species);
					double delta = value == set.deltaMu.end() ? 0.0 : value->second;
					if (set.vertex)
						ImGui::Text("%.4f", delta);
					else
					{
						ImGui::SetNextItemWidth(-1.0f);
						if (ImGui::InputDouble(("##" + species).c_str(), &delta, 0.0, 0.0, "%.4f"))
						{
							m_Session->m_File->muSets[s - vertices].deltaMu[species] = delta;
							m_Session->Changed();
						}
					}
				}
				ImGui::TableNextColumn();
				if (!set.vertex)
				{
					if (ImGui::SmallButton("x"))
						removeSet = static_cast<int>(s - vertices);
					if (!set.violated.empty())
					{
						ImGui::SameLine();
						std::string list;
						for (const auto &name : set.violated)
							list += (list.empty() ? "" : ", ") + name;
						ImGui::TextColored(kWarningColor, "(!)");
						ImGui::SetItemTooltip("Poza obszarem dozwolonym: %s", list.c_str());
					}
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (removeSet >= 0)
		{
			m_Session->m_File->muSets.erase(m_Session->m_File->muSets.begin() + removeSet);
			m_Session->Changed();
		}
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputTextWithHint("##newSet", "nazwa, np. N-ubogie", m_NewSetName.data(), m_NewSetName.size());
		ImGui::SameLine();
		ImGui::BeginDisabled(m_NewSetName[0] == '\0');
		if (ImGui::Button("Dodaj zestaw"))
		{
			EnergeticsMuSet set{m_NewSetName.data(), {}};
			if (!m_Session->m_Model.muSets.empty())
				set.deltaMu = m_Session->m_Model.muSets[static_cast<std::size_t>(m_Session->m_MuSet)].deltaMu; // start from the chosen one
			m_Session->m_File->muSets.push_back(std::move(set));
			m_NewSetName.fill('\0');
			m_Session->Changed();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Nowy zestaw zaczyna od wartości zaznaczonego.");
	}
} // namespace DefectStudio
