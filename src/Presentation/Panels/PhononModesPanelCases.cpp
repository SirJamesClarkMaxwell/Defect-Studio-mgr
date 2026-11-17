#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <imgui.h>
#include <algorithm>
#include <format>

namespace DefectStudio
{
	void DrawPhononDefectFolders(PhononPanelSession &session);

	void DrawPhononCases(PhononPanelSession &session)
	{
		const bool shown = ImGui::CollapsingHeader("Przypadki", ImGuiTreeNodeFlags_DefaultOpen);
		ImGui::SetItemTooltip("Jedno przejście GS → ES jednego defektu; warianty wyników są zestawami w przypadku.");
		if (!shown) { DrawPhononDefectFolders(session); return; }
		std::string activate, duplicate, remove, renameId, rename, hideId, importId;
		bool hide = false;
		if (ImGui::BeginTable("##cases", 6, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("liczyć", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Nazwa");
			ImGui::TableSetupColumn("Zestawy", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("ZPL [eV]", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("S", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			auto cases = session.Cases();
			std::stable_sort(cases.begin(), cases.end(), [](const auto &a, const auto &b) { return a.hidden < b.hidden; });
			for (const auto &item : cases)
			{
				ImGui::PushID(item.id.c_str());
				if (item.hidden) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
				ImGui::TableNextRow();
				if (!item.hidden && item.id == session.ActiveCaseId())
				{
					const auto accent = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.35f);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, accent);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, accent);
				}
				ImGui::TableSetColumnIndex(0);
				bool checked = session.CaseChecked(item.id);
				ImGui::BeginDisabled(item.hidden);
				if (ImGui::Checkbox("##compute", &checked)) session.SetCaseChecked(item.id, checked);
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("%s", item.hidden ? "Pokaż przypadek, aby uwzględnić go w obliczeniach." :
					"Uwzględnij w kolejce obliczeń. Wybór obowiązuje w tej sesji.");
				ImGui::TableSetColumnIndex(1);
				if (ImGui::Selectable(item.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns |
					ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, ImGui::GetFrameHeight()))) activate = item.id;
				ImGui::SetItemTooltip("Aktywuj przypadek: wejścia, wyniki i okno superkomórki. RMB: opcje.");
				if (ImGui::BeginPopupContextItem("##caseActions"))
				{
					ImGui::TextUnformatted("Zmień nazwę");
					std::vector<char> text(item.name.begin(), item.name.end());
					text.resize(text.size() + 1024, '\0');
					if (ImGui::InputText("##name", text.data(), text.size()))
					{
						renameId = item.id;
						rename = text.data();
					}
					ImGui::SetItemTooltip("Nazwa przypadku widoczna na liście i w tytule jego okna.");
					if (ImGui::MenuItem("Duplikuj")) duplicate = item.id;
					ImGui::SetItemTooltip("Kopia ustawień bez przypiętych zestawów i nadpisań katalogów wynikowych.");
					if (ImGui::MenuItem(item.hidden ? "Pokaż" : "Ukryj")) { hideId = item.id; hide = !item.hidden; }
					ImGui::SetItemTooltip("Zostaje na liście (przygaszony, na dole), poza wykresami, porównaniem i eksportem.");
					if (ImGui::MenuItem("Usuń z listy")) remove = item.id;
					ImGui::SetItemTooltip("Pliki zostają. Usuwa tylko przypadek i przypięcia na tej liście.");
					ImGui::EndPopup();
				}
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%zu", item.datasets.size());
				const auto result = session.CachedCaseResult(item.id);
				ImGui::TableSetColumnIndex(3);
				if (result)
				{
					const auto &zpl = result->get().zplEv;
					auto value = item.settings.plZpl == "formula" ? zpl.formula :
						item.settings.plZpl == "experimental" ? zpl.experimental :
						item.settings.plZpl == "spin_purified" ? zpl.spinPurified : zpl.computed;
					for (const auto candidate : {zpl.computed, zpl.spinPurified, zpl.experimental, zpl.formula})
						if (!value) value = candidate;
					if (value) ImGui::Text("%.4f", *value);
					else ImGui::TextUnformatted("-");
				}
				else ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(4);
				if (result) ImGui::Text("%.4f", result->get().sTotal);
				else ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(5);
				const auto &run = session.CaseRun(item.id);
				std::string_view status = "-";
				switch (run.status)
				{
					case PhononCaseStatus::Queued: status = "w kolejce"; break;
					case PhononCaseStatus::Running: status = "liczy…"; break;
					case PhononCaseStatus::Done: status = "gotowe"; break;
					case PhononCaseStatus::Failed: status = "błąd"; break;
					case PhononCaseStatus::Cancelled: status = "anulowano"; break;
					case PhononCaseStatus::Idle: break;
				}
				ImGui::TextUnformatted(status.data());
				if (!run.message.empty()) ImGui::SetItemTooltip("%s", run.message.c_str());
				const auto &newResults = session.NewServerResults(item.id);
				if (!newResults.empty())
				{
					ImGui::Text("%zu nowych wyników", newResults.size());
					if (ImGui::BeginItemTooltip())
					{
						for (const auto &entry : newResults)
							ImGui::Text("%dx%dx%d / %s%s\n%s", entry.result.supercell[0], entry.result.supercell[1], entry.result.supercell[2],
								entry.result.variant.c_str(), entry.updated ? " (zaktualizowane na serwerze)" : "", entry.result.hrDirectory.Utf8().c_str());
						ImGui::EndTooltip();
					}
					ImGui::BeginDisabled(session.IsBusy() || session.m_ProjectDirectory.Empty());
					if (ImGui::SmallButton("Wczytaj")) importId = item.id;
					ImGui::EndDisabled();
					ImGui::SetItemTooltip("%s", session.IsBusy() ? "Poczekaj na zakończenie bieżącego zadania." :
						session.m_ProjectDirectory.Empty() ? "Otwórz projekt przed importem." : "Wczytaj nowe i zaktualizowane wyniki do tego przypadku.");
				}
				if (item.hidden) ImGui::PopStyleVar();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::TextUnformatted("Kroki:");
		ImGui::SameLine();
		ImGui::BeginDisabled(session.IsBatchRunning());
		ImGui::Checkbox("fonony", &session.m_BatchPhonons);
		ImGui::SetItemTooltip("Policz fonony dla zaznaczonych widocznych przypadków.");
		ImGui::SameLine();
		ImGui::Checkbox("analiza CCD", &session.m_BatchCcdAnalysis);
		ImGui::SetItemTooltip("Uruchom analizę CCD dla zaznaczonych widocznych przypadków.");
		ImGui::EndDisabled();
		std::vector<std::string> checked;
		for (const auto &item : session.Cases())
			if (session.CaseChecked(item.id)) checked.push_back(item.id);
		const std::string reason = session.IsBatchRunning() ? "Kolejka już działa." :
			session.IsBusy() ? "Poczekaj na zakończenie bieżącego zadania." :
			checked.empty() ? "Zaznacz przypadki do obliczenia." :
			!session.m_BatchPhonons && !session.m_BatchCcdAnalysis ? "Wybierz co najmniej jeden krok." : "";
		ImGui::BeginDisabled(!reason.empty());
		if (ImGui::Button(std::format("Policz zaznaczone ({})", checked.size()).c_str()))
		{
			std::vector<PhononBatchStep> steps;
			if (session.m_BatchPhonons) steps.push_back(PhononBatchStep::Phonons);
			if (session.m_BatchCcdAnalysis) steps.push_back(PhononBatchStep::CcdAnalysis);
			session.StartBatch(std::move(checked), std::move(steps));
		}
		ImGui::EndDisabled();
		if (!reason.empty()) ImGui::SetItemTooltip("%s", reason.c_str());
		if (session.IsBatchRunning())
		{
			const auto [done, total] = session.BatchProgress();
			const auto label = std::format("{} / {}", done, total);
			ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f,
				ImVec2(-1.0f, 0.0f), label.c_str());
			if (ImGui::Button("Anuluj##batch")) session.CancelBatch();
		}
		// Release table references before changing the list or loading another result.
		if (!renameId.empty()) session.RenameCase(renameId, std::move(rename));
		if (!hideId.empty()) session.SetCaseHidden(hideId, hide);
		if (!importId.empty()) session.ImportNewServerResults(importId);
		if (!remove.empty()) session.RemoveCase(remove);
		else if (!duplicate.empty()) (void)session.DuplicateCase(duplicate);
		else if (!activate.empty() && importId.empty() && hideId.empty()) (void)session.ActivateCase(activate);
		if (ImGui::Button("Nowy przypadek")) (void)session.AddCase({});
		ImGui::SetItemTooltip("Dodaj pusty przypadek i ustaw go jako aktywny.");
		ImGui::SameLine();
		DrawPhononDefectFolders(session);
		const bool available = session.HasUnattachedCaseDatasets();
		ImGui::BeginDisabled(!available);
		if (ImGui::Button("Przypadki z zestawów")) (void)session.CreateCasesFromDatasets();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", available ? "Grupuj nieprzypięte wyniki według pary GS/ES z pochodzenia danych." :
			"Brak nieprzypiętych zestawów fononów / CCD z katalogami GS i ES w podsumowaniu.");
	}
}
