#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"

#include <algorithm>
#include <format>
#include <limits>
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

	// E_f(E_F) of every included state under the chosen mu set (each defect's envelope thick, the
	// other charge lines optional), the bands outside the gap, and the transition levels as their
	// own plot and table (they do not depend on mu).
	void DefectEnergeticsPanel::drawFormation()
	{
		if (m_Session->m_Model.muSets.empty() || m_Session->m_Model.reference.gap <= 0.0)
		{
			ImGui::TextDisabled("Potrzebne: bulk z przerwą i potencjały chemiczne.");
			return;
		}
		if (m_View == View::Formation)
		{
			drawMuCombo();
			drawContinuations();
		}
		const auto &set = m_Session->m_Model.muSets[static_cast<std::size_t>(m_Session->m_MuSet)];
		const SpeciesEnergies mu = AbsoluteChemicalPotentials(m_Session->m_Model.chemicalPotentials, set.deltaMu);
		const double gap = m_Session->m_Model.reference.gap;
		const double margin = std::max(0.25, 0.08 * gap);

		std::map<std::string, DefectLines> defects;
		for (const auto &row : m_Session->m_Model.rows)
		{
			if (!row.included)
				continue;
			const auto state = EnergeticsRowState(*m_Session->m_File, row);
			const auto intercept = state ? FormationEnergy(*state, m_Session->m_Model.reference, mu, 0.0) : std::nullopt;
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

		if (m_View == View::Formation)
		{
			// E_f range of what is drawn; the axis follows it only when it changes, so it can be zoomed.
			std::array<double, 2> range{std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
			const auto widen = [&](const double y) { range = {std::min(range[0], y), std::max(range[1], y)}; };
			for (const auto &[defect, lines] : defects)
			{
				for (const auto &segment : lines.envelope)
					for (const double x : {segment.from, segment.to})
						widen(lines.intercepts[segment.line] + lines.charges[segment.line] * x);
				for (std::size_t k = 0; k < lines.intercepts.size(); ++k)
					if (m_Session->m_Continuations.contains({defect, lines.charges[k]}))
					{
						widen(lines.intercepts[k]);
						widen(lines.intercepts[k] + lines.charges[k] * gap);
					}
			}
			const bool refit = range[0] <= range[1] && range != m_FormationRange;
			if (refit)
				m_FormationRange = range;
			const double pad = std::max(0.2, 0.08 * (range[1] - range[0]));

			ImPlotSpec textSpec;
			textSpec.Flags = ImPlotItemFlags_NoFit;
			if (ImPlot::BeginPlot("##Formation", ImVec2(-1.0f, std::max(1.0f, ImGui::GetContentRegionAvail().y))))
			{
				ImPlot::SetupAxes("E_F - E_VBM [eV]", "E_f [eV]");
				ImPlot::SetupAxisLimits(ImAxis_X1, -margin, gap + margin, ImPlotCond_Always);
				if (refit)
					ImPlot::SetupAxisLimits(ImAxis_Y1, range[0] - pad, range[1] + pad, ImPlotCond_Always);
				ImPlot::SetupLegend(ImPlotLocation_NorthEast);
				DrawBandEdges(0.0, gap, true);
				ImPlot::PlotText("VBM", -0.5 * margin, ImPlot::GetPlotLimits().Y.Max, ImVec2(0.0f, 10.0f), textSpec);
				ImPlot::PlotText("CBM", gap + 0.5 * margin, ImPlot::GetPlotLimits().Y.Max, ImVec2(0.0f, 10.0f), textSpec);
				for (const auto &[defect, lines] : defects)
				{
					ImPlotSpec thin;
					thin.LineColor = ImVec4(lines.colour.x, lines.colour.y, lines.colour.z, 0.35f);
					thin.Flags = ImPlotItemFlags_NoLegend;
					for (std::size_t k = 0; k < lines.intercepts.size(); ++k)
					{
						if (!m_Session->m_Continuations.contains({defect, lines.charges[k]}))
							continue;
						const double xs[2]{0.0, gap};
						const double ys[2]{lines.intercepts[k], lines.intercepts[k] + lines.charges[k] * gap};
						ImPlot::PlotLine(("##" + defect + std::to_string(lines.charges[k])).c_str(), xs, ys, 2, thin);
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

			return;
		}
		ImPlotSpec textSpec;
		textSpec.Flags = ImPlotItemFlags_NoFit;
		// Charge transition levels in the gap, one column per defect.
		std::vector<std::string> names;
		for (const auto &[defect, lines] : defects)
			if (!lines.transitions.empty())
				names.push_back(defect);
		if (!ImGui::BeginTabBar("##TransitionsTabs"))
			return;
		if (ImGui::BeginTabItem("Wykres CTL"))
		{
			if (names.empty())
				ImGui::TextDisabled("Brak przejść w przerwie.");
			if (!names.empty() && ImPlot::BeginPlot("##Transitions", ImVec2(-1.0f, std::max(1.0f, ImGui::GetContentRegionAvail().y))))
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
				DrawBandEdges(0.0, gap, false);
				ImPlot::PlotText("VBM", 0.0, -0.5 * margin, ImVec2(0.0f, 0.0f), textSpec);
				ImPlot::PlotText("CBM", 0.0, gap + 0.5 * margin, ImVec2(0.0f, 0.0f), textSpec);
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
							ticks[i], level.level, ImVec2(0.0f, -9.0f), textSpec);
					}
				}
				ImPlot::EndPlot();
			}

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Tabela przejść"))
		{
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
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
} // namespace DefectStudio
