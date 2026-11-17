#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <format>
#include <imgui.h>
#include "Core/Platform/FileDialog.hpp"

namespace DefectStudio
{
	namespace
	{
		void DrawRole(std::string_view label, PhononFolderRole role, Path &pick, const PhononFolderProposal &proposal,
			const Path &project, const std::vector<ProjectRootEntry> &roots)
		{
			ImGui::PushID(label.data());
			ImGui::TextUnformatted(label.data());
			const auto preview = pick.Empty() ? "(brak)" : PhononPanelSession::DirectoryLabel(pick, project, roots);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##role", preview.c_str()))
			{
				if (ImGui::Selectable("(brak)", pick.Empty())) pick = {};
				ImGui::SetItemTooltip("Nie przypisuj katalogu do tej roli.");
				for (const auto &candidate : proposal.candidates)
					if (candidate.role == role)
					{
						const auto path = PhononPanelSession::DirectoryLabel(candidate.directory, project, roots);
						if (ImGui::Selectable(path.c_str(), candidate.directory == pick)) pick = candidate.directory;
						ImGui::SetItemTooltip("%s", candidate.directory.Utf8().c_str());
					}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("%s", pick.Empty() ? "Brak wybranego katalogu; wybierz kandydata z listy." : pick.Utf8().c_str());
			ImGui::PopID();
		}
	}

	void DrawPhononDefectFolders(PhononPanelSession &session)
	{
		ImGui::BeginDisabled(session.IsScanningDatasets());
		if (ImGui::Button("Dodaj defekt z folderu…"))
		{
			const auto picked = Platform::PickFolder(session.m_ProjectDirectory);
			if (!picked) session.m_Error = picked.Error().userMessage;
			else if (picked->has_value()) session.AddDefectFolders({**picked});
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", session.IsScanningDatasets() ? "Poczekaj na skan lub go anuluj." :
			"Wybierz folder defektu lub wielu defektów, także na P:. Zaproponuj pary GS + ES i wczytaj gotowe wyniki.");
		for (std::size_t i = 0; i < session.Settings().defectRoots.size(); ++i)
		{
			const Path root = session.Settings().defectRoots[i];
			ImGui::PushID(static_cast<int>(i));
			const bool remove = ImGui::SmallButton("x");
			ImGui::SetItemTooltip("Usuń zapamiętany folder; przypadki i pliki zostają.");
			ImGui::SameLine();
			const auto label = PhononPanelSession::DirectoryLabel(root, session.m_ProjectDirectory, session.m_ProjectRoots);
			ImGui::TextUnformatted(label.c_str());
			ImGui::SetItemTooltip("%s", root.Utf8().c_str());
			ImGui::PopID();
			if (remove) { session.RemoveDefectRoot(root); break; }
		}
		if (!session.Settings().defectRoots.empty())
		{
			ImGui::BeginDisabled(session.IsScanningDatasets());
			if (ImGui::SmallButton("Sprawdź nowe wyniki")) session.RescanDefectFolders();
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("%s", session.IsScanningDatasets() ? "Skan już trwa." : "Przeskanuj zapamiętane foldery i sprawdź daty wyników HR.");
		}
		if (session.IsScanningDatasets())
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Anuluj skan folderów")) session.CancelDatasetScan();
			ImGui::SetItemTooltip("Przerwij bieżący skan; istniejące przypadki zostają.");
		}
		if (!session.DatasetScanMessage().empty()) ImGui::TextWrapped("%s", session.DatasetScanMessage().c_str());
		if (std::exchange(session.m_DefectFolderPopupRequested, false)) ImGui::OpenPopup("Nowe przypadki z folderu");
		ImGui::SetNextWindowSize(ImVec2(900.0f, 650.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::BeginPopupModal("Nowe przypadki z folderu", nullptr, ImGuiWindowFlags_None)) return;
		if (ImGui::BeginChild("##proposals", ImVec2(0.0f, -2.0f * ImGui::GetFrameHeightWithSpacing())))
			for (std::size_t index = 0; index < session.m_DefectFolderSelections.size(); ++index)
			{
				auto &selection = session.m_DefectFolderSelections[index];
				auto &proposal = selection.proposal;
				ImGui::PushID(static_cast<int>(index));
				ImGui::Separator();
				ImGui::Checkbox("Utwórz przypadek", &selection.selected);
				ImGui::SetItemTooltip("Dodaj przypadek z wybranymi katalogami i wynikami.");
				ImGui::TextUnformatted("Nazwa");
				std::vector<char> name(proposal.name.begin(), proposal.name.end());
				name.resize(name.size() + 1024, '\0');
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##name", name.data(), name.size())) proposal.name = name.data();
				ImGui::SetItemTooltip("Nazwa przypadku w tabeli i porównaniu.");
				if (!selection.existingCase.empty()) ImGui::TextDisabled("już w przypadku %s", selection.existingCase.c_str());
				const auto path = PhononPanelSession::DirectoryLabel(proposal.systemDirectory, session.m_ProjectDirectory, session.m_ProjectRoots);
				ImGui::TextWrapped("%s", path.c_str());
				ImGui::SetItemTooltip("%s", proposal.systemDirectory.Utf8().c_str());
				DrawRole("GS", PhononFolderRole::GroundState, proposal.groundStateDirectory, proposal, session.m_ProjectDirectory, session.m_ProjectRoots);
				DrawRole("ES", PhononFolderRole::ExcitedState, proposal.excitedStateDirectory, proposal, session.m_ProjectDirectory, session.m_ProjectRoots);
				DrawRole("Triplet", PhononFolderRole::Triplet, proposal.tripletDirectory, proposal, session.m_ProjectDirectory, session.m_ProjectRoots);
				DrawRole("GS@ES", PhononFolderRole::GroundAtExcited, proposal.groundAtExcitedDirectory, proposal, session.m_ProjectDirectory, session.m_ProjectRoots);
				DrawRole("FC", PhononFolderRole::ForceConstants, proposal.forceConstantDirectory, proposal, session.m_ProjectDirectory, session.m_ProjectRoots);
				ImGui::Text("Wyniki serwera (%zu)", proposal.serverResults.size());
				for (std::size_t i = 0; i < proposal.serverResults.size(); ++i)
				{
					const auto &hit = proposal.serverResults[i];
					bool checked = selection.serverChecked[i];
					const auto label = std::format("{}x{}x{} / {}##hr{}", hit.supercell[0], hit.supercell[1], hit.supercell[2], hit.variant, i);
					if (ImGui::Checkbox(label.c_str(), &checked)) selection.serverChecked[i] = checked;
					ImGui::SetItemTooltip("%s", hit.hrDirectory.Utf8().c_str());
				}
				ImGui::Text("Zestawy aplikacji (%zu)", proposal.datasets.size());
				for (std::size_t i = 0; i < proposal.datasets.size(); ++i)
				{
					const auto &dataset = proposal.datasets[i];
					bool checked = selection.datasetChecked[i];
					const auto label = dataset.label + "##dataset" + std::to_string(i);
					if (ImGui::Checkbox(label.c_str(), &checked)) selection.datasetChecked[i] = checked;
					ImGui::SetItemTooltip("%s", dataset.directory.Utf8().c_str());
				}
				ImGui::PopID();
			}
		ImGui::EndChild();
		std::size_t count = 0;
		bool missingPair = false, needsProject = false;
		for (const auto &selection : session.m_DefectFolderSelections)
			if (selection.selected)
			{
				++count;
				missingPair = missingPair || selection.proposal.groundStateDirectory.Empty() || selection.proposal.excitedStateDirectory.Empty();
				needsProject = needsProject || std::ranges::any_of(selection.serverChecked, [](bool value) { return value; });
			}
		const std::string blocker = session.IsBusy() ? "Poczekaj na zakończenie bieżącego zadania." : count == 0 ? "Zaznacz przypadki." :
			missingPair ? "Wybierz GS i ES dla każdego zaznaczonego przypadku." :
			needsProject && session.m_JobSystem.expired() ? "JobSystem niedostępny." :
			needsProject && session.m_ProjectDirectory.Empty() ? "Otwórz projekt przed importem wyników serwera." : "";
		ImGui::BeginDisabled(!blocker.empty());
		if (ImGui::Button(std::format("Utwórz zaznaczone ({})", count).c_str()))
		{
			(void)session.CreateCasesFromFolders(session.m_DefectFolderSelections);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", blocker.empty() ? "Utwórz przypadki, przypnij zestawy i dodaj wyniki serwera do kolejki importu." : blocker.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Anuluj")) ImGui::CloseCurrentPopup();
		ImGui::SetItemTooltip("Zamknij bez tworzenia przypadków; folder zostaje zapamiętany.");
		ImGui::EndPopup();
	}
}
