#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <algorithm>
#include <format>
#include <map>

#include <imgui.h>
#include <implot.h>

namespace DefectStudio
{
	namespace
	{
		// One defect under the chosen mu set: its charge lines, envelope and levels.
		struct DefectLines
		{
			std::vector<double> intercepts; // E_f at E_F = 0
			std::vector<int> charges;
			std::vector<EnvelopeSegment> envelope;
			std::vector<ChargeTransition> transitions;
			ImVec4 colour;
		};
	} // namespace

	// The valence band (red, below E_F = 0) and the conduction band (blue, above E_g), each
	// fading away from its edge. `alongX`: the energy is the x axis (E_f(E_F) plot), else y.
	void DefectEnergeticsPanel::drawBands(const double gap, const bool alongX)
	{
		const ImPlotRect limits = ImPlot::GetPlotLimits();
		const ImU32 red = IM_COL32(220, 60, 60, 120), redClear = IM_COL32(220, 60, 60, 0);
		const ImU32 blue = IM_COL32(60, 110, 235, 120), blueClear = IM_COL32(60, 110, 235, 0);
		ImDrawList *draw = ImPlot::GetPlotDrawList();
		ImPlot::PushPlotClipRect();
		if (alongX)
		{
			if (limits.X.Min < 0.0)
				draw->AddRectFilledMultiColor(ImPlot::PlotToPixels(limits.X.Min, limits.Y.Max),
					ImPlot::PlotToPixels(0.0, limits.Y.Min), redClear, red, red, redClear);
			if (limits.X.Max > gap)
				draw->AddRectFilledMultiColor(ImPlot::PlotToPixels(gap, limits.Y.Max),
					ImPlot::PlotToPixels(limits.X.Max, limits.Y.Min), blue, blueClear, blueClear, blue);
		}
		else
		{
			if (limits.Y.Min < 0.0)
				draw->AddRectFilledMultiColor(ImPlot::PlotToPixels(limits.X.Min, 0.0),
					ImPlot::PlotToPixels(limits.X.Max, limits.Y.Min), red, red, redClear, redClear);
			if (limits.Y.Max > gap)
				draw->AddRectFilledMultiColor(ImPlot::PlotToPixels(limits.X.Min, limits.Y.Max),
					ImPlot::PlotToPixels(limits.X.Max, gap), blueClear, blueClear, blue, blue);
		}
		ImPlot::PopPlotClipRect();
	}

	// E_f(E_F) of every included state under the chosen mu set (each defect's envelope thick, the
	// other charge lines optional), the bands outside the gap, and the transition levels as their
	// own plot and table (they do not depend on mu).
	void DefectEnergeticsPanel::drawFormation()
	{
		if (!ImGui::CollapsingHeader("Energie tworzenia", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		if (m_Model.muSets.empty() || m_Model.reference.gap <= 0.0)
		{
			ImGui::TextDisabled("Potrzebne: bulk z przerwą i potencjały chemiczne.");
			return;
		}
		const auto &set = m_Model.muSets[static_cast<std::size_t>(m_MuSet)];
		const SpeciesEnergies mu = AbsoluteChemicalPotentials(m_Model.chemicalPotentials, set.deltaMu);
		const double gap = m_Model.reference.gap;
		const double margin = std::max(0.25, 0.08 * gap);
		ImGui::Text("Zestaw μ: %s%s", set.vertex ? "bogate: " : "", set.name.c_str());
		ImGui::SameLine();
		ImGui::Checkbox("Kontynuacje krzywych", &m_ShowContinuations);
		ImGui::SetItemTooltip("Wszystkie stany ładunkowe na całej przerwie, nie tylko obwiednia.");

		std::map<std::string, DefectLines> defects;
		for (const auto &row : m_Model.rows)
		{
			if (!row.included)
				continue;
			const auto state = EnergeticsRowState(*m_File, row);
			const auto intercept = state ? FormationEnergy(*state, m_Model.reference, mu, 0.0) : std::nullopt;
			if (!intercept)
				continue;
			defects[row.defect].intercepts.push_back(*intercept);
			defects[row.defect].charges.push_back(row.charge);
		}
		int colour = 0;
		for (auto &[defect, lines] : defects)
		{
			lines.envelope = LowerEnvelope(lines.intercepts, lines.charges, gap);
			lines.transitions = EnvelopeTransitions(lines.envelope, lines.charges);
			lines.colour = ImPlot::GetColormapColor(colour++);
		}

		if (ImPlot::BeginPlot("##Formation", ImVec2(-1.0f, std::max(220.0f, 0.6f * ImGui::GetContentRegionAvail().x))))
		{
			ImPlot::SetupAxes("E_F - E_VBM [eV]", "E_f [eV]", 0, ImPlotAxisFlags_AutoFit);
			ImPlot::SetupAxisLimits(ImAxis_X1, -margin, gap + margin, ImPlotCond_Always);
			ImPlot::SetupLegend(ImPlotLocation_NorthEast);
			drawBands(gap, true);
			ImPlot::PlotText("VBM", -0.5 * margin, ImPlot::GetPlotLimits().Y.Max, ImVec2(0.0f, 10.0f));
			ImPlot::PlotText("CBM", gap + 0.5 * margin, ImPlot::GetPlotLimits().Y.Max, ImVec2(0.0f, 10.0f));
			for (const auto &[defect, lines] : defects)
			{
				if (m_ShowContinuations)
				{
					ImPlotSpec thin;
					thin.LineColor = ImVec4(lines.colour.x, lines.colour.y, lines.colour.z, 0.35f);
					thin.Flags = ImPlotItemFlags_NoLegend;
					for (std::size_t k = 0; k < lines.intercepts.size(); ++k)
					{
						const double xs[2]{0.0, gap};
						const double ys[2]{lines.intercepts[k], lines.intercepts[k] + lines.charges[k] * gap};
						ImPlot::PlotLine(("##" + defect + std::to_string(lines.charges[k])).c_str(), xs, ys, 2, thin);
					}
				}
				std::vector<double> xs, ys;
				for (const auto &segment : lines.envelope)
					for (const double x : {segment.from, segment.to})
					{
						xs.push_back(x);
						ys.push_back(lines.intercepts[segment.line] + lines.charges[segment.line] * x);
					}
				ImPlotSpec thick;
				thick.LineColor = lines.colour;
				thick.LineWeight = 2.5f;
				ImPlot::PlotLine(defect.c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), thick);
			}
			ImPlot::EndPlot();
		}

		// Charge transition levels in the gap, one column per defect.
		std::vector<std::string> names;
		for (const auto &[defect, lines] : defects)
			if (!lines.transitions.empty())
				names.push_back(defect);
		if (!names.empty() && ImPlot::BeginPlot("##Transitions", ImVec2(-1.0f, std::max(200.0f, 0.45f * ImGui::GetContentRegionAvail().x))))
		{
			std::vector<double> ticks(names.size());
			std::vector<const char *> labels(names.size());
			for (std::size_t i = 0; i < names.size(); ++i)
			{
				ticks[i] = static_cast<double>(i);
				labels[i] = names[i].c_str();
			}
			ImPlot::SetupAxes(nullptr, "ε − E_VBM [eV]");
			ImPlot::SetupAxisLimits(ImAxis_X1, -0.6, static_cast<double>(names.size()) - 0.4, ImPlotCond_Always);
			ImPlot::SetupAxisLimits(ImAxis_Y1, -margin, gap + margin, ImPlotCond_Always);
			ImPlot::SetupAxisTicks(ImAxis_X1, ticks.data(), static_cast<int>(ticks.size()), labels.data());
			drawBands(gap, false);
			ImPlot::PlotText("VBM", 0.0, -0.5 * margin);
			ImPlot::PlotText("CBM", 0.0, gap + 0.5 * margin);
			for (std::size_t i = 0; i < names.size(); ++i)
			{
				const DefectLines &lines = defects[names[i]];
				ImPlotSpec spec;
				spec.LineColor = lines.colour;
				spec.LineWeight = 3.0f;
				spec.Flags = ImPlotItemFlags_NoLegend;
				for (const auto &level : lines.transitions)
				{
					const double xs[2]{ticks[i] - 0.3, ticks[i] + 0.3};
					const double ys[2]{level.level, level.level};
					ImPlot::PlotLine(std::format("##{}{}{}", names[i], level.chargeFrom, level.chargeTo).c_str(), xs, ys, 2, spec);
					ImPlot::PlotText(std::format("({:+d}/{:+d}) {:.2f}", level.chargeFrom, level.chargeTo, level.level).c_str(),
						ticks[i], level.level, ImVec2(0.0f, -9.0f));
				}
			}
			ImPlot::EndPlot();
		}

		if (ImGui::BeginTable("##TransitionTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Defekt");
			ImGui::TableSetupColumn("Przejście");
			ImGui::TableSetupColumn("ε od VBM [eV]");
			ImGui::TableSetupColumn("E_g − ε [eV]");
			ImGui::TableHeadersRow();
			for (const auto &[defect, lines] : defects)
				for (const auto &level : lines.transitions)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(defect.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("(%+d/%+d)", level.chargeFrom, level.chargeTo);
					ImGui::TableNextColumn();
					ImGui::Text("%.4f", level.level);
					ImGui::TableNextColumn();
					ImGui::Text("%.4f", gap - level.level);
				}
			ImGui::EndTable();
		}
	}
} // namespace DefectStudio
