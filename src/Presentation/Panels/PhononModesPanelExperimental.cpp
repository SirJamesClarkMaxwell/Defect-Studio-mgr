#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <imgui.h>
#include <implot.h>
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"

namespace DefectStudio
{
	void DrawPhononPlotSeries(PhononPanelSession &session, PlotData &plot)
	{
		for (auto &series : plot.series)
		{
			const bool experimental = series.id.starts_with("pl/exp/");
			auto initial = series.style;
			initial.color.reset(); // Keep an inherited case colour automatic in the persisted style.
			auto &style = session.Settings().seriesStyles.try_emplace(PhononPlotStyleKey(series), initial).first->second;
			if (experimental && !style.color)
				for (const auto &item : session.Cases())
					if (series.id.starts_with("pl/exp/" + item.id + "/"))
						for (const auto &computed : plot.series)
							if (computed.id.starts_with("PL/") && (session.DatasetCaseId(computed.source) == item.id ||
								PhononPanelSession::SameFilePath(computed.source, item.settings.resultDirectory, {})))
							{ series.style.color = computed.style.color; break; }
			const auto legendId = PhononPlotLegendId(series);
			if (!experimental) ResolvePlotItemColor(series, style, legendId);
			if (PlotSeries(series, style, legendId)) session.Changed();
			if (!experimental) ResolvePlotItemColor(series, style, legendId);
			const auto label = legendId.empty() ? series.label : series.label + "###" + legendId;
			if (experimental && ImPlot::IsLegendEntryHovered(label.c_str()))
				ImGui::SetTooltip("%s", PhononExperimentalSeriesNote(session, series).c_str());
		}
	}

	void DrawPhononExperimental(PhononPanelSession &session,
		std::unordered_map<std::string, std::pair<std::vector<char>, bool>> &edits)
	{
		const auto changed = [&] { session.InvalidatePlots(); session.Changed(); };
		const bool expanded = ImGui::CollapsingHeader("PL eksperymentalne");
		if (expanded)
		{
			if (ImGui::Button("Wczytaj widmo…"))
			{
				const auto picked = Platform::PickOpenFile(session.m_ProjectDirectory, "Widma PL (txt, dat, csv, tsv)", "txt,dat,csv,tsv");
				if (!picked) session.m_Error = picked.Error().userMessage;
				else if (picked->has_value())
				{
					session.m_ExperimentalImport = {};
					session.m_ExperimentalImport.file = **picked;
					session.m_ExperimentalImport.label = (**picked).filename().Utf8();
					session.m_ExperimentalImportCase = session.ActiveCaseId();
					auto &buffer = edits["experimentalLabel"].first;
					buffer.assign(session.m_ExperimentalImport.label.begin(), session.m_ExperimentalImport.label.end());
					buffer.resize(buffer.size() + 1024, '\0');
					ImGui::OpenPopup("Import PL eksperymentalnego");
				}
			}
			ImGui::SetItemTooltip("Wczytaj plik tekstowy w miejscu; oryginalny plik pozostaje źródłem widma. Dostępny też filtr wszystkich plików.");
			for (auto &item : session.m_Settings.cases)
				if (item.id == session.ActiveCaseId())
					for (std::size_t index = 0; index < item.experimentalSpectra.size();)
					{
						auto &source = item.experimentalSpectra[index];
						ImGui::PushID(static_cast<int>(index));
						if (ImGui::Checkbox("##visible", &source.visible)) changed();
						ImGui::SetItemTooltip("Uwzględnij widmo na wykresie PL, w porównaniu i eksporcie.");
						ImGui::SameLine(); ImGui::TextUnformatted(source.label.c_str());
						ImGui::SetItemTooltip("Etykieta widma eksperymentalnego w legendzie.");
						const auto path = PhononPanelSession::DirectoryLabel(source.file, session.m_ProjectDirectory, session.m_ProjectRoots);
						ImGui::TextDisabled("%s", path.c_str());
						ImGui::SetItemTooltip("%s", source.file.Utf8().c_str());
						const auto &data = session.Experimental(source);
						{
							PropertyGrid grid("##experimentalZpl", PropertyGridLabelWidth({"ZPL [eV]"}));
							if (grid)
							{
								grid.Row("ZPL [eV]");
								bool hasZpl = source.zplEv.has_value();
								if (ImGui::Checkbox("##hasZpl", &hasZpl)) { source.zplEv = hasZpl ? std::optional<double>(2.0) : std::nullopt; changed(); }
								ImGui::SetItemTooltip("Wyłączone = brak ZPL widma; wyrównanie używa ZPL bieżącej krzywej obliczonej przypadku.");
								ImGui::SameLine();
								if (source.zplEv)
								{
									float value = static_cast<float>(*source.zplEv);
									ImGui::SetNextItemWidth(100.0f);
									if (ImGui::DragFloat("##zpl", &value, 0.0001f, 0.0f, 0.0f, "%.6f") && std::isfinite(value)) { source.zplEv = value; changed(); }
									ImGui::SetItemTooltip("ZPL tego widma w eV; przeciągnij lub wpisz wartość.");
								}
								else ImGui::TextDisabled("—");
								const auto selected = session.Result() ? SelectPhononPlCurve(*session.Result(), session.Settings().plZpl) : PhononPlSelection{};
								std::string blocker = data.loading ? "Wczytywanie widma…" : data.error;
								if (blocker.empty() && !selected.zplEv) blocker = "Brak ZPL bieżącej krzywej obliczonej.";
								if (blocker.empty() && !data.spectrum) blocker = "Brak danych widma.";
								if (blocker.empty())
								{
									const auto &energies = data.spectrum->energyEv;
									const auto first = std::lower_bound(energies.begin(), energies.end(), *selected.zplEv - 0.15);
									if (first == energies.end() || *first > *selected.zplEv + 0.15) blocker = "Brak próbek w oknie ZPL ± 0.15 eV.";
								}
								ImGui::SameLine(); ImGui::BeginDisabled(!blocker.empty());
								if (ImGui::SmallButton("z maksimum"))
								{
									const double peak = SpectrumPeakEnergyEv(*data.spectrum, *selected.zplEv - 0.15, *selected.zplEv + 0.15);
									if (std::isfinite(peak)) { source.zplEv = peak; changed(); }
								}
								ImGui::EndDisabled();
								const auto tooltip = !blocker.empty() ? blocker : std::format("Maksimum w oknie [{:.6g}, {:.6g}] eV (ZPL krzywej obliczonej ± 0.15 eV).", *selected.zplEv - 0.15, *selected.zplEv + 0.15);
								ImGui::SetItemTooltip("%s", tooltip.c_str());
							}
						}
						if (data.loading) ImGui::TextDisabled("Wczytywanie…");
						else if (!data.error.empty()) ImGui::TextWrapped("%s", data.error.c_str());
						const bool remove = ImGui::SmallButton("x");
						ImGui::SetItemTooltip("Usuń widmo z przypadku; plik źródłowy pozostaje na dysku.");
						if (remove)
						{
							item.experimentalSpectra.erase(item.experimentalSpectra.begin() + index);
							const auto prefix = "pl/exp/" + item.id + "/";
							for (std::size_t i = index; i < item.experimentalSpectra.size(); ++i)
							{
								const auto next = session.Settings().seriesStyles.find(prefix + std::to_string(i + 1));
								if (next == session.Settings().seriesStyles.end()) session.Settings().seriesStyles.erase(prefix + std::to_string(i));
								else session.Settings().seriesStyles[prefix + std::to_string(i)] = next->second;
							}
							session.Settings().seriesStyles.erase(prefix + std::to_string(item.experimentalSpectra.size()));
							changed();
						}
						else ++index;
						ImGui::PopID();
					}
		}
		if (!ImGui::BeginPopup("Import PL eksperymentalnego")) return;
		auto &source = session.m_ExperimentalImport;
		if (source.file.Empty()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
		ImGui::TextUnformatted(PhononPanelSession::DirectoryLabel(source.file, session.m_ProjectDirectory, session.m_ProjectRoots).c_str());
		ImGui::SetItemTooltip("%s", source.file.Utf8().c_str());
		const auto &data = session.Experimental(source);
		ImGui::TextUnformatted("Pierwsze 8 linii");
		if (ImGui::BeginChild("##preview", ImVec2(520.0f, 130.0f), 0, ImGuiWindowFlags_HorizontalScrollbar)) ImGui::TextUnformatted(data.preview.c_str());
		ImGui::EndChild();
		const auto delimiter = data.table.delimiter == '\t' ? "tabulator" : data.table.delimiter == ' ' ? "białe znaki" : data.table.delimiter == ';' ? ";" : ",";
		ImGui::Text("Separator: %s; kolumny: %zu; pominięte linie: %zu", delimiter, data.table.columns.size(), data.table.skippedLines);
		ImGui::SetItemTooltip("Nagłówki i komentarze są liczone jako pominięte linie; puste linie nie są liczone.");
		{
			PropertyGrid grid("##experimentalImport", PropertyGridLabelWidth({"Kolumna x", "Kolumna y", "Jednostka", "Etykieta"}));
			if (grid)
			{
				for (const auto &[label, column] : {std::pair{"Kolumna x", std::ref(source.xColumn)}, std::pair{"Kolumna y", std::ref(source.yColumn)}})
				{
					grid.Row(label); ImGui::PushID(label);
					const auto name = [&](int i) { return std::format("{}: {}", i + 1, i >= 0 && static_cast<std::size_t>(i) < data.table.names.size() ? data.table.names[i] : ""); };
					if (ImGui::BeginCombo("##column", name(column.get()).c_str()))
					{
						for (std::size_t i = 0; i < data.table.columns.size(); ++i)
							if (ImGui::Selectable(name(static_cast<int>(i)).c_str(), column.get() == static_cast<int>(i))) column.get() = static_cast<int>(i);
						ImGui::EndCombo();
					}
					ImGui::SetItemTooltip("%s", std::string_view(label) == "Kolumna x" ? "Energia fotonu." : "Intensywność mierzona.");
					ImGui::PopID();
				}
				grid.Row("Jednostka");
				if (ImGui::BeginCombo("##unit", "eV")) { if (ImGui::Selectable("eV", true)) source.unit = "eV"; ImGui::EndCombo(); }
				ImGui::SetItemTooltip("Inne jednostki - wkrótce.");
				grid.Row("Etykieta");
				auto &buffer = edits["experimentalLabel"].first;
				if (buffer.empty()) buffer.resize(1024, '\0');
				if (ImGui::InputText("##label", buffer.data(), buffer.size())) source.label = buffer.data();
				ImGui::SetItemTooltip("Nazwa serii w legendzie i eksporcie; domyślnie nazwa pliku.");
			}
		}
		const auto &current = session.Experimental(source);
		const auto reason = session.ActiveCaseId() != session.m_ExperimentalImportCase ? "Zmieniono aktywny przypadek; otwórz import ponownie." :
			current.loading ? "Wczytywanie…" : !current.error.empty() ? current.error : !current.spectrum ? "Brak poprawnego widma." : "";
		if (!reason.empty()) ImGui::TextWrapped("%s", reason.c_str());
		ImGui::BeginDisabled(!reason.empty());
		if (ImGui::Button("Dodaj") && session.AddExperimentalSpectrum(source)) ImGui::CloseCurrentPopup();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", reason.empty() ? "Dodaj widmo do aktywnego przypadku." : reason.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Anuluj")) ImGui::CloseCurrentPopup();
		ImGui::SetItemTooltip("Zamknij bez dodawania widma.");
		ImGui::EndPopup();
	}
}
