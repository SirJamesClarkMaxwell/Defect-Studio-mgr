#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculationSummaryPanel.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"

#include "Core/Platform/FileDialog.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h> // BeginItem: links and arrows share the bars' legend entry

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
		if (ImPlot::BeginPlot("Convergence", ImVec2(-1.0f, m_ConvergenceHeight)))
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
		PlotHeightHandle("##convergenceHeight", m_ConvergenceHeight);

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
		if (ImGui::Button("Auto: HOMO-3 .. LUMO+3"))
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
		const auto edges = m_PickedEdges ? m_PickedEdges : m_BandEdgesProvider ? m_BandEdgesProvider() : std::nullopt;
		if (ImGui::Button("Bulk (VBM/CBM)..."))
			if (const auto picked = Platform::PickFolder(m_EdgesDirectory.Empty() ? m_Directory : m_EdgesDirectory); picked && picked->has_value())
			{
				m_EdgesDirectory = **picked;
				m_PickedEdges = VaspRunRecordIO::ReadBandEdges(m_EdgesDirectory);
				if (!m_PickedEdges)
					m_EdgesDirectory = {};
			}
		ImGui::SetItemTooltip("Folder obliczenia bulk z EIGENVAL. Bez wyboru: bulk z Defect Energetics.\n"
			"Energie KS defektu i bulku bez wyrównania potencjału (różnica zwykle ~0.1 eV).");
		ImGui::SameLine();
		if (!edges)
			ImGui::TextDisabled("VBM/CBM: brak bulku (EIGENVAL)");
		else
			ImGui::TextDisabled("VBM %.3f eV, CBM %.3f eV (%s)%s", edges->vbm, edges->cbm,
				m_PickedEdges ? m_EdgesDirectory.filename().Utf8().c_str() : "Defect Energetics",
				m_RelativeToFermi ? " - niewidoczne względem E-fermi" : "");
		ImGui::TextDisabled("Bars: KS levels; dashed: spin up, dotted: spin down; arrows: occupation (faded = partial).");

		// One series per (spin, band): the level at each global step and its occupation (0..1 per spin).
		struct Level
		{
			double x, y, fill;
		};
		std::map<std::pair<int, int>, std::vector<Level>> bands;
		std::set<int> bandIndices;
		bool spinPolarised = false;
		std::size_t offset = 0;
		for (const auto &run : m_Chain)
		{
			if (!run.data || !run.data->summary.energyTrend)
				continue;
			const auto &summary = run.data->summary;
			const double full = summary.ispin.value_or(1) == 2 ? 1.0 : 2.0;
			spinPolarised = spinPolarised || summary.ispin.value_or(1) == 2;
			for (const auto &step : summary.ksSteps)
				for (const auto &block : step.blocks)
				{
					if (block.kpoint != m_Kpoint)
						continue;
					const double x = static_cast<double>(offset + static_cast<std::size_t>(step.step));
					const double shift = m_RelativeToFermi ? step.fermi.value_or(0.0) : 0.0;
					for (std::size_t i = 0; i < block.bands.size(); ++i)
					{
						bands[{block.spin, block.bands[i]}].push_back({x, block.energies[i] - shift,
							std::clamp(block.occupations[i] / full, 0.0, 1.0)});
						bandIndices.insert(block.bands[i]);
					}
				}
			offset += summary.energyTrend->size();
		}
		if (ImPlot::BeginPlot("KS levels", ImVec2(-1.0f, m_KsHeight)))
		{
			ImPlot::SetupAxes("Global ionic step", m_RelativeToFermi ? "Energy - E-fermi (eV)" : "Energy (eV)");
			ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside);
			if (edges && !m_RelativeToFermi)
			{
				DrawBandEdges(edges->vbm, edges->cbm, false);
				const double lines[2]{edges->vbm, edges->cbm};
				ImPlotSpec edgeSpec;
				edgeSpec.LineColor = ImVec4(0.7f, 0.7f, 0.7f, 0.6f);
				edgeSpec.Flags = ImPlotItemFlags_NoLegend | static_cast<int>(ImPlotInfLinesFlags_Horizontal);
				ImPlot::PlotInfLines("##edges", lines, 2, edgeSpec);
				ImPlotSpec textSpec;
				textSpec.Flags = ImPlotItemFlags_NoFit;
				const double left = ImPlot::GetPlotLimits().X.Min;
				ImPlot::PlotText("VBM", left, edges->vbm, ImVec2(22.0f, 9.0f), textSpec);
				ImPlot::PlotText("CBM", left, edges->cbm, ImVec2(22.0f, -9.0f), textSpec);
			}
			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
			for (const auto &[key, levels] : bands)
			{
				const auto [spin, band] = key;
				const ImVec4 colour = ImPlot::GetColormapColor(static_cast<int>(std::distance(bandIndices.begin(), bandIndices.find(band))));
				const std::string label = std::format("band {}{}", band, spinPolarised ? (spin == 1 ? ", spin up" : ", spin down") : "");
				// The levels as short bars (fitted, in the legend).
				std::vector<double> xs, ys;
				for (const auto &level : levels)
				{
					xs.insert(xs.end(), {level.x - 0.25, level.x + 0.25});
					ys.insert(ys.end(), {level.y, level.y});
				}
				ImPlotSpec bars;
				bars.LineColor = colour;
				bars.LineWeight = 3.0f;
				bars.Flags = ImPlotLineFlags_Segments;
				ImPlot::PlotLine(label.c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), bars);
				// Same item (hidden with it): dashed (up) / dotted (down) links between consecutive steps
				// and an arrow per occupied level (up arrow left of centre, down arrow right).
				if (!ImPlot::BeginItem(label.c_str(), bars, colour))
					continue;
				ImDrawList *draw = ImPlot::GetPlotDrawList();
				const ImU32 line = ImGui::GetColorU32(colour);
				const float dash = spin == 1 ? 6.0f : 2.0f, gapLength = spin == 1 ? 4.0f : 3.0f;
				for (std::size_t i = 0; i + 1 < levels.size(); ++i)
				{
					if (levels[i + 1].x - levels[i].x > 1.5)
						continue; // a missing snapshot breaks the line
					const ImVec2 a = ImPlot::PlotToPixels(levels[i].x, levels[i].y);
					const ImVec2 b = ImPlot::PlotToPixels(levels[i + 1].x, levels[i + 1].y);
					const float length = std::hypot(b.x - a.x, b.y - a.y);
					for (float t = 0.0f; t < length; t += dash + gapLength)
					{
						const float u = t / length, v = std::min(t + dash, length) / length;
						draw->AddLine(ImVec2(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u),
							ImVec2(a.x + (b.x - a.x) * v, a.y + (b.y - a.y) * v), line, 1.5f);
					}
				}
				const auto arrow = [&](const ImVec2 centre, const bool up, const float alpha) {
					const ImU32 faded = (ink & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha * 255.0f) << IM_COL32_A_SHIFT);
					const float h = 9.0f, sign = up ? -1.0f : 1.0f;
					draw->AddLine(ImVec2(centre.x, centre.y - sign * h), ImVec2(centre.x, centre.y + sign * (h - 4.0f)), faded, 2.0f);
					draw->AddTriangleFilled(ImVec2(centre.x, centre.y + sign * h), ImVec2(centre.x - 4.0f, centre.y + sign * (h - 6.0f)),
						ImVec2(centre.x + 4.0f, centre.y + sign * (h - 6.0f)), faded);
				};
				for (const auto &level : levels)
				{
					if (level.fill < 0.05)
						continue;
					if (spinPolarised)
						arrow(ImPlot::PlotToPixels(level.x + (spin == 1 ? -0.08 : 0.08), level.y), spin == 1, static_cast<float>(level.fill));
					else
					{ // unpolarised: up arrow for the first electron, down for the second
						arrow(ImPlot::PlotToPixels(level.x - 0.08, level.y), true, static_cast<float>(std::min(1.0, 2.0 * level.fill)));
						if (level.fill > 0.5)
							arrow(ImPlot::PlotToPixels(level.x + 0.08, level.y), false, static_cast<float>(2.0 * level.fill - 1.0));
					}
				}
				ImPlot::EndItem();
			}
			renderBoundaries();
			ImPlot::EndPlot();
		}
		PlotHeightHandle("##ksHeight", m_KsHeight);
	}
} // namespace DefectStudio
