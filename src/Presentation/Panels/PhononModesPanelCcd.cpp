#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <cmath>
#include <imgui.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "IO/CcdAnalysisIO.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"
#include "ScientificRuntime/Python/CcdInputsJob.hpp"
#include "ScientificRuntime/Python/CcdAnalysisJob.hpp"

namespace DefectStudio
{

	void PhononModesPanel::drawCcd()
	{
		if (!ImGui::CollapsingHeader("CCD / PL 1D")) return;
		auto &s = m_Session->Settings();
		const bool busy = m_Session->IsBusy();
		ImGui::TextDisabled("GS, ES, tryplet, dodatkowe energie i ZPL: sekcja Wejścia.");
		ImGui::BeginDisabled(busy);
		{
			PropertyGrid grid("##ccdAnalysisInputs", PropertyGridLabelWidth({"Ścieżka CCD (opcjonalna)", "Wyniki CCD", "sigma [meV]", "T [K]"}));
			if (grid)
			{
				grid.Row("Ścieżka CCD (opcjonalna)");
				if (DrawFolder("ccdPath", s.ccdDirectory, m_Session->m_CcdAnalysisError)) persist();
				ImGui::SetItemTooltip("Katalog obliczonych punktów gs/es. Pusty = pojedyncze punkty GS/ES z Wejścia.");
				grid.Row("Wyniki CCD");
				if (DrawFolder("ccdAnalysisOutput", s.ccdAnalysisOutputDirectory, m_Session->m_CcdAnalysisError, true, m_Session->ccdAnalysisOutputPath())) persist();
				grid.Row("sigma [meV]"); ImGui::InputDouble("##ccdSigma", &s.sigmaMeV, 0, 0, "%.4g");
				if (ImGui::IsItemDeactivatedAfterEdit()) { if (!std::isfinite(s.sigmaMeV) || s.sigmaMeV <= 0) s.sigmaMeV = 6; persist(); }
				grid.Row("T [K]"); ImGui::InputDouble("##ccdTemperature", &s.ccdTemperatureK, 0, 0, "%.4g");
				if (ImGui::IsItemDeactivatedAfterEdit()) { if (!std::isfinite(s.ccdTemperatureK) || s.ccdTemperatureK < 0) s.ccdTemperatureK = 0; persist(); }
			}
		}
		if (ImGui::Checkbox("krzywizna z fononów", &s.ccdPhononCurvature)) persist();
		ImGui::SetItemTooltip("Aktywny zestaw fononów → mod efektywny 1D. Krzywizna z dodatkowych energii ma pierwszeństwo.");
		const auto blocker = m_Session->ccdAnalysisBlocker();
		ImGui::BeginDisabled(!blocker.empty());
		if (ImGui::Button("Policz CCD / PL 1D")) m_Session->SubmitCcdAnalysis();
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::SetItemTooltip("%s", blocker.c_str());
		else if (busy) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		ImGui::SameLine();
		if (ImGui::Button("Wczytaj CCD..."))
		{
			const auto picked = Platform::PickFolder(s.selectedCcdDirectory);
			if (!picked) m_Session->m_CcdAnalysisError = picked.Error().userMessage;
			else if (picked->has_value() && m_Session->loadCcdResult(**picked)) persist();
		}
		ImGui::EndDisabled();
		if (busy) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		if (!blocker.empty()) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s", blocker.c_str());
		if (m_Session->m_PendingCcdAnalysisJob)
		{
			const auto jobs = m_Session->m_JobSystem.lock();
			const auto snapshot = jobs ? jobs->GetJob(m_Session->m_PendingCcdAnalysisJobId) : std::nullopt;
			if (snapshot) ImGui::ProgressBar(snapshot->totalWork > 0 ? snapshot->completedWork / snapshot->totalWork : 0,
				ImVec2(-1, 0), snapshot->currentMessage.c_str());
			if (ImGui::Button("Anuluj analizę CCD"))
			{
				if (m_Session->IsBatchRunning()) m_Session->CancelBatch();
				else m_Session->cancelJob(m_Session->m_PendingCcdAnalysisJobId);
			}
		}
		if (!m_Session->m_CcdAnalysisError.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", m_Session->m_CcdAnalysisError.c_str());
		if (ImGui::TreeNode("Generuj punkty CCD"))
		{
			ImGui::BeginDisabled(busy);
			{
				PropertyGrid grid("##ccdInputs", PropertyGridLabelWidth({"Katalog punktów CCD", "Krok t"}));
				if (grid)
				{
					grid.Row("Katalog punktów CCD");
					if (DrawFolder("ccd", s.ccdOutputDirectory, m_Session->m_CcdError, true, m_Session->ccdOutputPath())) persist();
					grid.Row("Krok t"); ImGui::InputDouble("##ccdStep", &s.ccdStep, 0, 0, "%.4g");
					if (ImGui::IsItemDeactivatedAfterEdit()) { if (!std::isfinite(s.ccdStep) || s.ccdStep <= 0) s.ccdStep = 0.25; persist(); }
				}
			}
			const std::string reason = s.groundStateDirectory.Empty() || s.excitedStateDirectory.Empty() ? "Ustaw GS i ES w sekcji Wejścia." :
				m_Session->ccdOutputPath().Empty() ? "Ustaw katalog punktów CCD lub katalog projektu." : "";
			ImGui::BeginDisabled(!reason.empty());
			if (ImGui::Button("Generuj punkty CCD")) m_Session->SubmitCcdInputs();
			ImGui::EndDisabled();
			if (!reason.empty()) ImGui::SetItemTooltip("%s", reason.c_str());
			else if (busy) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
			ImGui::EndDisabled();
			if (!reason.empty()) ImGui::TextDisabled("%s", reason.c_str());
			ImGui::TextDisabled("Przed sbatch: WAVECAR do es/t_+100 i gs/t_+000");
			ImGui::TreePop();
		}
		if (m_Session->m_PendingCcdJob)
		{
			ImGui::TextDisabled("Generowanie punktów CCD...");
			if (ImGui::Button("Anuluj punkty CCD"))
				m_Session->cancelJob(m_Session->m_PendingCcdJobId);
		}
		if (!m_Session->m_CcdInputsResult.empty()) ImGui::TextUnformatted(m_Session->m_CcdInputsResult.c_str());
		if (!m_Session->m_CcdError.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", m_Session->m_CcdError.c_str());
	}
}
