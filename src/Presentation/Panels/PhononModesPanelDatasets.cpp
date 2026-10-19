#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <span>
#include <imgui.h>
#include <implot.h>
#include "Presentation/Panels/PlotWidgets.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"

namespace DefectStudio
{
	void DrawPhononCases(PhononPanelSession &session);

	void PhononModesPanel::drawDatasets()
	{
		DrawPhononCases(*m_Session);
		if (!ImGui::CollapsingHeader("Zestawy", ImGuiTreeNodeFlags_DefaultOpen)) return;
		if (ImGui::Button("Skanuj ponownie")) m_Session->scanDatasets();
		if (m_Session->IsScanningDatasets()) { ImGui::SameLine(); ImGui::TextDisabled("Skanowanie..."); }
		else if (!m_Session->DatasetScanMessage().empty())
		{
			ImGui::SameLine(); ImGui::TextUnformatted(m_Session->DatasetScanMessage().c_str());
		}
		std::optional<AnalysisDatasetRef> selected;
		std::optional<Path> attach, detach;
		const auto proposed = m_Session->ProposedDatasets(m_Session->ActiveCaseId());
		auto displayed = m_Session->Datasets();
		for (const auto &item : m_Session->Cases())
			for (const auto &path : item.datasets)
				if (std::none_of(displayed.begin(), displayed.end(), [&](const auto &dataset) {
					return PhononPanelSession::SameFilePath(path, dataset.directory, m_Session->m_ProjectDirectory);
				}))
					displayed.push_back({PhononPanelSession::SameFilePath(path, item.settings.resultDirectory, m_Session->m_ProjectDirectory) ?
						AnalysisDatasetKind::Phonons : AnalysisDatasetKind::Ccd, path, path.filename().Utf8() + " (bez podsumowania)", {}});
		bool showOther = false;
		const auto groupFor = [&](const AnalysisDatasetRef &dataset) {
			if (dataset.kind == AnalysisDatasetKind::Dispersion || dataset.kind == AnalysisDatasetKind::BulkMode) return 3;
			const auto owner = m_Session->DatasetCaseId(dataset.directory);
			if (!owner.empty() && owner == m_Session->ActiveCaseId()) return 0;
			if (std::any_of(proposed.begin(), proposed.end(), [&](const auto &path) {
				return PhononPanelSession::SameFilePath(path, dataset.directory, m_Session->m_ProjectDirectory);
			})) return 1;
			return 2;
		};
		std::vector<std::reference_wrapper<const AnalysisDatasetRef>> ordered;
		for (const auto &dataset : displayed) ordered.push_back(std::cref(dataset));
		std::stable_sort(ordered.begin(), ordered.end(), [&](const auto &a, const auto &b) { return groupFor(a.get()) < groupFor(b.get()); });
		int previousGroup = -1;
		if (ImGui::BeginTable("##datasets", 6, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("porównaj", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Nazwa"); ImGui::TableSetupColumn("Typ", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Katalog"); ImGui::TableSetupColumn("ZPL");
			ImGui::TableHeadersRow();
			const auto &settings = m_Session->Settings();
			for (const auto &entry : ordered)
			{
				const auto &dataset = entry.get();
				const int group = groupFor(dataset);
				if (group != previousGroup)
				{
					previousGroup = group;
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(2);
					if (group == 2)
					{
						showOther = ImGui::CollapsingHeader("Inne");
						ImGui::SetItemTooltip("Zestawy nieprzypięte lub należące do innych przypadków.");
					}
					else ImGui::TextUnformatted(group == 0 ? "Przypięte" : group == 1 ? "Proponowane" : "Bulk (współdzielone zestawy)");
				}
				if (group == 2 && !showOther) continue;
				ImGui::PushID(dataset.directory.Utf8().c_str());
				ImGui::TableNextRow();
				// Loaded = the panel uses it (one per kind, a marker); current = the one row last opened, the only fill.
				const bool active = PhononPanelSession::SameFilePath(dataset.directory,
						dataset.kind == AnalysisDatasetKind::Dispersion ? settings.selectedDispersionDirectory :
						dataset.kind == AnalysisDatasetKind::BulkMode ? settings.selectedBulkModeDirectory :
					dataset.kind == AnalysisDatasetKind::Ccd ? settings.selectedCcdDirectory : settings.resultDirectory, m_Session->m_ProjectDirectory);
				if (PhononPanelSession::SameFilePath(dataset.directory, m_Session->CurrentDataset(), m_Session->m_ProjectDirectory))
				{
					const auto accent = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.35f);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, accent);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, accent);
				}
				ImGui::TableSetColumnIndex(0);
				bool compared = m_Session->IsCompared(dataset.directory);
				ImGui::BeginDisabled(dataset.kind == AnalysisDatasetKind::Dispersion || dataset.kind == AnalysisDatasetKind::BulkMode || dataset.schema.empty());
				if (ImGui::Checkbox("##compare", &compared)) { m_Session->SetCompared(dataset.directory, compared); persist(); }
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("%s", dataset.schema.empty() ? "Brak widma: wygenerowane punkty CCD lub niedostępny wynik. Przeskanuj zestawy po obliczeniach." :
					(dataset.kind == AnalysisDatasetKind::Dispersion || dataset.kind == AnalysisDatasetKind::BulkMode) ?
					"Porównaj widma fononowe lub PL 1D zestawów CCD." : "Dodaj / usuń zestaw w globalnym porównaniu.");
				ImGui::TableSetColumnIndex(1);
				// Not `selected`: the accent row background marks it over the full row height (checkbox
				// rows are taller than a Selectable), the frame-high Selectable only adds hover.
				ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
				ImGui::BeginDisabled(dataset.schema.empty());
				if (ImGui::Selectable(active ? "●###dataset" : "###dataset", false,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, ImGui::GetFrameHeight()))) selected = dataset;
				ImGui::PopStyleVar();
				ImGui::EndDisabled();
				if (dataset.schema.empty()) ImGui::SetItemTooltip("Brak podsumowania wyniku; punkty CCD trzeba najpierw obliczyć i przeanalizować.");
				if (active) ImGui::SetItemTooltip("Wczytany: panel używa tego zestawu (jeden na rodzaj: fonony, dyspersja, mod bulku, CCD).");
				ImGui::TableSetColumnIndex(2);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(dataset.label.c_str());
				const auto owner = m_Session->DatasetCaseId(dataset.directory);
				if (group == 2 && !owner.empty())
					for (const auto &item : m_Session->Cases())
						if (item.id == owner)
						{
							ImGui::SameLine();
							ImGui::TextDisabled("(%s)", item.name.c_str());
						}
				if (group == 0 || group == 1 || (group == 2 && !m_Session->ActiveCaseId().empty()))
				{
					ImGui::SameLine();
					if (ImGui::SmallButton(group == 0 ? "Odepnij" : "Przypnij"))
					{
						if (group == 0) detach = dataset.directory;
						else attach = dataset.directory;
					}
					ImGui::SetItemTooltip("%s", group == 0 ? "Usuń przypięcie; pliki zostają." :
						"Przypnij do aktywnego przypadku; usuwa przypięcie z innego przypadku.");
				}
				ImGui::TableSetColumnIndex(3);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(dataset.kind == AnalysisDatasetKind::Phonons ? "fonony" : dataset.kind == AnalysisDatasetKind::Dispersion ? "dyspersja" : dataset.kind == AnalysisDatasetKind::BulkMode ? "mod bulku" : "CCD");
				ImGui::TableSetColumnIndex(4);
				ImGui::AlignTextToFramePadding();
				const auto pathLabel = PhononPanelSession::DirectoryLabel(dataset.directory, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots);
				ImGui::TextUnformatted(pathLabel.c_str()); ImGui::SetItemTooltip("%s", dataset.directory.Utf8().c_str());
				ImGui::TableSetColumnIndex(5);
				std::string formula, error;
				if (dataset.kind == AnalysisDatasetKind::Phonons && !dataset.schema.empty())
				{
					if (active && m_Session->Result()) formula = m_Session->Result()->zplFormulaText;
					else if (const auto spectra = m_Session->Spectra(dataset.directory, error)) formula = spectra->get().zplFormulaText;
				}
				else if (dataset.kind == AnalysisDatasetKind::Ccd && !dataset.schema.empty())
				{
					if (active && m_Session->CcdResult()) formula = m_Session->CcdResult()->get().zplFormulaText;
					else if (const auto ccd = m_Session->Ccd(dataset.directory, error)) formula = ccd->get().zplFormulaText;
				}
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(formula.empty() ? "-" : formula.c_str());
				if (!error.empty()) ImGui::SetItemTooltip("%s", error.c_str());
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		// Loading adds/updates the row, so do it after releasing references into the list.
		if (attach) m_Session->AttachDataset(m_Session->ActiveCaseId(), *attach);
		if (detach) m_Session->DetachDataset(m_Session->ActiveCaseId(), *detach);
		if (selected)
		{
			const auto owner = m_Session->DatasetCaseId(selected->directory);
			if (!owner.empty()) (void)m_Session->ActivateCase(owner);
			else if (m_Session->Cases().empty() && selected->kind != AnalysisDatasetKind::Dispersion)
				(void)m_Session->AddCase(selected->label);
			if (selected->kind == AnalysisDatasetKind::Phonons) m_Session->loadResult(selected->directory);
			else if (selected->kind == AnalysisDatasetKind::Ccd) (void)m_Session->loadCcdResult(selected->directory);
			else if (selected->kind == AnalysisDatasetKind::BulkMode) (void)m_Session->LoadBulkMode(selected->directory);
			else (void)m_Session->loadDispersion(selected->directory);
			persist();
		}
		if (!m_Session->m_CcdAnalysisError.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", m_Session->m_CcdAnalysisError.c_str());
	}

	void PhononModesPanel::drawZplInputs()
	{
		auto &settings = m_Session->Settings();
		const auto &formula = settings.zplFormula;
		if (!m_ZplPresetFormula || *m_ZplPresetFormula != formula)
			m_ZplPreset = formula == "E_es - E_gs" ? 0 : formula == "2*E_es - E_T - E_gs" ? 1 : formula == "1.945" ? 2 : 3;
		constexpr std::array<std::string_view, 4> presets = {"ΔSCF: E_es − E_gs", "korekcja spinowa: 2E_es − E_T − E_gs", "eksperymentalne (liczba)", "własny wzór"};
		{
			PropertyGrid grid("##zplInputs", PropertyGridLabelWidth({"ZPL", "Wzór"}));
			if (grid)
			{
				grid.Row("ZPL");
				if (ImGui::BeginCombo("##zplPreset", presets[m_ZplPreset].data()))
				{
					for (int i = 0; i < 4; ++i)
						if (ImGui::Selectable(presets[i].data(), m_ZplPreset == i))
						{
							m_ZplPreset = i;
							if (i < 3) settings.zplFormula = i == 0 ? "E_es - E_gs" : i == 1 ? "2*E_es - E_T - E_gs" : "1.945";
							persist();
						}
					ImGui::EndCombo();
				}
				grid.Row("Wzór");
				std::vector<char> text(settings.zplFormula.begin(), settings.zplFormula.end());
				text.resize(text.size() + 1024, '\0');
				if (ImGui::InputText("##zplFormula", text.data(), text.size())) { settings.zplFormula = text.data(); m_ZplPreset = 3; }
				if (ImGui::IsItemDeactivatedAfterEdit()) persist();
				ImGui::SetItemTooltip("E_gs, E_es, E_T (tryplet), E_es@gs, E_gs@es: końcowe TOTEN [eV].\nOperatory: + - * / i nawiasy; liczba = ZPL eksperymentalne.\nPusty wzór: dotychczasowe krzywe ZPL.");
			}
		}
		m_ZplPresetFormula = settings.zplFormula;
		if (ImGui::CollapsingHeader("Dodatkowe energie"))
		{
			PropertyGrid grid("##extraEnergies", PropertyGridLabelWidth({"E_es@gs", "E_gs@es"}));
			if (grid)
			{
				grid.Row("E_es@gs"); if (DrawFolder("esAtGs", settings.excitedAtGroundDirectory, m_Session->m_Error)) persist();
				grid.Row("E_gs@es"); if (DrawFolder("gsAtEs", settings.groundAtExcitedDirectory, m_Session->m_Error)) persist();
			}
		}
	}
}
