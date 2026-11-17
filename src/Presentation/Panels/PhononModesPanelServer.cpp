#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <format>
#include <imgui.h>

#include "Core/Platform/FileDialog.hpp"

namespace DefectStudio
{
	void PhononModesPanel::drawServerImport()
	{
		auto &session = *m_Session;
		const std::string blocker = session.IsBusy() ? "Poczekaj na zakończenie bieżącego zadania." :
			session.m_ProjectDirectory.Empty() ? "Otwórz projekt; wyniki będą zapisane w jego katalogu." :
			session.ActiveCaseId().empty() ? "Wybierz aktywny przypadek." :
			session.m_JobSystem.expired() ? "JobSystem niedostępny." : std::string{};
		ImGui::BeginDisabled(!blocker.empty() || session.IsScanningDatasets());
		if (ImGui::Button("Wczytaj wyniki z serwera…"))
		{
			const auto picked = Platform::PickFolder({});
			if (!picked) session.m_Error = picked.Error().userMessage;
			else if (picked->has_value()) session.startServerScan(**picked);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", !blocker.empty() ? blocker.c_str() : session.IsScanningDatasets() ?
			"Poczekaj na skan lub go anuluj." : "Wybierz katalog na zamontowanym serwerze. Skan nie zapisuje tam plików; import trafia do projektu.");
		if (session.m_ScanningServer)
		{
			ImGui::SameLine();
			if (ImGui::Button("Anuluj skan HR")) session.CancelDatasetScan();
			ImGui::SetItemTooltip("Przerwij skanowanie serwera.");
			ImGui::TextDisabled("%s", session.DatasetScanMessage().c_str());
		}
		if (!session.m_ServerRuns.empty())
		{
			const auto done = std::count_if(session.m_ServerRuns.begin(), session.m_ServerRuns.end(), [](const auto &entry) {
				return entry.second.status == PhononCaseStatus::Done || entry.second.status == PhononCaseStatus::Failed ||
					entry.second.status == PhononCaseStatus::Cancelled;
			});
			ImGui::Text("Import HR: %zu / %zu", static_cast<std::size_t>(done), session.m_ServerRuns.size());
			for (const auto &hit : session.m_ServerResults)
			{
				const auto run = session.m_ServerRuns.find(hit.hrDirectory.Utf8());
				if (run == session.m_ServerRuns.end()) continue;
				const auto &value = run->second;
				const std::string_view status = value.status == PhononCaseStatus::Queued ? "w kolejce" :
					value.status == PhononCaseStatus::Running ? "wczytywanie" : value.status == PhononCaseStatus::Done ? "gotowe" :
					value.status == PhononCaseStatus::Cancelled ? "anulowano" : "błąd";
				const auto label = PhononPanelSession::DirectoryLabel(hit.hrDirectory, session.m_ProjectDirectory, session.m_ProjectRoots);
				ImGui::Text("%s: %s", label.c_str(), status.data());
				ImGui::SetItemTooltip("%s\n%s", hit.hrDirectory.Utf8().c_str(), value.message.c_str());
			}
			if (session.IsBatchRunning() && static_cast<std::size_t>(done) < session.m_ServerRuns.size())
			{
				if (ImGui::Button("Anuluj import HR")) session.CancelBatch();
				ImGui::SetItemTooltip("Anuluj bieżący import i pozostałe wpisy w kolejce.");
			}
		}
		if (std::exchange(session.m_ServerPopupRequested, false)) ImGui::OpenPopup("Wyniki HR z serwera");
		ImGui::SetNextWindowSize(ImVec2(900.0f, 500.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::BeginPopupModal("Wyniki HR z serwera", nullptr, ImGuiWindowFlags_None)) return;
		if (ImGui::Button("Zaznacz wszystkie")) std::fill(session.m_ServerChecked.begin(), session.m_ServerChecked.end(), true);
		ImGui::SetItemTooltip("Uwzględnij wszystkie znalezione wyniki.");
		ImGui::SameLine();
		if (ImGui::Button("Odznacz")) std::fill(session.m_ServerChecked.begin(), session.m_ServerChecked.end(), false);
		ImGui::SetItemTooltip("Wyczyść zaznaczenie.");
		if (ImGui::BeginTable("##serverHr", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders,
			ImVec2(0.0f, std::max(100.0f, ImGui::GetContentRegionAvail().y - 70.0f))))
		{
			for (const std::string_view label : {"Wczytaj", "System", "Superkomórka", "Wariant", "Cutoff [Å]"}) ImGui::TableSetupColumn(label.data());
			ImGui::TableHeadersRow();
			for (std::size_t i = 0; i < session.m_ServerResults.size(); ++i)
			{
				const auto &hit = session.m_ServerResults[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				bool checked = session.m_ServerChecked[i];
				if (ImGui::Checkbox("##import", &checked)) session.m_ServerChecked[i] = checked;
				ImGui::SetItemTooltip("Dodaj ten wynik do aktywnego przypadku.");
				ImGui::TableSetColumnIndex(1);
				auto label = PhononPanelSession::DirectoryLabel(hit.systemDirectory, session.m_ProjectDirectory, session.m_ProjectRoots);
				if (label == hit.systemDirectory.Utf8())
				{
					const auto ancestor = hit.systemDirectory.parent_path().parent_path().parent_path().parent_path();
					label = "…/" + PhononPanelSession::DirectoryLabel(hit.systemDirectory, ancestor, {});
				}
				ImGui::TextUnformatted(label.c_str());
				ImGui::SetItemTooltip("%s", hit.systemDirectory.Utf8().c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%dx%dx%d", hit.supercell[0], hit.supercell[1], hit.supercell[2]);
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(hit.variant.c_str());
				ImGui::SetItemTooltip("%s", hit.hrDirectory.Utf8().c_str());
				ImGui::TableSetColumnIndex(4);
				if (hit.cutoffAngstrom) ImGui::Text("%.4g", *hit.cutoffAngstrom);
				else ImGui::TextUnformatted("auto");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		const auto count = std::count(session.m_ServerChecked.begin(), session.m_ServerChecked.end(), true);
		ImGui::BeginDisabled(count == 0 || !blocker.empty());
		if (ImGui::Button(std::format("Wczytaj zaznaczone ({})", count).c_str()))
		{
			session.importServerResults();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", !blocker.empty() ? blocker.c_str() : count == 0 ? "Zaznacz wyniki do importu." :
			"Wczytaj kolejno z ustawieniami σ / γ / ZPL aktywnego przypadku. Ponowny import zastępuje jego poprzedni wynik w projekcie.");
		ImGui::SameLine();
		if (ImGui::Button("Zamknij")) ImGui::CloseCurrentPopup();
		ImGui::SetItemTooltip("Zamknij listę bez importu.");
		ImGui::EndPopup();
	}
}
