#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/FnvCorrectionInputs.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"
#include "ScientificRuntime/Python/FnvCorrectionBridge.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr ImVec4 kWarningColor(0.95f, 0.75f, 0.30f, 1.0f);

		std::string ChargeText(const int charge)
		{
			return charge > 0 ? std::format("+{}", charge) : std::format("{}", charge);
		}
	} // namespace

	// One line per (defect, q): whether it counts, the run whose TOTEN is used (auto = the
	// proposal), the LOCPOT that goes to FNV, E_corr and E_f(0) under the chosen mu set.
	void DefectEnergeticsPanel::drawRows()
	{
		if (!ImGui::CollapsingHeader("Obliczenia", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		if (m_Model.rows.empty())
		{
			ImGui::TextDisabled(m_File->runs.empty() ? "Wskaż folder i kliknij Skanuj."
				: "Brak obliczeń defektów (sprawdź bulk).");
			return;
		}
		const SpeciesEnergies mu = m_Model.muSets.empty() ? SpeciesEnergies{}
			: AbsoluteChemicalPotentials(m_Model.chemicalPotentials, m_Model.muSets[static_cast<std::size_t>(m_MuSet)].deltaMu);
		constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
		if (!ImGui::BeginTable("##EnergeticsRows", 8, flags))
			return;
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Defekt", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("q", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Obliczenie energii (zbieżne)", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("LOCPOT dla FNV", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("E_corr [eV]", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("E_f(0) [eV]", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Uwagi", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();
		std::string previousDefect;
		for (std::size_t r = 0; r < m_Model.rows.size(); ++r)
		{
			const EnergeticsRow &row = m_Model.rows[r];
			ImGui::PushID(static_cast<int>(r));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			bool included = row.included;
			if (ImGui::Checkbox("##included", &included))
			{
				choiceFor(row).included = included;
				changed();
			}
			ImGui::SetItemTooltip("Uwzględnij w wykresie, obwiedni i eksporcie.");
			ImGui::TableNextColumn();
			if (row.defect != previousDefect)
				ImGui::TextUnformatted(row.defect.c_str());
			previousDefect = row.defect;
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(ChargeText(row.charge).c_str());

			ImGui::TableNextColumn();
			{
				const std::string preview = row.energyRun
					? runLabel(*row.energyRun) + (row.userChoice ? "" : "  (auto)")
					: "wybierz: " + row.proposalReason;
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##energyRun", preview.c_str()))
				{
					if (ImGui::Selectable(("auto: " + row.proposalReason).c_str(), !row.userChoice))
					{
						choiceFor(row).energyRun.reset();
						changed();
					}
					for (const std::size_t candidate : row.candidates)
					{
						const VaspRunRecord &run = m_File->runs[candidate];
						if (ImGui::Selectable(runLabel(candidate).c_str(), row.userChoice && row.energyRun == candidate))
						{
							choiceFor(row).energyRun = run.directory;
							changed();
						}
						ImGui::SetItemTooltip("%s, PRECFOCK %s, ENCUT %.0f eV\nTOTEN %s eV\n%s",
							run.method.hybrid ? "HSE" : "PBE", run.method.precFock.empty() ? "-" : run.method.precFock.c_str(),
							run.method.encut, run.totalEnergy ? std::format("{:.8f}", *run.totalEnergy).c_str() : "brak",
							run.directory.Utf8().c_str());
					}
					ImGui::EndCombo();
				}
			}

			ImGui::TableNextColumn();
			{
				const bool own = std::any_of(m_File->choices.begin(), m_File->choices.end(), [&](const auto &choice) {
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
					const auto picked = Platform::PickOpenFile(row.locpot.Empty() ? m_File->root : row.locpot.parent_path(), "", "");
					if (picked && picked->has_value())
					{
						choiceFor(row).locpot = **picked;
						changed();
					}
				}
				ImGui::SetItemTooltip("Inny LOCPOT dla FNV niż z obliczenia energii (np. GeN q-1 w pracy inż.).");
				if (own)
				{
					ImGui::SameLine(0.0f, spacing);
					if (ImGui::Button("x", ImVec2(button, 0.0f)))
					{
						choiceFor(row).locpot.reset();
						changed();
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
					if (const auto panel = m_FnvPanel.lock())
						panel->ShowCorrection(row.locpot);
				ImGui::SetItemTooltip("E_lat %.4f, C %.4f (rozrzut osi %.4f) eV - kliknij, by otworzyć w FNV.",
					row.correction->latticeEnergy, row.correction->alignment, row.correction->alignmentSpread);
			}
			else
				ImGui::TextColored(kWarningColor, "brak");

			ImGui::TableNextColumn();
			const auto state = EnergeticsRowState(*m_File, row);
			const auto formation = state ? FormationEnergy(*state, m_Model.reference, mu, 0.0) : std::nullopt;
			if (formation)
				ImGui::Text("%.4f", *formation);
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
		ImGui::EndTable();

		// FNV queue
		if (m_FnvRunning || !m_FnvQueue.empty())
		{
			ImGui::ProgressBar(m_FnvTotal ? static_cast<float>(m_FnvDone) / static_cast<float>(m_FnvTotal) : 0.0f,
				ImVec2(-ImGui::CalcTextSize("Anuluj").x - 3.0f * ImGui::GetStyle().FramePadding.x, 0.0f),
				m_FnvRunning ? std::format("FNV {}/{}: {} q={}", m_FnvDone + 1, m_FnvTotal, m_FnvRunning->defect,
					ChargeText(m_FnvRunning->charge)).c_str() : nullptr);
			ImGui::SameLine();
			if (ImGui::Button("Anuluj##fnv"))
			{
				m_FnvQueue.clear();
				if (const auto jobSystem = m_JobSystem.lock(); jobSystem && m_FnvJobId)
					(void)jobSystem->RequestCancel(m_FnvJobId);
			}
		}
		else
		{
			std::size_t missing = 0;
			for (const auto &row : m_Model.rows)
				missing += row.included && row.charge != 0 && !row.correction && !row.locpot.Empty() ? 1 : 0;
			ImGui::BeginDisabled(missing == 0 || !m_Model.bulk);
			if (ImGui::Button(std::format("Policz brakujące FNV ({})", missing).c_str()))
				startFnvBatch();
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Kolejno, jedno naraz: profile pymatgen (2 LOCPOT, ~1 min lokalnie, kilka minut po "
				"sieci), środek z domieszek, okna plateau zaproponowane. Okna i środek poprawisz potem w panelu FNV.");
		}
	}

	void DefectEnergeticsPanel::startFnvBatch()
	{
		if (!m_Model.bulk)
			return;
		const Path bulkLocpot = m_File->runs[*m_Model.bulk].directory / Path::FromUtf8("LOCPOT");
		const auto bulkStructure = ReadLocpotStructure(bulkLocpot);
		std::vector<std::string> skipped;
		for (const auto &row : m_Model.rows)
		{
			if (!row.included || row.charge == 0 || row.correction || row.locpot.Empty())
				continue;
			const auto defectStructure = ReadLocpotStructure(row.locpot);
			const auto centre = defectStructure && bulkStructure ? FindImpurityCentre(*defectStructure, *bulkStructure)
				: std::nullopt;
			if (!centre)
			{
				skipped.push_back(std::format("{} q={}", row.defect, ChargeText(row.charge)));
				continue;
			}
			FnvTask task;
			task.defect = row.defect;
			task.charge = row.charge;
			task.entry.defectLocpot = row.locpot;
			task.entry.bulkLocpot = bulkLocpot;
			task.entry.charge = row.charge;
			task.entry.dielectric = m_File->dielectric;
			task.entry.dielectricSource = m_File->dielectricSource;
			task.entry.centreFractional = centre->fractional;
			task.entry.convention = FnvModelConvention::Sxdefectalign;
			m_FnvQueue.push_back(std::move(task));
		}
		m_FnvDone = 0;
		m_FnvTotal = m_FnvQueue.size();
		m_Message = skipped.empty() ? std::string() : "Bez środka (brak domieszki - ustaw w FNV): " + [&] {
			std::string list;
			for (const auto &name : skipped)
				list += (list.empty() ? "" : ", ") + name;
			return list;
		}();
		submitNextFnv();
	}

	void DefectEnergeticsPanel::submitNextFnv()
	{
		const auto jobSystem = m_JobSystem.lock();
		if (!jobSystem || m_FnvQueue.empty())
			return;
		m_FnvRunning = std::move(m_FnvQueue.front());
		m_FnvQueue.pop_front();
		FnvProfileRequest request;
		request.defectLocpot = m_FnvRunning->entry.defectLocpot;
		request.bulkLocpot = m_FnvRunning->entry.bulkLocpot;
		request.charge = m_FnvRunning->entry.charge;
		request.dielectric = m_FnvRunning->entry.dielectric;
		request.centreFractional = m_FnvRunning->entry.centreFractional;
		request.energyCutoff = m_FnvRunning->entry.energyCutoff;
		m_FnvJob = CreateRef<FnvCorrectionJob>(request);
		m_FnvJobId = jobSystem->Submit(m_FnvJob, JobPriority::Normal);
	}

	void DefectEnergeticsPanel::pollFnvBatch()
	{
		if (!m_FnvJob)
			return;
		const auto jobSystem = m_JobSystem.lock();
		const auto snapshot = jobSystem ? jobSystem->GetJob(m_FnvJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running))
			return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_FnvJob->GetResult() && m_FnvRunning)
		{
			PersistedFnvCorrection entry = std::move(m_FnvRunning->entry);
			entry.profiles = std::move(*m_FnvJob->GetResult());
			for (int a = 0; a < 3; ++a)
				entry.windows[a] = ProposeFnvAlignment(entry.profiles->axes[a],
					FnvModelPotential(*entry.profiles, a, entry.convention)).window;
			if (const auto panel = m_FnvPanel.lock())
				panel->StoreCorrection(std::move(entry));
		}
		else if (m_FnvRunning)
			m_Message = std::format("FNV {} q={}: {}", m_FnvRunning->defect, ChargeText(m_FnvRunning->charge),
				snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "nie powiodło się");
		++m_FnvDone;
		m_FnvJob.reset();
		m_FnvJobId = 0;
		m_FnvRunning.reset();
		submitNextFnv();
	}
} // namespace DefectStudio
