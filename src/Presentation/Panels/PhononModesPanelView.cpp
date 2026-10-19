#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <format>
#include <numbers>
#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"

#include "Renderer/RendererWindowState.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

namespace DefectStudio
{
	void PhononModesPanel::drawSummary()
	{
		const auto &result = *m_Session->Result();
		ImGui::SeparatorText("Podsumowanie");
		ImGui::Text("S_total = %.4f   Debye–Waller = %.4f   N = %zu", result.sTotal, result.debyeWaller, result.AtomCount());
		if (!m_Session->m_EmbeddingSummary.empty()) ImGui::TextWrapped("%s", m_Session->m_EmbeddingSummary.c_str());
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
		auto exportModes = modes;
		SortPhononModes(result, exportModes, m_ModeSortColumn, m_ModeSortAscending);
		ImGui::SameLine();
		TableExportPopup(PhononModesColumns(result, exportModes, settings.carriedFraction, m_Session->m_ProjectDirectory),
			m_Session->m_ProjectDirectory, PhononExportLabel(*m_Session, result.label), m_TableExport);
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
			m_ModeSortColumn = sort.ColumnIndex;
			m_ModeSortAscending = sort.SortDirection == ImGuiSortDirection_Ascending;
			SortPhononModes(result, modes, m_ModeSortColumn, m_ModeSortAscending);
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
		auto &settings = m_Session->Settings();
		const auto ccdBlocker = m_Session->CcdDirectionBlocker();
		ImGui::SeparatorText("Animacja drgań");
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Źródło"); ImGui::SameLine();
		ImGui::SetNextItemWidth(190.0f);
		if (ImGui::BeginCombo("##vibrationSource", settings.animationSource == "bulk_q" ? "mod bulku w q" : settings.animationSource == "ccd_direction" ? "kierunek ΔQ" : "mod defektu"))
		{
			if (ImGui::Selectable("mod defektu", settings.animationSource == "mode")) m_Session->SetAnimationSource("mode");
			ImGui::BeginDisabled(!ccdBlocker.empty());
			if (ImGui::Selectable("kierunek ΔQ", settings.animationSource == "ccd_direction")) m_Session->SetAnimationSource("ccd_direction");
			if (!ccdBlocker.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s", ccdBlocker.c_str());
			ImGui::EndDisabled();
			ImGui::BeginDisabled(settings.bulkForceConstantDirectory.Empty());
			if (ImGui::Selectable("mod bulku w q", settings.animationSource == "bulk_q")) m_Session->SetAnimationSource("bulk_q");
			if (settings.bulkForceConstantDirectory.Empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Ustaw katalog FC bulku w przypadku.");
			ImGui::EndDisabled();
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Źródło pola przemieszczeń atomów.");
		const bool ccd = settings.animationSource == "ccd_direction";
		const bool bulk = settings.animationSource == "bulk_q";
		bool sweep = ccd && settings.ccdMotion == "sweep";
		if (bulk)
		{
			ImGui::TextUnformatted("q (zredukowane, baza prymitywna)");
			for (int axis = 0; axis < 3; ++axis)
			{
				if (axis) ImGui::SameLine();
				float value = static_cast<float>(settings.bulkQ[axis]);
				ImGui::SetNextItemWidth(110.0f);
				if (ImGui::DragFloat(axis == 0 ? "qx" : axis == 1 ? "qy" : "qz", &value, 0.001f, 0.0f, 0.0f, "%.6g"))
				{
					settings.bulkQ[axis] = value;
					persist();
				}
			}
			ImGui::SetNextItemWidth(150.0f);
			if (ImGui::InputInt("Gałąź", &settings.bulkBand))
			{
				settings.bulkBand = std::clamp(settings.bulkBand, 0, m_Session->m_Dispersion ? m_Session->m_Dispersion->bandCount - 1 : INT_MAX);
				persist();
			}
			std::vector<char> size(settings.bulkSupercell.begin(), settings.bulkSupercell.end());
			size.resize(size.size() + 128, '\0');
			ImGui::SetNextItemWidth(190.0f);
			if (ImGui::InputText("Superkomórka", size.data(), size.size())) { settings.bulkSupercell = size.data(); persist(); }
			ImGui::SetItemTooltip("auto lub NxMxK, np. 3x3x1; wielokrotności komórki prymitywnej. W 2D K = 1.");
			const auto reason = m_Session->BulkModeBlocker();
			const std::string blocker = !reason.empty() ? reason : m_Session->IsBusy() ? "Poczekaj na zakończenie bieżącego zadania." : "";
			ImGui::BeginDisabled(!blocker.empty());
			if (ImGui::Button("Policz mod")) m_Session->SubmitBulkMode();
			ImGui::EndDisabled();
			if (!blocker.empty()) ImGui::SetItemTooltip("%s", blocker.c_str());
			if (m_Session->m_PendingModeJob)
			{
				const auto jobs = m_Session->m_JobSystem.lock();
				const auto snapshot = jobs ? jobs->GetJob(m_Session->m_PendingModeJobId) : std::nullopt;
				if (snapshot) ImGui::ProgressBar(snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f,
					ImVec2(-1.0f, 0.0f), snapshot->currentMessage.c_str());
				if (ImGui::Button("Anuluj mod bulku")) m_Session->CancelBulkMode();
			}
			if (!m_Session->m_BulkModeError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_Session->m_BulkModeError.c_str());
			if (const auto &mode = m_Session->BulkMode(); mode)
			{
				ImGui::Text("ℏω = %.3f meV, q = (%.6g, %.6g, %.6g), gałąź %d, %d×%d×%d", mode->frequencyMeV,
					mode->q.x, mode->q.y, mode->q.z, mode->band, mode->supercell[0], mode->supercell[1], mode->supercell[2]);
				if (!mode->commensurate)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Ruch nie domyka się okresowo w tej superkomórce (fragment fali).");
					if (mode->suggestedSupercell)
					{
						const auto &suggested = *mode->suggestedSupercell;
						const auto value = std::format("{}x{}x{}", suggested[0], suggested[1], suggested[2]);
						const auto label = std::format("Użyj {}×{}×{}", suggested[0], suggested[1], suggested[2]);
						ImGui::BeginDisabled(m_Session->IsBusy());
						if (ImGui::Button(label.c_str()))
						{
							settings.bulkQ = mode->q;
							settings.bulkBand = mode->band;
							settings.bulkSupercell = value;
							if (suggested[2] != 1) settings.dispersionInPlane = false;
							m_Session->SubmitBulkMode();
						}
						ImGui::EndDisabled();
					}
					else ImGui::TextDisabled("Brak współmiernej superkomórki ≤ 24");
				}
			}
		}
		if (ccd && !ccdBlocker.empty()) ImGui::TextDisabled("%s", ccdBlocker.c_str());
		if (ccd && m_Session->CcdResult())
			for (const auto &warning : m_Session->CcdResult()->get().loadWarnings) ImGui::TextWrapped("%s", warning.c_str());
		ImGui::TextUnformatted(m_Session->Field().label.c_str());
		if (const auto partner = bulk ? (m_Session->BulkMode() && m_Session->BulkMode()->partner ?
			std::optional<std::size_t>(m_Session->BulkMode()->partner->band) : std::nullopt) : m_Session->DegeneratePartnerOfSelected(); partner && !ccd)
		{
			if (bulk) ImGui::Text("Zdegenerowany z gałęzią %zu", *partner);
			else ImGui::Text("Zdegenerowany z modem %d", m_Session->Result()->modes[*partner].index);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Mieszanie θ"); ImGui::SameLine();
			float angle = static_cast<float>(m_Session->MixAngle() * 180.0 / std::numbers::pi);
			ImGui::SetNextItemWidth(190.0f);
			if (ImGui::DragFloat("##vibrationMixAngle", &angle, 0.5f, 0.0f, 180.0f, "%.1f°", ImGuiSliderFlags_AlwaysClamp))
				m_Session->SetMixAngle(angle * std::numbers::pi / 180.0);
			ImGui::SetItemTooltip("cos θ · e1 + sin θ · e2; 0° = wybrany mod, 90° = partner");
		}
		if (ccd)
		{
			ImGui::TextUnformatted("Ruch"); ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("##ccdMotion", sweep ? "przejście GS ↔ ES" : "drganie wokół GS"))
			{
				if (ImGui::Selectable("przejście GS ↔ ES", sweep)) m_Session->SetCcdMotion("sweep");
				if (ImGui::Selectable("drganie wokół GS", !sweep)) m_Session->SetCcdMotion("oscillate");
				ImGui::EndCombo();
			}
			sweep = settings.ccdMotion == "sweep";
			if (sweep)
			{
				float extent = static_cast<float>(settings.ccdExtent);
				ImGui::TextUnformatted("Zasięg"); ImGui::SameLine();
				ImGui::SetNextItemWidth(190.0f);
				if (ImGui::DragFloat("##ccdExtent", &extent, 0.01f, 1.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					m_Session->SetCcdExtent(extent);
			}
		}
		ImGui::BeginDisabled(bulk ? !m_Session->BulkMode() || m_Session->supercellPath().Empty() : ccd ? !ccdBlocker.empty() || m_Session->supercellPath().Empty() : settings.forceConstantDirectory.Empty());
		if (ImGui::Button(bulk ? "Otwórz superkomórkę bulku" : ccd ? "Otwórz geometrię GS" : "Otwórz superkomórkę")) m_Session->openSupercellWindow();
		ImGui::EndDisabled();
		if (!ccd && !bulk && settings.forceConstantDirectory.Empty()) ImGui::SetItemTooltip("Ustaw katalog stałych siłowych (FC).");
		ImGui::SameLine();
		if (!m_Session->m_WindowError.empty()) ImGui::TextDisabled("%s", m_Session->m_WindowError.c_str());
		else if (const auto window = m_Session->targetWindow()) ImGui::TextDisabled("%s", window->get().title.c_str());
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
		const auto window = m_Session->targetWindow();
		const std::string animationBlocker = ccd && !ccdBlocker.empty() ? ccdBlocker :
			bulk && !m_Session->BulkMode() ? "Policz lub wczytaj mod bulku." :
			!ccd && !bulk && !m_Session->SelectedMode() ? "Wybierz mod." :
			!window ? (bulk ? "Otwórz superkomórkę bulku." : ccd ? "Otwórz geometrię GS." : "Otwórz superkomórkę FC.") : m_Session->m_WindowError;
		ImGui::BeginDisabled(!animationBlocker.empty());
		if (ImGui::Button(m_Session->IsAnimating() && !m_Session->m_Paused ? "⏸ Pauza" : "▶ Animuj"))
		{
			if (!m_Session->IsAnimating()) { m_Session->StartAnimation(window->get()); m_Session->m_Paused = false; }
			else m_Session->m_Paused = !m_Session->m_Paused;
			persist();
		}
		if (!animationBlocker.empty()) ImGui::SetItemTooltip("%s", animationBlocker.c_str());
		ImGui::SameLine();
		if (ImGui::Button("⏹ Stop")) StopAnimation();
		ImGui::EndDisabled();
		if (!animationBlocker.empty()) ImGui::SetItemTooltip("%s", animationBlocker.c_str());
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
		ImGui::Text("strzałek: %zu z %zu", m_Session->ArrowCount(), m_Session->Field().symbols.size());
		ImGui::TableSetColumnIndex(1);
		if (!ccd && !bulk && m_Session->Result())
			ImGui::Text("%zu atomów niesie %.1f %% modu", m_Session->AtomsCarryingSelectedFraction(), settings.carriedFraction * 100.0);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Amplituda [Å]");
		ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("Okres [s]");
		ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(sweep ? "Q [amu^½ Å]" : "Faza");
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		float amplitude = static_cast<float>(settings.animationAmplitudeAngstrom);
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::BeginDisabled(sweep);
		if (ImGui::DragFloat("##animationAmplitude", &amplitude, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
			settings.animationAmplitudeAngstrom = amplitude;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::EndDisabled();
		ImGui::TableSetColumnIndex(1);
		float period = static_cast<float>(settings.animationPeriodSeconds);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##animationPeriod", &period, 0.02f, 0.2f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
			settings.animationPeriodSeconds = period;
		if (ImGui::IsItemDeactivatedAfterEdit()) persist();
		ImGui::TableSetColumnIndex(2);
		ImGui::BeginDisabled(!window || !animationBlocker.empty() ||
			(sweep ? m_Session->IsAnimating() && !m_Session->m_Paused : !m_Session->m_Paused || !m_Session->IsAnimating()));
		float phase = static_cast<float>(m_Session->Phase());
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (sweep)
		{
			float q = static_cast<float>(settings.ccdSweepQ);
			if (ImGui::DragFloat("##ccdSweepQ", &q, 0.01f, 0.0f, static_cast<float>(m_Session->CcdSweepMaximum()), "%.3f", ImGuiSliderFlags_AlwaysClamp))
				m_Session->SetCcdSweepQ(window->get(), q);
		}
		else if (ImGui::SliderFloat("##phase", &phase, -std::numbers::pi_v<float>, std::numbers::pi_v<float>, "%.2f rad", ImGuiSliderFlags_AlwaysClamp))
			m_Session->SetPhase(window->get(), phase);
		ImGui::EndDisabled();
		ImGui::EndTable();
	}
}
