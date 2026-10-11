#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculationSummaryPanel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

#include <imgui.h>
#include <implot.h>

namespace DefectStudio
{
	void CalculationSummaryPanel::renderBoundaries()
	{
		double offset = 0.0;
		const auto limits = ImPlot::GetPlotLimits();
		for (const auto &run : m_Chain)
		{
			if (!run.data || !run.data->summary.energyTrend || run.data->summary.energyTrend->empty())
				continue;
			const double boundary = offset + 0.5;
			const std::string label = run.directory.filename().Utf8() +
				(run.outcarName == "OUTCAR" ? "" : " / " + run.outcarName);
			ImPlotSpec spec;
			spec.LineColor = ImVec4(0.6f, 0.6f, 0.6f, 0.7f);
			spec.Flags = ImPlotItemFlags_NoLegend;
			ImPlot::PlotInfLines(("##boundary" + std::to_string(offset)).c_str(), &boundary, 1, spec);
			ImPlotSpec textSpec;
			textSpec.Flags = ImPlotItemFlags_NoFit;
			ImPlot::PlotText(label.c_str(), boundary, limits.Y.Max, ImVec2(20.0f, 12.0f), textSpec);
			offset += static_cast<double>(run.data->summary.energyTrend->size());
		}
	}

	void CalculationSummaryPanel::renderPlots()
	{
		const double missing = std::numeric_limits<double>::quiet_NaN();
		std::vector<double> steps, energies, forces, withoutEntropy, sigmaZero;
		std::set<int> kpoints;
		bool hasFermi = true;
		bool hasKs = false;
		for (const auto &run : m_Chain)
		{
			if (!run.data || !run.data->summary.energyTrend)
				continue;
			const auto &summary = run.data->summary;
			for (std::size_t i = 0; i < summary.energyTrend->size(); ++i)
			{
				steps.push_back(static_cast<double>(steps.size() + 1));
				energies.push_back((*summary.energyTrend)[i]);
				forces.push_back(i < summary.forceTrend.size() ? summary.forceTrend[i] : missing);
				withoutEntropy.push_back(i < summary.energyWithoutEntropy.size() ? summary.energyWithoutEntropy[i] : missing);
				sigmaZero.push_back(i < summary.energySigmaZero.size() ? summary.energySigmaZero[i] : missing);
			}
			for (const auto &step : summary.ksSteps)
			{
				hasKs = true;
				hasFermi = hasFermi && step.fermi.has_value();
				for (const auto &block : step.blocks)
					kpoints.insert(block.kpoint);
			}
		}
		if (steps.empty())
		{
			ImGui::TextDisabled("No complete ionic steps available. Load an OUTCAR or vasprun.xml.");
			return;
		}
		const auto hasValues = [](const std::vector<double> &values)
		{
			return std::ranges::any_of(values, [](double value) { return std::isfinite(value); });
		};
		ImGui::Spacing();
		if (ImPlot::BeginPlot("Convergence", ImVec2(-1.0f, 240.0f)))
		{
			ImPlot::SetupAxes("Global ionic step", "Energy (eV)");
			if (hasValues(forces))
				ImPlot::SetupAxis(ImAxis_Y2, "Max |F| (eV/A)", ImPlotAxisFlags_Opposite);
			ImPlotSpec energySpec;
			energySpec.Marker = ImPlotMarker_Circle;
			ImPlot::PlotLine("TOTEN", steps.data(), energies.data(), static_cast<int>(steps.size()), energySpec);
			if (hasValues(withoutEntropy))
				ImPlot::PlotLine("Without entropy", steps.data(), withoutEntropy.data(), static_cast<int>(steps.size()));
			if (hasValues(sigmaZero))
				ImPlot::PlotLine("Sigma -> 0", steps.data(), sigmaZero.data(), static_cast<int>(steps.size()));
			if (hasValues(forces))
			{
				ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
				ImPlotSpec forceSpec;
				forceSpec.Marker = ImPlotMarker_Diamond;
				ImPlot::PlotLine("Max |F|", steps.data(), forces.data(), static_cast<int>(steps.size()), forceSpec);
				ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
			}
			renderBoundaries();
			ImPlot::EndPlot();
		}

		ImGui::BeginDisabled(isLoading());
		ImGui::SetNextItemWidth(140.0f);
		bool changed = ImGui::InputInt("First band", &m_BandStart);
		ImGui::SetNextItemWidth(140.0f);
		changed = ImGui::InputInt("Last band (inclusive)", &m_BandEnd) || changed;
		if (changed)
		{
			m_BandStart = std::clamp(m_BandStart, 1, 1000000);
			m_BandEnd = std::clamp(m_BandEnd, m_BandStart, 1000000);
			m_AutoBands = false;
		}
		if (ImGui::Button("Apply band window"))
		{
			m_AutoBands = false;
			dispatchLoad();
		}
		ImGui::SameLine();
		if (ImGui::Button("Auto: HOMO-8 .. LUMO+8"))
		{
			m_AutoBands = true;
			dispatchLoad();
		}
		ImGui::EndDisabled();
		if (!hasKs)
		{
			ImGui::TextDisabled("KS levels unavailable (no complete OUTCAR band snapshots).");
			return;
		}
		if (!kpoints.contains(m_Kpoint))
			m_Kpoint = *kpoints.begin();
		if (kpoints.size() > 1)
		{
			const std::string current = std::to_string(m_Kpoint);
			if (ImGui::BeginCombo("K-point", current.c_str()))
			{
				for (const int kpoint : kpoints)
					if (ImGui::Selectable(std::to_string(kpoint).c_str(), kpoint == m_Kpoint))
						m_Kpoint = kpoint;
				ImGui::EndCombo();
			}
		}
		if (!hasFermi)
			m_RelativeToFermi = false;
		ImGui::BeginDisabled(!hasFermi);
		ImGui::Checkbox("Energy relative to each step's E-fermi", &m_RelativeToFermi);
		ImGui::EndDisabled();
		ImGui::TextDisabled("Up: blue / Down: orange. Filled: occupied; yellow diamond: partial; hollow: empty.");
		ImGui::TextDisabled("Incomplete KS snapshots are omitted; missing steps break the band lines.");

		struct Series
		{
			std::vector<double> x, y;
		};
		std::map<std::pair<int, int>, Series> bands;
		std::array<std::array<Series, 3>, 2> markers;
		std::size_t offset = 0;
		for (const auto &run : m_Chain)
		{
			if (!run.data || !run.data->summary.energyTrend)
				continue;
			const auto &summary = run.data->summary;
			for (const auto &step : summary.ksSteps)
			{
				for (const auto &block : step.blocks)
				{
					if (block.kpoint != m_Kpoint)
						continue;
					const std::size_t index = offset + static_cast<std::size_t>(step.step - 1);
					const double x = static_cast<double>(index + 1) + (block.spin == 1 ? -0.08 : 0.08);
					for (std::size_t i = 0; i < block.bands.size(); ++i)
					{
						auto &series = bands[{block.spin, block.bands[i]}];
						if (series.y.empty())
						{
							series.x = steps;
							for (auto &value : series.x)
								value += block.spin == 1 ? -0.08 : 0.08;
							series.y.assign(steps.size(), missing);
						}
						const double y = block.energies[i] - (m_RelativeToFermi ? step.fermi.value_or(0.0) : 0.0);
						series.y[index] = y;
						const double full = summary.ispin.value_or(1) == 2 ? 1.0 : 2.0;
						const int category = block.occupations[i] <= 1e-5 ? 2 :
							(block.occupations[i] >= full - 1e-3 ? 0 : 1);
						auto &points = markers[block.spin - 1][category];
						points.x.push_back(x);
						points.y.push_back(y);
					}
				}
			}
			offset += summary.energyTrend->size();
		}
		if (ImPlot::BeginPlot("KS levels", ImVec2(-1.0f, 320.0f)))
		{
			ImPlot::SetupAxes("Global ionic step", m_RelativeToFermi ? "Energy - E-fermi (eV)" : "Energy (eV)");
			const std::array<ImVec4, 2> colors = {ImVec4(0.3f, 0.55f, 1.0f, 1.0f), ImVec4(1.0f, 0.45f, 0.3f, 1.0f)};
			for (const auto &[key, series] : bands)
			{
				ImPlotSpec spec;
				spec.LineColor = colors[key.first - 1];
				spec.LineColor.w = 0.35f;
				spec.Flags = ImPlotItemFlags_NoLegend;
				const std::string label = "##band" + std::to_string(key.first) + "/" + std::to_string(key.second);
				ImPlot::PlotLine(label.c_str(), series.x.data(), series.y.data(), static_cast<int>(series.y.size()), spec);
			}
			const std::array<std::string, 3> states = {" occupied", " partial", " empty"};
			for (int spin = 0; spin < 2; ++spin)
				for (int category = 0; category < 3; ++category)
				{
					const auto &points = markers[spin][category];
					if (points.x.empty())
						continue;
					ImPlotSpec spec;
					spec.Marker = category == 1 ? ImPlotMarker_Diamond : ImPlotMarker_Circle;
					spec.MarkerLineColor = colors[spin];
					spec.MarkerFillColor = category == 2 ? ImVec4(0, 0, 0, 0) :
						(category == 1 ? ImVec4(1.0f, 0.85f, 0.1f, 1.0f) : colors[spin]);
					const std::string label = std::string(spin == 0 ? "Up" : "Down") + states[category];
					ImPlot::PlotScatter(label.c_str(), points.x.data(), points.y.data(), static_cast<int>(points.x.size()), spec);
				}
			renderBoundaries();
			ImPlot::EndPlot();
		}
	}
} // namespace DefectStudio
