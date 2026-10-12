#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <imgui.h>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	void PhononModesPanel::drawSummary()
	{
		const auto &result = *m_Session->Result();
		ImGui::SeparatorText("Podsumowanie");
		ImGui::Text("S_total = %.4f   Debye–Waller = %.4f   N = %zu", result.sTotal, result.debyeWaller, result.AtomCount());
		if (!m_EmbeddingSummary.empty()) ImGui::TextWrapped("%s", m_EmbeddingSummary.c_str());
		for (const auto &[label, value] : {std::pair{"obliczone", result.zplEv.computed},
			std::pair{"po korekcji spinowej", result.zplEv.spinPurified}, std::pair{"eksperymentalne", result.zplEv.experimental}})
			if (value) { ImGui::Text("ZPL (%s): %.4f eV", label, *value); }
		if (!result.stable)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "mody urojone: %zu", result.imaginaryModes.size());
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				for (const auto index : result.imaginaryModes) ImGui::Text("#%d", index);
				ImGui::EndTooltip();
			}
		}
		ImGui::TextDisabled("przybliżenie harmoniczne, T = 0");
		ImGui::SetItemTooltip("%s", result.approximation.c_str());
	}

	void PhononModesPanel::drawModesTable()
	{
		auto &settings = m_Session->Settings();
		if (ImGui::BeginCombo("Irrep", settings.irrepFilter.empty() ? "wszystkie" : settings.irrepFilter.c_str()))
		{
			if (ImGui::Selectable("wszystkie", settings.irrepFilter.empty())) { settings.irrepFilter.clear(); persist(); }
			for (const auto &irrep : m_Session->Irreps())
				if (ImGui::Selectable(irrep.c_str(), settings.irrepFilter == irrep)) { settings.irrepFilter = irrep; persist(); }
			ImGui::EndCombo();
		}
		const auto &result = *m_Session->Result();
		auto modes = m_Session->VisibleModes();
		const std::string fractionLabel = std::format("N({:.0f}%)", settings.carriedFraction * 100.0);
		if (!ImGui::BeginTable("##phononModes", 7, ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit,
			ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 18.0f))) return;
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("#");
		ImGui::TableSetupColumn("ℏω [meV]", ImGuiTableColumnFlags_DefaultSort);
		for (const char *label : {"irrep", "β = N/N_eff", "N_eff", "S_k"}) ImGui::TableSetupColumn(label);
		ImGui::TableSetupColumn(fractionLabel.c_str(), ImGuiTableColumnFlags_NoSort);
		ImGui::TableHeadersRow();
		if (const auto specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0)
		{
			const auto sort = specs->Specs[0];
			std::stable_sort(modes.begin(), modes.end(), [&](auto a, auto b) {
				const auto &left = result.modes[a], &right = result.modes[b];
				if (sort.ColumnIndex == 2)
					return sort.SortDirection == ImGuiSortDirection_Ascending ? left.irrep < right.irrep : right.irrep < left.irrep;
				const auto value = [&](const PhononMode &mode) {
					switch (sort.ColumnIndex)
					{
					case 0: return static_cast<double>(mode.index);
					case 3: return mode.beta;
					case 4: return mode.effectiveAtomCount;
					case 5: return mode.huangRhys;
					default: return mode.frequencyMeV;
					}
				};
				return sort.SortDirection == ImGuiSortDirection_Ascending ? value(left) < value(right) : value(right) < value(left);
			});
			specs->SpecsDirty = false;
		}
		bool scrollToSelection = false;
		if (ImGui::IsWindowFocused() && !modes.empty())
		{
			const int direction = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 0;
			if (direction)
			{
				const auto found = std::find(modes.begin(), modes.end(), static_cast<std::size_t>(settings.selectedMode));
				const int current = found == modes.end() ? -1 : static_cast<int>(found - modes.begin());
				const int next = std::clamp(current + direction, 0, static_cast<int>(modes.size()) - 1);
				selectMode(static_cast<int>(modes[next]));
				scrollToSelection = true;
			}
		}
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(modes.size()));
		if (scrollToSelection)
		{
			const auto found = std::find(modes.begin(), modes.end(), static_cast<std::size_t>(settings.selectedMode));
			const int row = static_cast<int>(found - modes.begin());
			clipper.IncludeItemsByIndex(row, row + 1);
		}
		while (clipper.Step())
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const auto index = modes[row];
				const auto &mode = result.modes[index];
				ImGui::PushID(static_cast<int>(index));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				const bool selected = settings.selectedMode == static_cast<int>(index);
				if (ImGui::Selectable(std::to_string(mode.index).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					selectMode(static_cast<int>(index));
				if (selected && scrollToSelection) ImGui::SetScrollHereY();
				ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", mode.frequencyMeV);
				ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(mode.irrep.c_str());
				ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", mode.beta);
				ImGui::TableSetColumnIndex(4); ImGui::Text("%.2f", mode.effectiveAtomCount);
				ImGui::TableSetColumnIndex(5); ImGui::Text("%.4f", mode.huangRhys);
				if (ImGui::TableSetColumnIndex(6))
					ImGui::Text("%zu", AtomsCarryingFraction(result, index, settings.carriedFraction));
				ImGui::PopID();
			}
		ImGui::EndTable();
	}

	void PhononModesPanel::drawVisualisation()
	{
		ImGui::SeparatorText("Wizualizacja");
		ImGui::BeginDisabled(m_Session->Settings().forceConstantDirectory.Empty());
		if (ImGui::Button("Otwórz superkomórkę")) openSupercellWindow();
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (!m_WindowError.empty()) ImGui::TextDisabled("%s", m_WindowError.c_str());
		else if (const auto window = targetWindow()) ImGui::TextDisabled("%s", window->get().title.c_str());
		auto &settings = m_Session->Settings();
		if (ImGui::Checkbox("Pokaż strzałki", &settings.showArrows)) persist();
		float threshold = static_cast<float>(settings.participationThreshold);
		if (ImGui::SliderFloat("Próg udziału", &threshold, 1e-4f, 1.0f, "%.4g", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
			settings.participationThreshold = threshold;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::Text("strzałek: %zu z %zu", m_Session->ArrowCount(), m_Session->Result()->AtomCount());
		float fraction = static_cast<float>(settings.carriedFraction * 100.0);
		if (ImGui::SliderFloat("X [%]", &fraction, 0.1f, 100.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp)) settings.carriedFraction = fraction / 100.0;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::Text("%zu atomów niesie %.1f %% modu", m_Session->AtomsCarryingSelectedFraction(), settings.carriedFraction * 100.0);
		ImGui::InputDouble("Długość najdłuższej strzałki [Å]", &settings.arrowLengthAngstrom, 0.0, 0.0, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (!std::isfinite(settings.arrowLengthAngstrom) || settings.arrowLengthAngstrom <= 0.0) settings.arrowLengthAngstrom = 1.5;
			persist();
		}
		ImGui::ColorEdit3("Mały udział", &settings.arrowLowColor.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::ColorEdit3("Duży udział", &settings.arrowHighColor.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		const auto window = targetWindow();
		ImGui::BeginDisabled(!window || !m_WindowError.empty() || !m_Session->SelectedMode());
		if (ImGui::Button(m_Session->IsAnimating() && !m_Paused ? "⏸ Pauza" : "▶ Animuj"))
		{
			if (!m_Session->IsAnimating()) { m_Session->StartAnimation(window->get()); m_Paused = false; }
			else m_Paused = !m_Paused;
		}
		ImGui::SameLine();
		if (ImGui::Button("⏹ Stop")) StopAnimation();
		ImGui::EndDisabled();
		ImGui::InputDouble("Amplituda [Å]", &settings.animationAmplitudeAngstrom, 0.0, 0.0, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (!std::isfinite(settings.animationAmplitudeAngstrom) || settings.animationAmplitudeAngstrom < 0.0) settings.animationAmplitudeAngstrom = 0.3;
			persist();
		}
		ImGui::InputDouble("Okres [s]", &settings.animationPeriodSeconds, 0.0, 0.0, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (!std::isfinite(settings.animationPeriodSeconds) || settings.animationPeriodSeconds <= 0.0) settings.animationPeriodSeconds = 1.5;
			persist();
		}
		ImGui::BeginDisabled(!window || !m_Paused || !m_Session->IsAnimating());
		float phase = static_cast<float>(m_Session->Phase());
		if (ImGui::SliderFloat("Faza", &phase, -std::numbers::pi_v<float>, std::numbers::pi_v<float>, "%.2f rad", ImGuiSliderFlags_AlwaysClamp))
			m_Session->SetPhase(window->get(), phase);
		ImGui::EndDisabled();
	}
}
