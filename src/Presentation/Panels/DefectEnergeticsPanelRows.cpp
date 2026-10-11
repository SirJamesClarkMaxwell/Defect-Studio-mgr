#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiItemFlags_MixedValue

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr ImVec4 kWarningColor(0.95f, 0.75f, 0.30f, 1.0f);

		std::string ChargeText(const int charge)
		{
			return charge > 0 ? std::format("+{}", charge) : std::format("{}", charge);
		}

		// Checked when all `count` are on, mixed when some; returns true when clicked (then: all on
		// unless all were on).
		bool TriStateCheckbox(const char *id, const std::size_t on, const std::size_t count)
		{
			bool all = on == count;
			ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, on > 0 && on < count);
			const bool clicked = ImGui::Checkbox(id, &all);
			ImGui::PopItemFlag();
			return clicked;
		}
	} // namespace

	void DefectEnergeticsPanel::drawCalculations()
	{
		drawExport();
		if (!m_Session->m_Message.empty())
			ImGui::TextWrapped("%s", m_Session->m_Message.c_str());
		drawDefectSet();
		drawRows();
	}

	// One line per (defect, q): whether it counts, the run whose TOTEN is used (auto = the
	// proposal), the LOCPOT that goes to FNV, E_corr and E_f(0) under the chosen mu set.
	void DefectEnergeticsPanel::drawRows()
	{
		if (m_Session->m_Model.rows.empty())
		{
			ImGui::TextDisabled(m_Session->m_File->runs.empty() ? "Wskaż folder i kliknij Skanuj."
				: "Brak obliczeń defektów (sprawdź bulk).");
			return;
		}
		const SpeciesEnergies mu = m_Session->m_Model.muSets.empty() ? SpeciesEnergies{}
			: AbsoluteChemicalPotentials(m_Session->m_Model.chemicalPotentials, m_Session->m_Model.muSets[static_cast<std::size_t>(m_Session->m_MuSet)].deltaMu);
		constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
		if (!ImGui::BeginTable("##EnergeticsRows", 7, flags))
			return;
		ImGui::TableSetupColumn("Defekt / q", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Obliczenie energii (zbieżne)", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("E [eV]", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("LOCPOT dla FNV", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("E_corr [eV]", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("E_f(0) [eV]", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Uwagi", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();
		// One tree node per defect (its included states, tri-state), its charge
		// states as the children.
		for (std::size_t first = 0; first < m_Session->m_Model.rows.size();)
		{
			const std::string defect = m_Session->m_Model.rows[first].defect;
			std::size_t last = first;
			while (last < m_Session->m_Model.rows.size() && m_Session->m_Model.rows[last].defect == defect)
				++last;
			std::size_t included = 0, problems = 0;
			std::string charges;
			for (std::size_t r = first; r < last; ++r)
			{
				const EnergeticsRow &row = m_Session->m_Model.rows[r];
				included += row.included ? 1 : 0;
				problems += row.problems.size();
				charges += (charges.empty() ? "" : "  ") + ChargeText(row.charge);
			}
			const std::size_t count = last - first;
			ImGui::PushID(defect.c_str());
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (TriStateCheckbox("##included", included, count))
			{
				for (std::size_t r = first; r < last; ++r)
					m_Session->choiceFor(m_Session->m_Model.rows[r]).included = included != count;
				m_Session->Changed();
			}
			ImGui::SetItemTooltip("Uwzględnij wszystkie stany ładunkowe w wykresie, obwiedni i eksporcie.");
			ImGui::SameLine();
			const bool open = ImGui::TreeNodeEx(defect.c_str(), ImGuiTreeNodeFlags_SpanTextWidth);
			drawDefectContextMenu(defect);
			ImGui::SetItemTooltip("Prawy klik: usuń defekt z analizy.");
			ImGui::TableNextColumn();
			ImGui::TextDisabled("q: %s", charges.c_str());
			ImGui::TableSetColumnIndex(6);
			if (problems)
				ImGui::TextColored(kWarningColor, "(!) %zu", problems);
			if (open)
			{
				for (std::size_t r = first; r < last; ++r)
					drawStateRow(r, mu);
				ImGui::TreePop();
			}
			ImGui::PopID();
			first = last;
		}
		ImGui::EndTable();

		// FNV queue
		if (m_Session->m_FnvRunning || !m_Session->m_FnvQueue.empty())
		{
			ImGui::ProgressBar(m_Session->m_FnvTotal ? static_cast<float>(m_Session->m_FnvDone) / static_cast<float>(m_Session->m_FnvTotal) : 0.0f,
				ImVec2(-ImGui::CalcTextSize("Anuluj").x - 3.0f * ImGui::GetStyle().FramePadding.x, 0.0f),
				m_Session->m_FnvRunning ? std::format("FNV {}/{}: {} q={}", m_Session->m_FnvDone + 1, m_Session->m_FnvTotal, m_Session->m_FnvRunning->defect,
					ChargeText(m_Session->m_FnvRunning->charge)).c_str() : nullptr);
			ImGui::SameLine();
			if (ImGui::Button("Anuluj##fnv"))
			{
				m_Session->m_FnvQueue.clear();
				if (const auto jobSystem = m_Session->m_JobSystem.lock(); jobSystem && m_Session->m_FnvJobId)
					(void)jobSystem->RequestCancel(m_Session->m_FnvJobId);
			}
		}
		else
		{
			std::size_t missing = 0;
			for (const auto &row : m_Session->m_Model.rows)
				missing += row.included && row.charge != 0 && !row.correction && !row.locpot.Empty() ? 1 : 0;
			ImGui::BeginDisabled(missing == 0 || !m_Session->m_Model.bulk);
			if (ImGui::Button(std::format("Policz brakujące FNV ({})", missing).c_str()))
				m_Session->startFnvBatch();
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Kolejno, jedno naraz: profile pymatgen (2 LOCPOT, ~1 min lokalnie, kilka minut po "
				"sieci), środek z domieszek, okna plateau zaproponowane. Okna i środek poprawisz potem w panelu FNV.");
		}
	}

	// A charge state under its defect's node: included, the energy run, the LOCPOT for
	// FNV, E_corr, E_f(0) and the problems.
	void DefectEnergeticsPanel::drawStateRow(const std::size_t r, const SpeciesEnergies &mu)
	{
		const EnergeticsRow &row = m_Session->m_Model.rows[r];
		ImGui::PushID(static_cast<int>(r));
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		bool included = row.included;
		if (ImGui::Checkbox("##included", &included))
		{
			m_Session->choiceFor(row).included = included;
			m_Session->Changed();
		}
		ImGui::SetItemTooltip("Uwzględnij w wykresie, obwiedni i eksporcie.");
		ImGui::SameLine();
		ImGui::Text("q = %s", ChargeText(row.charge).c_str());

		ImGui::TableNextColumn();
		{
			const std::string preview = row.energyRun
				? m_Session->runLabel(*row.energyRun) + (row.userChoice ? "" : "  (auto)")
				: "wybierz: " + row.proposalReason;
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##energyRun", preview.c_str()))
			{
				if (ImGui::Selectable(("auto: " + row.proposalReason).c_str(), !row.userChoice))
				{
					m_Session->choiceFor(row).energyRun.reset();
					m_Session->Changed();
				}
				for (const std::size_t candidate : row.candidates)
				{
					const VaspRunRecord &run = m_Session->m_File->runs[candidate];
					if (ImGui::Selectable(m_Session->runLabel(candidate).c_str(), row.userChoice && row.energyRun == candidate))
					{
						m_Session->choiceFor(row).energyRun = run.directory;
						m_Session->Changed();
					}
					ImGui::SetItemTooltip("%s, PRECFOCK %s, ENCUT %.0f eV\nTOTEN %s eV\n%s",
						run.method.hybrid ? "HSE" : "PBE", run.method.precFock.empty() ? "-" : run.method.precFock.c_str(),
						run.method.encut, run.totalEnergy ? std::format("{:.8f}", *run.totalEnergy).c_str() : "brak",
						run.directory.Utf8().c_str());
				}
				ImGui::EndCombo();
			}
		}

		ImGui::TableNextColumn(); // TOTEN of the energy run
		if (const auto &runs = m_Session->m_File->runs; row.energyRun && *row.energyRun < runs.size() && runs[*row.energyRun].totalEnergy)
			ImGui::Text("%.4f", *runs[*row.energyRun].totalEnergy);
		else
			ImGui::TextDisabled("-");

		ImGui::TableNextColumn();
		{
			const bool own = std::any_of(m_Session->m_File->choices.begin(), m_Session->m_File->choices.end(), [&](const auto &choice) {
				return choice.defect == row.defect && choice.charge == row.charge && choice.locpot;
			});
			const float button = ImGui::GetFrameHeight();
			const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
			std::string shown = row.locpot.Empty() ? "brak" : row.locpot.parent_path().filename().Utf8() + "/" + row.locpot.filename().Utf8();
			ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - (own ? 2.0f : 1.0f) * (button + spacing)));
			ImGui::InputText("##locpot", shown.data(), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
			ImGui::SetItemTooltip("%s", row.locpot.Empty() ? "Obliczenie energii nie ma LOCPOT - wskaż plik." : row.locpot.Utf8().c_str());
			ImGui::SameLine(0.0f, spacing);
			if (ImGui::Button("...", ImVec2(button, 0.0f)))
			{
				const auto picked = Platform::PickOpenFile(row.locpot.Empty() ? m_Session->m_File->root : row.locpot.parent_path(), "", "");
				if (picked && picked->has_value())
				{
					m_Session->choiceFor(row).locpot = **picked;
					m_Session->Changed();
				}
			}
			ImGui::SetItemTooltip("Inny LOCPOT dla FNV niż z obliczenia energii (np. GeN q-1 w pracy inż.).");
			if (own)
			{
				ImGui::SameLine(0.0f, spacing);
				if (ImGui::Button("x", ImVec2(button, 0.0f)))
				{
					m_Session->choiceFor(row).locpot.reset();
					m_Session->Changed();
				}
				ImGui::SetItemTooltip("Wróć do LOCPOT obliczenia energii.");
			}
		}

		ImGui::TableNextColumn();
		if (row.charge == 0)
			ImGui::TextDisabled("0");
		else if (row.correction)
		{
			if (ImGui::SmallButton(std::format("{:.4f}", row.correction->correction).c_str()))
				if (const auto panel = m_Session->m_FnvPanel.lock())
					panel->ShowCorrection(row.locpot);
			ImGui::SetItemTooltip("E_lat %.4f, C %.4f (rozrzut osi %.4f) eV - kliknij, by otworzyć w FNV.",
				row.correction->latticeEnergy, row.correction->alignment, row.correction->alignmentSpread);
		}
		else
			ImGui::TextColored(kWarningColor, "brak");

		ImGui::TableNextColumn();
		const auto state = EnergeticsRowState(*m_Session->m_File, row);
		const auto formation = state ? FormationEnergy(*state, m_Session->m_Model.reference, mu, 0.0) : std::nullopt;
		if (formation)
			ImGui::Text("%.4f", *formation);
		else if (!row.energyRun && !row.candidates.empty())
		{
			ImGui::TextColored(kWarningColor, "wybierz");
			ImGui::SetItemTooltip("Żadne obliczenie nie jest zbieżne i zakończone (%s).\n"
				"Wybierz je ręcznie w kolumnie \"Obliczenie energii\", jeśli mimo to ma być użyte.", row.proposalReason.c_str());
		}
		else
			ImGui::TextDisabled("-");

		ImGui::TableNextColumn();
		if (!row.problems.empty())
		{
			ImGui::TextColored(kWarningColor, "(!) %zu", row.problems.size());
			if (ImGui::BeginItemTooltip())
			{
				for (const auto &problem : row.problems)
					ImGui::BulletText("%s", problem.c_str());
				ImGui::EndTooltip();
			}
		}
		ImGui::PopID();
	}

} // namespace DefectStudio
