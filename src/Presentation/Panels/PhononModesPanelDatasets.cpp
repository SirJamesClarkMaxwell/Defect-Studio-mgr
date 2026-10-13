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
	void PhononModesPanel::SetProjectRoots(std::vector<ProjectRootEntry> roots)
	{
		m_ProjectRoots = std::move(roots);
		scanDatasets();
	}

	void PhononModesPanel::scanDatasets()
	{
		m_DefaultDatasetDirectories.clear();
		std::vector<Path> roots;
		if (!m_ProjectDirectory.Empty()) roots.push_back(m_ProjectDirectory);
		for (const auto &root : m_ProjectRoots) roots.push_back(root.path);
		m_Session->StartDatasetScan(m_JobSystem, std::move(roots));
	}

	Path PhononModesPanel::defaultDatasetPath(const std::vector<Path> &inputs, const std::string &kind) const
	{
		std::string key = m_ProjectDirectory.Utf8() + "\n" + kind;
		for (const auto &input : inputs) key += "\n" + input.Utf8();
		const auto found = m_DefaultDatasetDirectories.find(key);
		if (found != m_DefaultDatasetDirectories.end()) return found->second;
		return m_DefaultDatasetDirectories.emplace(key,
			DefaultDatasetDirectory(inputs, kind, m_ProjectDirectory, IsDirectoryWritable)).first->second;
	}

	Path PhononModesPanel::computationOutputPath() const
	{
		const auto &s = m_Session->Settings();
		if (!s.outputDirectory.Empty()) return s.outputDirectory;
		std::vector<Path> inputs{s.forceConstantDirectory, s.groundStateDirectory, s.excitedStateDirectory,
			s.tripletDirectory, s.groundAtExcitedDirectory, s.excitedAtGroundDirectory};
		if (!s.embeddingMult.empty()) inputs.push_back(s.bulkForceConstantDirectory);
		return defaultDatasetPath(inputs, s.embeddingMult.empty() ? "phonons" : "phonons_emb_" + s.embeddingMult);
	}

	void PhononModesPanel::drawDatasets()
	{
		if (!ImGui::CollapsingHeader("Zestawy", ImGuiTreeNodeFlags_DefaultOpen)) return;
		if (ImGui::Button("Skanuj ponownie")) scanDatasets();
		if (m_Session->IsScanningDatasets()) { ImGui::SameLine(); ImGui::TextDisabled("Skanowanie..."); }
		else if (!m_Session->DatasetScanMessage().empty())
		{
			ImGui::SameLine(); ImGui::TextUnformatted(m_Session->DatasetScanMessage().c_str());
		}
		std::optional<AnalysisDatasetRef> selected;
		if (ImGui::BeginTable("##datasets", 6, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("porównaj", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Nazwa"); ImGui::TableSetupColumn("Typ", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Katalog"); ImGui::TableSetupColumn("ZPL");
			ImGui::TableHeadersRow();
			const auto &settings = m_Session->Settings();
			for (const auto &dataset : m_Session->Datasets())
			{
				ImGui::PushID(dataset.directory.Utf8().c_str());
				ImGui::TableNextRow();
				// Loaded = the panel uses it (one per kind, a marker); current = the one row last opened, the only fill.
				const bool active = PhononPanelSession::SameFilePath(dataset.directory,
					dataset.kind == AnalysisDatasetKind::Dispersion ? settings.selectedDispersionDirectory :
					dataset.kind == AnalysisDatasetKind::Ccd ? settings.selectedCcdDirectory : settings.resultDirectory, m_ProjectDirectory);
				if (PhononPanelSession::SameFilePath(dataset.directory, m_Session->CurrentDataset(), m_ProjectDirectory))
				{
					const auto accent = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.35f);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, accent);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, accent);
				}
				ImGui::TableSetColumnIndex(0);
				bool compared = m_Session->IsCompared(dataset.directory);
				ImGui::BeginDisabled(dataset.kind == AnalysisDatasetKind::Dispersion);
				if (ImGui::Checkbox("##compare", &compared)) { m_Session->SetCompared(dataset.directory, compared); m_ResetPlots = true; persist(); }
				ImGui::EndDisabled();
				if (dataset.kind == AnalysisDatasetKind::Dispersion) ImGui::SetItemTooltip("Porównaj widma fononowe lub PL 1D zestawów CCD.");
				ImGui::TableSetColumnIndex(1);
				// Not `selected`: the accent row background marks it over the full row height (checkbox
				// rows are taller than a Selectable), the frame-high Selectable only adds hover.
				ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
				if (ImGui::Selectable(active ? "●###dataset" : "###dataset", false,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, ImGui::GetFrameHeight()))) selected = dataset;
				ImGui::PopStyleVar();
				if (active) ImGui::SetItemTooltip("Wczytany: panel używa tego zestawu (jeden na rodzaj: fonony, dyspersja, CCD).");
				ImGui::TableSetColumnIndex(2);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(dataset.label.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(dataset.kind == AnalysisDatasetKind::Phonons ? "fonony" : dataset.kind == AnalysisDatasetKind::Dispersion ? "dyspersja" : "CCD");
				ImGui::TableSetColumnIndex(4);
				ImGui::AlignTextToFramePadding();
				const auto pathLabel = PhononPanelSession::DirectoryLabel(dataset.directory, m_ProjectDirectory, m_ProjectRoots);
				ImGui::TextUnformatted(pathLabel.c_str()); ImGui::SetItemTooltip("%s", dataset.directory.Utf8().c_str());
				ImGui::TableSetColumnIndex(5);
				std::string formula, error;
				if (dataset.kind == AnalysisDatasetKind::Phonons)
				{
					if (active && m_Session->Result()) formula = m_Session->Result()->zplFormulaText;
					else if (const auto spectra = m_Session->Spectra(dataset.directory, error)) formula = spectra->get().zplFormulaText;
				}
				else if (dataset.kind == AnalysisDatasetKind::Ccd)
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
		if (selected)
		{
			if (selected->kind == AnalysisDatasetKind::Phonons) loadResult(selected->directory);
			else if (selected->kind == AnalysisDatasetKind::Ccd) (void)loadCcdResult(selected->directory);
			else (void)loadDispersion(selected->directory);
			persist();
		}
		if (!m_CcdAnalysisError.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", m_CcdAnalysisError.c_str());
	}

	void PhononModesPanel::drawZplInputs()
	{
		auto &settings = m_Session->Settings();
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
		if (ImGui::CollapsingHeader("Dodatkowe energie"))
		{
			PropertyGrid grid("##extraEnergies", PropertyGridLabelWidth({"E_es@gs", "E_gs@es"}));
			if (grid)
			{
				grid.Row("E_es@gs"); if (DrawFolder("esAtGs", settings.excitedAtGroundDirectory, m_Error)) persist();
				grid.Row("E_gs@es"); if (DrawFolder("gsAtEs", settings.groundAtExcitedDirectory, m_Error)) persist();
			}
		}
	}

	void PhononModesPanel::drawDatasetSpectra(bool pl, double minimum, double maximum)
	{
		auto &settings = m_Session->Settings();
		const auto draw = [&](const PhononAnalysisResult &result, const Path &directory, const std::string &label) {
			const auto key = (pl ? "PL/" : "S/") + PhononPanelSession::DirectoryLabel(directory, m_ProjectDirectory, m_ProjectRoots);
			const auto legend = label + "###" + directory.Utf8();
			auto &style = settings.seriesStyles[key];
			const auto &x = pl ? result.plPhotonEnergyEv : result.spectralEnergyMeV;
			const auto &y = pl ? PhononPanelSession::PlCurve(result, PhononPanelSession::PlCurveChoice(result, settings.plZpl)) : result.spectralDensityPerMeV;
			if (x.empty() || y.size() != x.size()) return;
			// Only the visible range, so auto-fit (double click) does not zoom out to the grid end.
			const auto [first, last] = PhononPanelSession::GridIndexRange(x, minimum, maximum);
			PlotStyledLine(legend.c_str(), std::span(x).subspan(first, last - first), std::span(y).subspan(first, last - first), style);
			if (SeriesLegendPopup(legend.c_str(), style)) persist();
		};
		const auto &active = *m_Session->Result();
		draw(active, settings.resultDirectory, active.label.empty() ? settings.resultDirectory.filename().Utf8() : active.label);
		for (const auto &dataset : m_Session->Datasets())
			if (dataset.kind == AnalysisDatasetKind::Phonons && m_Session->IsCompared(dataset.directory) &&
				!PhononPanelSession::SameFilePath(dataset.directory, settings.resultDirectory, m_ProjectDirectory))
			{
				std::string error;
				if (const auto spectra = m_Session->Spectra(dataset.directory, error)) draw(spectra->get(), dataset.directory, dataset.label);
			}
	}
}
