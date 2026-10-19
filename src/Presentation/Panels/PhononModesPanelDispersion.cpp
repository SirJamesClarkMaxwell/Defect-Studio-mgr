#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <span>
#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"
#include "ScientificRuntime/Python/PhononDispersionJob.hpp"

namespace DefectStudio
{

	void PhononModesPanel::drawDispersion()
	{
		if (!ImGui::CollapsingHeader("Dyspersja bulku")) return;
		auto &settings = m_Session->Settings();
		ImGui::BeginDisabled(m_Session->m_PendingJob != nullptr || m_Session->m_PendingCcdJob != nullptr || m_Session->m_PendingDispersionJob != nullptr || m_Session->m_PendingCcdAnalysisJob != nullptr);
		{
			PropertyGrid grid("##dispersionInputs", PropertyGridLabelWidth({"Katalog FC bulku", "Katalog wyników (opcjonalny)", "npoints"}));
			if (grid)
			{
				grid.Row("Katalog FC bulku");
				const auto previousBulk = settings.bulkForceConstantDirectory;
				if (DrawFolder("dispersionBulk", settings.bulkForceConstantDirectory, m_Session->m_DispersionError))
				{
					const auto directory = settings.bulkForceConstantDirectory;
					settings.bulkForceConstantDirectory = previousBulk;
					m_Session->SetDirectory("bulk", directory);
					persist();
				}
				grid.Row("Katalog wyników (opcjonalny)");
				if (DrawFolder("dispersionOutput", settings.dispersionDirectory, m_Session->m_DispersionError, true, m_Session->dispersionOutputPath()))
				{
					persist();
				}
				grid.Row("npoints");
				ImGui::InputInt("##dispersionPoints", &settings.dispersionPoints);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					settings.dispersionPoints = std::max(2, settings.dispersionPoints);
					persist();
				}
			}
		}
		if (ImGui::Checkbox("tylko w płaszczyźnie (2D)", &settings.dispersionInPlane)) persist();
		const std::string blocker = settings.bulkForceConstantDirectory.Empty() ? "Ustaw katalog FC bulku." :
			m_Session->dispersionOutputPath().Empty() ? "Ustaw katalog projektu lub katalog wyników." :
			PhononPanelSession::OutputContainsInput(m_Session->dispersionOutputPath(), settings.bulkForceConstantDirectory) ?
			"Katalog wyników nie może być katalogiem wejściowym ani go zawierać." : "";
		ImGui::BeginDisabled(!blocker.empty());
		if (ImGui::Button("Policz dyspersję")) m_Session->SubmitDispersion();
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::SetItemTooltip("%s", blocker.c_str());
		else if (m_Session->m_PendingJob || m_Session->m_PendingCcdJob || m_Session->m_PendingDispersionJob || m_Session->m_PendingCcdAnalysisJob) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		ImGui::SameLine();
		if (ImGui::Button("Wczytaj..."))
		{
			const auto picked = Platform::PickFolder(m_Session->dispersionOutputPath());
			if (!picked) m_Session->m_DispersionError = picked.Error().userMessage;
			else if (picked->has_value() && m_Session->loadDispersion(**picked)) persist();
		}
		ImGui::EndDisabled();
		if (m_Session->m_PendingJob || m_Session->m_PendingCcdJob || m_Session->m_PendingDispersionJob || m_Session->m_PendingCcdAnalysisJob) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		if (m_Session->m_PendingDispersionJob)
		{
			const auto jobs = m_Session->m_JobSystem.lock();
			const auto snapshot = jobs ? jobs->GetJob(m_Session->m_PendingDispersionJobId) : std::nullopt;
			if (snapshot)
				ImGui::ProgressBar(snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f,
					ImVec2(-1.0f, 0.0f), snapshot->currentMessage.c_str());
			if (ImGui::Button("Anuluj dyspersję")) m_Session->cancelJob(m_Session->m_PendingDispersionJobId);
		}
		if (!m_Session->m_DispersionError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_Session->m_DispersionError.c_str());
	}
}
