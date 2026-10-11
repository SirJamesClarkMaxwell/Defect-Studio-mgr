#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>

#include <imgui.h>
#include <implot.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr ImVec4 kWarningColor(0.95f, 0.75f, 0.30f, 1.0f);
		constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

		// A density for a log axis: NaN (a gap in the line) when unconverged or zero.
		double LogPlottable(const DefectConcentrationSolution &solution, double value)
		{
			return solution.converged && value > 0.0 ? value : kNan;
		}
	} // namespace

	void DefectEnergeticsPanel::drawConcentrations()
	{
		const float width = std::min(0.5f * ImGui::GetContentRegionAvail().x,
			std::clamp(0.35f * ImGui::GetContentRegionAvail().x, 180.0f, 380.0f));
		if (ImGui::BeginChild("##ConcentrationInputs", ImVec2(width, 0.0f), ImGuiChildFlags_Borders,
			ImGuiWindowFlags_HorizontalScrollbar))
			drawConcentrationInputs();
		ImGui::EndChild();
		ImGui::SameLine();
		if (ImGui::BeginChild("##ConcentrationResults", ImVec2(0.0f, 0.0f)))
		{
			if (ImGui::BeginTabBar("##ConcentrationTabs"))
			{
				const char *labels[]{"Temperatura", "Linia μ", "Mapa"};
				for (int mode = 0; mode < 3; ++mode)
					if (ImGui::BeginTabItem(labels[mode]))
					{
						drawConcentrationResults(mode);
						ImGui::EndTabItem();
					}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();
	}

	// Inputs (saved in energetics.yaml), the job, and the linked plots: c(T) + E_F(T) at one lambda;
	// along the Delta mu line also c(lambda) + E_F(lambda) at one T and a T x lambda map.
	void DefectEnergeticsPanel::drawConcentrationInputs()
	{
		drawMuCombo();
		ConcentrationSettings &settings = m_Session->m_File->concentrations;
		bool edited = false;
		const float field = 7.0f * ImGui::GetFontSize();

		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("m_e* DOS", &settings.electronMass, 0.0, 0.0, "%.3f");
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("m_h* DOS [m_0]", &settings.holeMass, 0.0, 0.0, "%.3f");
		ImGui::SetItemTooltip("Masy efektywne gęstości stanów (wszystkie doliny / pasma, bez spinu).\n"
			"Domyślnie 0.46 / 1.25: Alasio et al., PR Applied 21, 054043 (2024) - sprawdź normalizację dolin.\n"
			"W diamencie nośniki liczą się dopiero blisko krawędzi pasm - E_F zwykle ustalają defekty.");
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("T od", &settings.temperatureMin, 0.0, 0.0, "%.0f K");
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("do", &settings.temperatureMax, 0.0, 0.0, "%.0f K");
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputInt("punktów T", &settings.temperatureSteps);
		settings.temperatureMin = std::clamp(settings.temperatureMin, 1.0, 3000.0);
		settings.temperatureMax = std::clamp(settings.temperatureMax, settings.temperatureMin, 3000.0);
		settings.temperatureSteps = std::clamp(settings.temperatureSteps, 1, 1000);
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("N_fixed [cm⁻³]", &settings.fixedCharge, 0.0, 0.0, "%.3e");
		ImGui::SetItemTooltip("Stały zjonizowany ładunek domieszek spoza listy: donory +, akceptory -.");

		// Delta mu line between two sets.
		const auto setCombo = [&](const char *label, std::string &name) {
			ImGui::SetNextItemWidth(2.0f * field);
			if (ImGui::BeginCombo(label, name.empty() ? "brak" : name.c_str()))
			{
				if (ImGui::Selectable("brak", name.empty()))
				{
					name.clear();
					edited = true;
				}
				for (const auto &set : m_Session->m_Model.muSets)
					if (ImGui::Selectable(set.name.c_str(), set.name == name))
					{
						name = set.name;
						edited = true;
					}
				ImGui::EndCombo();
			}
		};
		setCombo("linia Δμ od", settings.lineFrom);
		setCombo("do##lineTo", settings.lineTo);
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputInt("punktów λ", &settings.linePoints);
		settings.linePoints = std::clamp(settings.linePoints, 2, 200);
		ImGui::SetItemTooltip("Bez linii: tylko zaznaczony zestaw μ. Z linią: Δμ(λ) = (1-λ)·od + λ·do, mapa T × λ.");

		// Per defect: site density and degeneracy.
		std::vector<std::string> defects;
		for (const auto &row : m_Session->m_Model.rows)
			if (row.included && (defects.empty() || defects.back() != row.defect))
				defects.push_back(row.defect);
		int bulkAtoms = 0;
		if (m_Session->m_Model.bulk)
			for (const int ions : m_Session->m_File->runs[*m_Session->m_Model.bulk].ions)
				bulkAtoms += ions;
		const double defaultSites = m_Session->m_BulkVolume ? bulkAtoms / *m_Session->m_BulkVolume * 1e24 : 0.0;
		if (ImGui::BeginTable("##ConcentrationDefects", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Defekt");
			ImGui::TableSetupColumn("N_sites [cm⁻³]");
			ImGui::TableSetupColumn("g (orientacje × spin)");
			ImGui::TableHeadersRow();
			for (const auto &defect : defects)
			{
				ImGui::PushID(defect.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(defect.c_str());
				ImGui::TableNextColumn();
				const auto sites = settings.sites.find(defect);
				double value = sites != settings.sites.end() ? sites->second : defaultSites;
				ImGui::SetNextItemWidth(field);
				if (ImGui::InputDouble("##sites", &value, 0.0, 0.0, "%.4e"))
				{
					settings.sites[defect] = value;
					edited = true;
				}
				ImGui::SetItemTooltip("Domyślnie atomy bulku / objętość komórki (%.4e cm⁻³). Kompleksy i międzywęzłowe: wpisz sam.",
					defaultSites);
				ImGui::TableNextColumn();
				const auto degeneracy = settings.degeneracy.find(defect);
				double g = degeneracy != settings.degeneracy.end() ? degeneracy->second : 1.0;
				ImGui::SetNextItemWidth(field);
				if (ImGui::InputDouble("##g", &g, 0.0, 0.0, "%.2f"))
				{
					settings.degeneracy[defect] = g;
					edited = true;
				}
				ImGui::SetItemTooltip("Jedna wartość dla wszystkich stanów ładunkowych defektu (ponytail: bez g na stan).");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (edited)
			m_Session->Changed();

		if (m_Session->m_ConcentrationJob)
		{
			const auto jobSystem = m_Session->m_JobSystem.lock();
			const auto snapshot = jobSystem ? jobSystem->GetJob(m_Session->m_ConcentrationJobId) : std::nullopt;
			ImGui::ProgressBar(snapshot && snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f,
				ImVec2(-ImGui::CalcTextSize("Anuluj").x - 3.0f * ImGui::GetStyle().FramePadding.x, 0.0f));
			ImGui::SameLine();
			if (ImGui::Button("Anuluj##concentrations") && jobSystem)
				(void)jobSystem->RequestCancel(m_Session->m_ConcentrationJobId);
		}
		else
		{
			if (ImGui::Button("Policz koncentracje"))
				m_Session->StartConcentrations();
			ImGui::BeginDisabled(m_Session->m_Concentrations.solutions.empty() || !m_Session->ConcentrationsCurrent());
			if (ImGui::Button("Eksport koncentracji TSV..."))
				if (const auto folder = Platform::PickFolder(); folder && folder->has_value())
				{
					std::string error;
					m_Session->m_Message = TextFileIO::Save(**folder / Path::FromUtf8("concentrations.tsv"), m_Session->concentrationsTsv(), error)
						? "Zapisano concentrations.tsv w " + (*folder)->Utf8() + "." : "Eksport: " + error;
				}
			ImGui::EndDisabled();
		}
		if (!m_Session->m_Concentrations.error.empty())
			ImGui::TextColored(kWarningColor, "%s", m_Session->m_Concentrations.error.c_str());
		if (!m_Session->m_Message.empty())
			ImGui::TextWrapped("%s", m_Session->m_Message.c_str());

	}

	void DefectEnergeticsPanel::drawConcentrationResults(int mode)
	{
		if (!m_Session->ConcentrationsCurrent() && !m_Session->m_Concentrations.solutions.empty())
			ImGui::TextColored(kWarningColor, "Wyniki nieaktualne — policz ponownie po zmianie wejść.");
		const EnergeticsConcentrations &result = m_Session->m_Concentrations;
		const std::size_t nT = result.temperatures.size();
		const std::size_t nL = result.solutions.size() / std::max<std::size_t>(1, nT);
		if (nL == 0 || nT == 0)
		{
			ImGui::TextDisabled("Policz koncentracje, aby zobaczyć wyniki.");
			return;
		}
		std::size_t failed = 0;
		for (const auto &solution : result.solutions)
			failed += solution.converged ? 0 : 1;
		if (failed)
			ImGui::TextColored(kWarningColor, "%zu punktów bez rozwiązania (przerwy na wykresach).", failed);
		const double gap = result.gap;
		const double margin = std::max(0.25, 0.08 * gap);
		const auto solution = [&](std::size_t l, std::size_t t) -> const DefectConcentrationSolution & { return result.solutions[l * nT + t]; };

		m_ConcentrationLambda = std::clamp(m_ConcentrationLambda, 0, static_cast<int>(nL) - 1);
		m_ConcentrationTemperature = std::clamp(m_ConcentrationTemperature, 0, static_cast<int>(nT) - 1);
		// c(T) and E_F(T) at one lambda.
		if (mode == 0 && nL > 1)
		{
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::SliderInt("##lambda", &m_ConcentrationLambda, 0, static_cast<int>(nL) - 1,
				result.muLabels[static_cast<std::size_t>(m_ConcentrationLambda)].c_str());
		}
		const std::size_t l = static_cast<std::size_t>(std::clamp(m_ConcentrationLambda, 0, static_cast<int>(nL) - 1));
		const auto plotSeries = [&](const char *name, std::span<const double> xs, auto value, std::size_t count, const ImPlotSpec &spec) {
			std::vector<double> ys(count);
			for (std::size_t i = 0; i < count; ++i)
				ys[i] = value(i);
			ImPlot::PlotLine(name, xs.data(), ys.data(), static_cast<int>(count), spec);
		};
		ImPlotSpec dashed;
		dashed.LineColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
		if (mode == 0)
		{
			m_ConcentrationHeight = std::clamp(m_ConcentrationHeight, 1.0f,
				std::max(1.0f, 0.8f * ImGui::GetContentRegionAvail().y));
			const float height = m_ConcentrationHeight;
			if (ImPlot::BeginPlot("##ConcentrationT", ImVec2(-1.0f, height)))
			{
				ImPlot::SetupAxes("T [K]", "c [cm⁻³]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
				ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
				ImPlot::SetupLegend(ImPlotLocation_SouthEast);
				for (std::size_t d = 0; d < result.defects.size(); ++d)
					plotSeries(result.defects[d].c_str(), result.temperatures,
						[&](std::size_t t) { const auto &s = solution(l, t); return LogPlottable(s, d < s.defectTotalsCm3.size() ? s.defectTotalsCm3[d] : 0.0); },
						nT, ImPlotSpec());
				plotSeries("n", result.temperatures, [&](std::size_t t) { return LogPlottable(solution(l, t), solution(l, t).electronsCm3); }, nT, dashed);
				plotSeries("p", result.temperatures, [&](std::size_t t) { return LogPlottable(solution(l, t), solution(l, t).holesCm3); }, nT, dashed);
				ImPlot::EndPlot();
			}
			PlotHeightHandle("##concentrationHeight", m_ConcentrationHeight);
			if (ImPlot::BeginPlot("##FermiT", ImVec2(-1.0f, std::max(1.0f, ImGui::GetContentRegionAvail().y))))
			{
				ImPlot::SetupAxes("T [K]", "E_F - E_VBM [eV]", ImPlotAxisFlags_AutoFit);
				ImPlot::SetupAxisLimits(ImAxis_Y1, -margin, gap + margin, ImPlotCond_Always);
				DrawBandEdges(0.0, gap, false);
				plotSeries("E_F", result.temperatures, [&](std::size_t t) { return solution(l, t).converged ? solution(l, t).fermiLevelEv : kNan; }, nT, ImPlotSpec());
				ImPlot::EndPlot();
			}

			return;
		}
		if (nL < 2)
		{
			ImGui::TextDisabled("Wybierz linię μ w formularzu i policz koncentracje.");
			return;
		}
		// Along the line at one T, and the T x lambda map.
		if (mode == 1)
		{
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::SliderInt("##temperature", &m_ConcentrationTemperature, 0, static_cast<int>(nT) - 1,
				std::format("T = {:.0f} K", result.temperatures[static_cast<std::size_t>(m_ConcentrationTemperature)]).c_str());
		}
		const std::size_t t = static_cast<std::size_t>(std::clamp(m_ConcentrationTemperature, 0, static_cast<int>(nT) - 1));
		if (mode == 1)
		{
			m_ConcentrationHeight = std::clamp(m_ConcentrationHeight, 1.0f,
				std::max(1.0f, 0.8f * ImGui::GetContentRegionAvail().y));
			const float height = m_ConcentrationHeight;
			if (ImPlot::BeginPlot("##ConcentrationLambda", ImVec2(-1.0f, height)))
			{
				ImPlot::SetupAxes("λ", "c [cm⁻³]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
				ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
				ImPlot::SetupLegend(ImPlotLocation_SouthEast);
				for (std::size_t d = 0; d < result.defects.size(); ++d)
					plotSeries(result.defects[d].c_str(), result.lambdas,
						[&](std::size_t k) { const auto &s = solution(k, t); return LogPlottable(s, d < s.defectTotalsCm3.size() ? s.defectTotalsCm3[d] : 0.0); },
						nL, ImPlotSpec());
				plotSeries("n", result.lambdas, [&](std::size_t k) { return LogPlottable(solution(k, t), solution(k, t).electronsCm3); }, nL, dashed);
				plotSeries("p", result.lambdas, [&](std::size_t k) { return LogPlottable(solution(k, t), solution(k, t).holesCm3); }, nL, dashed);
				ImPlot::EndPlot();
			}

			PlotHeightHandle("##lambdaHeight", m_ConcentrationHeight);
			if (ImPlot::BeginPlot("##FermiLambda", ImVec2(-1.0f, std::max(1.0f, ImGui::GetContentRegionAvail().y))))
			{
				ImPlot::SetupAxes("λ", "E_F - E_VBM [eV]", ImPlotAxisFlags_AutoFit);
				ImPlot::SetupAxisLimits(ImAxis_Y1, -margin, gap + margin, ImPlotCond_Always);
				DrawBandEdges(0.0, gap, false);
				plotSeries("E_F", result.lambdas, [&](std::size_t k) { return solution(k, t).converged ? solution(k, t).fermiLevelEv : kNan; }, nL, ImPlotSpec());
				ImPlot::EndPlot();
			}
			return;
		}
		const float field = 7.0f * ImGui::GetFontSize();
		std::vector<std::string> mapNames{"E_F - E_VBM [eV]"};
		for (const auto &defect : result.defects)
			mapNames.push_back("log10 c(" + defect + ")");
		m_ConcentrationMap = std::clamp(m_ConcentrationMap, 0, static_cast<int>(mapNames.size()) - 1);
		ImGui::SetNextItemWidth(2.0f * field);
		if (ImGui::BeginCombo("Mapa T × λ", mapNames[static_cast<std::size_t>(m_ConcentrationMap)].c_str()))
		{
			for (int k = 0; k < static_cast<int>(mapNames.size()); ++k)
				if (ImGui::Selectable(mapNames[static_cast<std::size_t>(k)].c_str(), k == m_ConcentrationMap))
					m_ConcentrationMap = k;
			ImGui::EndCombo();
		}
		std::vector<double> values(nL * nT, kNan); // row 0 = top = lambda 1
		double low = std::numeric_limits<double>::max(), high = std::numeric_limits<double>::lowest();
		for (std::size_t k = 0; k < nL; ++k)
			for (std::size_t c = 0; c < nT; ++c)
			{
				const auto &s = solution(k, c);
				const std::size_t d = static_cast<std::size_t>(m_ConcentrationMap - 1);
				const double v = !s.converged ? kNan : m_ConcentrationMap == 0 ? s.fermiLevelEv
					: d < s.defectTotalsCm3.size() && s.defectTotalsCm3[d] > 0.0 ? std::log10(s.defectTotalsCm3[d]) : kNan;
				values[(nL - 1 - k) * nT + c] = v;
				if (std::isfinite(v))
				{
					low = std::min(low, v);
					high = std::max(high, v);
				}
			}
		if (low > high)
		{
			ImGui::TextDisabled("Brak zbieżnych punktów mapy.");
			return;
		}
		if (high - low < 1e-12)
			high = low + 1.0;
		const float height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
		const float scaleWidth = 6.0f * ImGui::GetFontSize();
		if (ImPlot::BeginPlot("##ConcentrationMap", ImVec2(std::max(1.0f,
			ImGui::GetContentRegionAvail().x - scaleWidth - ImGui::GetStyle().ItemSpacing.x), height)))
		{
			ImPlot::SetupAxes("T [K]", "λ", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			ImPlot::PlotHeatmap("##map", values.data(), static_cast<int>(nL), static_cast<int>(nT), low, high, nullptr,
				ImPlotPoint(result.temperatures.front(), 0.0), ImPlotPoint(result.temperatures.back(), 1.0));
			ImPlot::EndPlot();
		}
		ImGui::SameLine();
		ImPlot::ColormapScale("##mapScale", low, high, ImVec2(scaleWidth, height), "%.2f");
	}

} // namespace DefectStudio
