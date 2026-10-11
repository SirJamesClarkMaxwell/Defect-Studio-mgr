#include "Core/dspch.hpp"

#include "Presentation/Panels/FnvCorrectionPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <imgui.h>
#include <implot.h>

namespace DefectStudio
{
	namespace
	{
		// a1, a2, a3 as x, y, z in the viewport gizmo: red, green, blue.
		constexpr std::array<ImVec4, 3> kAxisColors{ImVec4(0.90f, 0.25f, 0.25f, 1.0f),
			ImVec4(0.30f, 0.75f, 0.30f, 1.0f), ImVec4(0.30f, 0.50f, 0.95f, 1.0f)};

		ImVec4 Faded(ImVec4 colour, float alpha)
		{
			colour.w = alpha;
			return colour;
		}
	} // namespace

	// All three axes on one plot, defect in the middle (x = 0) and its periodic images at the edges
	// (+-L/2): ΔV − V_model (what C is read from) always, ΔV and V_model on request, every plateau
	// window shaded; the handles move the window of the chosen axis.
	void FnvCorrectionPanel::drawPlots()
	{
		if (!ImGui::CollapsingHeader("Profile", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		ImGui::Checkbox("ΔV", &m_ShowDeltaV);
		ImGui::SameLine();
		ImGui::Checkbox("V_model", &m_ShowModel);
		ImGui::SameLine(0.0f, 2.0f * ImGui::GetStyle().ItemSpacing.x);
		ImGui::TextUnformatted("Okno:");
		for (int a = 0; a < 3; ++a)
		{
			ImGui::SameLine();
			ImGui::PushID(a);
			ImGui::RadioButton(m_AxisLabels[a].c_str(), &m_EditedAxis, a);
			ImGui::PopID();
		}
		ImGui::SetItemTooltip("Oś, której okno plateau przesuwają żółte uchwyty na wykresie.");

		double length = 0.0;
		for (const auto &axis : m_Profiles->axes)
			length = std::max(length, axis.length);
		// Square, sized by the panel width.
		if (!ImPlot::BeginPlot("##FnvAxes", ImVec2(-1.0f, ImGui::GetContentRegionAvail().x)))
			return;
		ImPlot::SetupAxes("Odległość od defektu [Å]", "Potencjał [eV]", 0, ImPlotAxisFlags_AutoFit);
		ImPlot::SetupAxisLimits(ImAxis_X1, -length / 2.0, length / 2.0, ImPlotCond_Always);
		ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
		ImPlot::SetupFinish();
		for (int a = 0; a < 3; ++a)
			if (a != m_EditedAxis)
				plotAxis(a, false);
		plotAxis(m_EditedAxis, true); // last, so its handles sit on top
		ImPlot::EndPlot();
	}

	void FnvCorrectionPanel::plotAxis(const int a, const bool editable)
	{
		const auto &axis = m_Profiles->axes[a];
		const auto &model = m_ModelPotential[a];
		auto &alignment = m_Alignments[a];
		const double length = axis.length;
		const double half = length / 2.0;
		// Signed distance from the defect, wrapped into [-L/2, L/2).
		const auto offset = [length, half](double x) {
			const double value = std::fmod(x + half, length);
			return (value < 0.0 ? value + length : value) - half;
		};
		// Rotate the periodic arrays so they start at -L/2, then repeat the first point one period
		// later to close the curves at +L/2 without a spurious line across the seam.
		const std::size_t count = axis.position.size();
		std::size_t start = 0;
		for (std::size_t i = 1; i < count; ++i)
			if (offset(axis.position[i] - alignment.defectPosition) < offset(axis.position[start] - alignment.defectPosition))
				start = i;
		std::vector<double> x(count + 1), delta(count + 1), potential(count + 1), residual(count + 1);
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::size_t source = (start + i) % count;
			x[i] = offset(axis.position[source] - alignment.defectPosition);
			delta[i] = axis.deltaV[source];
			potential[i] = model[source];
			residual[i] = delta[i] - potential[i];
		}
		x.back() = x.front() + length;
		delta.back() = delta.front();
		potential.back() = potential.front();
		residual.back() = residual.front();

		const std::string &label = m_AxisLabels[a];
		const ImVec4 colour = kAxisColors[a];
		ImPlotSpec line;
		if (m_ShowModel)
		{
			line.LineWeight = 3.0f;
			line.LineColor = Faded(colour, 0.35f);
			ImPlot::PlotLine(("V_model " + label).c_str(), x.data(), potential.data(), static_cast<int>(x.size()), line);
		}
		if (m_ShowDeltaV)
		{
			line.LineWeight = 1.0f;
			line.LineColor = Faded(colour, 0.5f);
			ImPlot::PlotLine(("ΔV " + label).c_str(), x.data(), delta.data(), static_cast<int>(x.size()), line);
		}
		line.LineWeight = editable ? 2.0f : 1.5f;
		line.LineColor = colour;
		ImPlot::PlotLine(("ΔV − V_model " + label).c_str(), x.data(), residual.data(), static_cast<int>(x.size()), line);

		// The window runs from `begin` to `end` in the +x direction; the usual one sits opposite the
		// defect, so it crosses the edge and begin > end (two pieces, at both edges of the plot).
		const double step = std::min(axis.position[1] - axis.position[0], length);
		const double width = std::clamp(alignment.window.width, step, length - step);
		const double centre = alignment.window.centre - alignment.defectPosition;
		double begin = offset(centre - width / 2.0);
		double end = offset(centre + width / 2.0);
		const ImVec4 handles(0.95f, 0.85f, 0.45f, 1.0f);
		if (editable)
		{
			const bool beginChanged = ImPlot::DragLineX(0, &begin, handles, 1.5f,
				ImPlotDragToolFlags_NoFit | ImPlotDragToolFlags_Delayed);
			const bool endChanged = ImPlot::DragLineX(1, &end, handles, 1.5f,
				ImPlotDragToolFlags_NoFit | ImPlotDragToolFlags_Delayed);
			if ((beginChanged || endChanged) && std::isfinite(begin) && std::isfinite(end))
			{
				begin = std::clamp(begin, -half, half);
				end = std::clamp(end, -half, half);
				const double span = std::fmod(end - begin + 2.0 * length, length);
				const double newWidth = std::clamp(span, step, length - step);
				if (beginChanged)
					begin = end - newWidth;
				else
					end = begin + newWidth;
				const double newCentre = std::fmod(alignment.defectPosition + begin + newWidth / 2.0 + 2.0 * length, length);
				alignment = AlignFnvInWindow(axis, model, FnvPlateauWindow{newCentre, newWidth});
				m_Dirty = true;
				begin = offset(begin);
				end = offset(end);
			}
		}

		const auto limits = ImPlot::GetPlotLimits();
		ImPlotSpec band;
		band.FillColor = editable ? handles : colour;
		band.FillAlpha = editable ? 0.15f : 0.06f;
		band.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
		// C of this axis across its window only, so three C values stay readable.
		ImPlotSpec reference;
		reference.LineColor = editable ? handles : colour;
		reference.LineWeight = 2.0f;
		reference.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
		const auto piece = [&](double from, double to, const char *suffix) {
			const double edges[2]{from, to};
			const double bottom[2]{limits.Y.Min, limits.Y.Min}, top[2]{limits.Y.Max, limits.Y.Max};
			const double level[2]{alignment.alignment, alignment.alignment};
			ImPlot::PlotShaded(("##window" + label + suffix).c_str(), edges, bottom, top, 2, band);
			ImPlot::PlotLine(("##C" + label + suffix).c_str(), edges, level, 2, reference);
		};
		if (begin <= end)
			piece(begin, end, "");
		else
		{
			piece(begin, half, "+");
			piece(-half, end, "-");
		}
	}
} // namespace DefectStudio
