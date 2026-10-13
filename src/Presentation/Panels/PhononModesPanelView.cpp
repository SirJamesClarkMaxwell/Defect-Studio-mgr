#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cfloat>
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
		ImGui::TextUnformatted("Irrep"); ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::BeginCombo("##irrep", settings.irrepFilter.empty() ? "wszystkie" : settings.irrepFilter.c_str()))
		{
			if (ImGui::Selectable("wszystkie", settings.irrepFilter.empty())) { settings.irrepFilter.clear(); persist(); }
			for (const auto &irrep : m_Session->Irreps())
				if (ImGui::Selectable(irrep.c_str(), settings.irrepFilter == irrep)) { settings.irrepFilter = irrep; persist(); }
			ImGui::EndCombo();
		}
		const auto &result = *m_Session->Result();
		ImGui::SameLine(); ImGui::TextUnformatted("min S_k"); ImGui::SameLine();
		float minimum = static_cast<float>(settings.minHuangRhys);
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::DragFloat("##minSk", &minimum, 0.0001f, 0.0f, 1.0f, "%.4g",
			ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat))
		{ settings.minHuangRhys = minimum; persist(); }
		ImGui::SetItemTooltip("Pokaż tylko mody o czynniku Huanga-Rhysa S_k ≥ progu (udział modu w S)");
		auto modes = m_Session->VisibleModes();
		const auto total = std::count_if(result.modes.begin(), result.modes.end(), [](const auto &mode) { return !mode.isTranslation; });
		ImGui::SameLine(); ImGui::Text("modów: %zu z %zu", modes.size(), static_cast<std::size_t>(total));
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
		bool scrollToSelection = m_ScrollToMode;
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
			if (found != modes.end())
			{
				const int row = static_cast<int>(found - modes.begin());
				clipper.IncludeItemsByIndex(row, row + 1);
			}
			else scrollToSelection = false;
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
				const std::string label = (selected ? (m_Session->IsAnimating() ? "▶ " : "● ") : "") +
					std::to_string(mode.index) + "###mode";
				// The accent row background marks the selection; Selectable's own highlight would stack on it.
				if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
					selectMode(static_cast<int>(index));
				if (selected)
				{
					const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.35f);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, accent);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, accent);
				}
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
		m_ScrollToMode = false;
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
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Pokaż strzałki");
		if (ImGui::IsItemClicked()) { settings.showArrows = !settings.showArrows; persist(); }
		ImGui::SameLine();
		if (ImGui::Checkbox("##showArrows", &settings.showArrows)) persist();
		ImGui::SameLine();
		ImGui::TextUnformatted("Zgodne z fazą");
		if (ImGui::IsItemClicked()) { settings.arrowsFollowPhase = !settings.arrowsFollowPhase; persist(); }
		ImGui::SameLine();
		if (ImGui::Checkbox("##arrowsFollowPhase", &settings.arrowsFollowPhase)) persist();
		ImGui::SameLine();
		ImGui::TextUnformatted("Kolory:"); ImGui::SameLine();
		ImGui::ColorEdit3("##arrowLowColor", &settings.arrowLowColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::SetItemTooltip("Mały udział");
		ImGui::SameLine();
		ImGui::ColorEdit3("##arrowHighColor", &settings.arrowHighColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::SetItemTooltip("Duży udział");
		ImGui::SameLine();
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
		if (!ImGui::BeginTable("##visualisationSettings", 3, ImGuiTableFlags_SizingStretchSame)) return;
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Próg strzałek");
		ImGui::SetItemTooltip("Udział atomu w modzie = |e_i|^2 (suma po atomach = 1). Strzałki atomów o mniejszym udziale są ukryte. Nie filtruje tabeli - do tego służy min S_k.");
		ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("Atomy niosące [%]");
		ImGui::SetItemTooltip("Ile najsilniej drgających atomów razem niesie X %% modu (kolumna N(X%%) w tabeli).");
		ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted("Długość strzałki [Å]");
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		float threshold = static_cast<float>(settings.participationThreshold);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::SliderFloat("##participationThreshold", &threshold, 1e-4f, 1.0f, "%.4g", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
			settings.participationThreshold = threshold;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::SetItemTooltip("Udział atomu w modzie = |e_i|^2 (suma po atomach = 1). Strzałki atomów o mniejszym udziale są ukryte.");
		ImGui::TableSetColumnIndex(1);
		float fraction = static_cast<float>(settings.carriedFraction * 100.0);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::SliderFloat("##carriedFraction", &fraction, 0.1f, 100.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp)) settings.carriedFraction = fraction / 100.0;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::SetItemTooltip("Ile najsilniej drgających atomów razem niesie X %% modu (kolumna N(X%%) w tabeli).");
		ImGui::TableSetColumnIndex(2);
		float length = static_cast<float>(settings.arrowLengthAngstrom);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##arrowLength", &length, 0.01f, 0.1f, 5.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
			settings.arrowLengthAngstrom = length;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("strzałek: %zu z %zu", m_Session->ArrowCount(), m_Session->Result()->AtomCount());
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%zu atomów niesie %.1f %% modu", m_Session->AtomsCarryingSelectedFraction(), settings.carriedFraction * 100.0);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Amplituda [Å]");
		ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("Okres [s]");
		ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted("Faza");
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		float amplitude = static_cast<float>(settings.animationAmplitudeAngstrom);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##animationAmplitude", &amplitude, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
			settings.animationAmplitudeAngstrom = amplitude;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::TableSetColumnIndex(1);
		float period = static_cast<float>(settings.animationPeriodSeconds);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##animationPeriod", &period, 0.02f, 0.2f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
			settings.animationPeriodSeconds = period;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::TableSetColumnIndex(2);
		ImGui::BeginDisabled(!window || !m_Paused || !m_Session->IsAnimating());
		float phase = static_cast<float>(m_Session->Phase());
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::SliderFloat("##phase", &phase, -std::numbers::pi_v<float>, std::numbers::pi_v<float>, "%.2f rad", ImGuiSliderFlags_AlwaysClamp))
			m_Session->SetPhase(window->get(), phase);
		ImGui::EndDisabled();
		ImGui::EndTable();
	}
}
