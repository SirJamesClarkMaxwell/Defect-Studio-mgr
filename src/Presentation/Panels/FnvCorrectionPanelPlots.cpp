#include "Core/dspch.hpp"

#include "Presentation/Panels/FnvCorrectionPanel.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <implot.h>

namespace DefectStudio
{
	void FnvCorrectionPanel::drawPlots()
	{
		if (!ImGui::CollapsingHeader("Profile", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		if (ImGui::BeginTabBar("##FnvProfiles"))
		{
			constexpr std::array<const char *, 3> labels{"a1", "a2", "a3"};
			for (int a = 0; a < 3; ++a)
			{
				ImGui::PushID(a);
				if (ImGui::BeginTabItem(labels[a]))
				{
					drawAxisPlot(a);
					ImGui::EndTabItem();
				}
				ImGui::PopID();
			}
			ImGui::EndTabBar();
		}
	}

	void FnvCorrectionPanel::drawAxisPlot(const int a)
	{
		const auto &axis = m_Profiles->axes[a];
		const auto &model = m_ModelPotential[a];
		auto &alignment = m_Alignments[a];
		const double length = axis.length;
		const auto wrap = [length](double x) {
			const double value = std::fmod(x, length);
			return value < 0.0 ? value + length : value;
		};
		// The defect is a grid point. Rotate the periodic arrays together, then repeat that point
		// at L to close the curves without drawing a spurious line across the periodic seam.
		const std::size_t count = axis.position.size();
		const std::size_t start = static_cast<std::size_t>(
			std::lower_bound(axis.position.begin(), axis.position.end(), alignment.defectPosition) - axis.position.begin());
		std::vector<double> x(count + 1), delta(count + 1), potential(count + 1), residual(count + 1);
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::size_t source = (start + i) % count;
			x[i] = wrap(axis.position[source] - alignment.defectPosition);
			delta[i] = axis.deltaV[source];
			potential[i] = model[source];
			residual[i] = delta[i] - potential[i];
		}
		x.back() = length;
		delta.back() = delta.front();
		potential.back() = potential.front();
		residual.back() = residual.front();

		if (!ImPlot::BeginPlot("##FnvAxis", ImVec2(-1.0f, 220.0f)))
			return;
		ImPlot::SetupAxes("Odległość od defektu [Å]", "Potencjał [eV]", 0, ImPlotAxisFlags_AutoFit);
		ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, length, ImPlotCond_Always);
		ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
		ImPlot::SetupFinish();

		ImPlotSpec line;
		line.LineWeight = 1.5f;
		line.LineColor = ImVec4(235.0f / 255.0f, 104.0f / 255.0f, 52.0f / 255.0f, 1.0f);
		ImPlot::PlotLine("ΔV", x.data(), delta.data(), static_cast<int>(x.size()), line);
		line.LineColor = ImVec4(42.0f / 255.0f, 120.0f / 255.0f, 214.0f / 255.0f, 1.0f);
		ImPlot::PlotLine("V_model", x.data(), potential.data(), static_cast<int>(x.size()), line);
		line.LineColor = ImVec4(27.0f / 255.0f, 175.0f / 255.0f, 122.0f / 255.0f, 1.0f);
		ImPlot::PlotLine("ΔV − V_model", x.data(), residual.data(), static_cast<int>(x.size()), line);

		const double step = std::min(axis.position[1] - axis.position[0], length);
		const double halfWidth = std::min(alignment.window.width, length) / 2.0;
		const double centre = wrap(alignment.window.centre - alignment.defectPosition);
		double left = std::clamp(centre - halfWidth, 0.0, length - step);
		double right = std::clamp(centre + halfWidth, left + step, length);
		const ImVec4 handles(0.95f, 0.85f, 0.45f, 1.0f);
		const bool leftChanged = ImPlot::DragLineX(0, &left, handles, 1.5f,
			ImPlotDragToolFlags_NoFit | ImPlotDragToolFlags_Delayed);
		const bool rightChanged = ImPlot::DragLineX(1, &right, handles, 1.5f,
			ImPlotDragToolFlags_NoFit | ImPlotDragToolFlags_Delayed);
		if ((leftChanged || rightChanged) && std::isfinite(left) && std::isfinite(right))
		{
			if (leftChanged)
				left = std::clamp(left, 0.0, right - step);
			else
				right = std::clamp(right, left + step, length);
			const FnvPlateauWindow window{wrap(alignment.defectPosition + (left + right) / 2.0), right - left};
			alignment = AlignFnvInWindow(axis, model, window);
		}

		const auto limits = ImPlot::GetPlotLimits();
		const double edges[2]{left, right};
		const double bottom[2]{limits.Y.Min, limits.Y.Min}, top[2]{limits.Y.Max, limits.Y.Max};
		ImPlotSpec band;
		band.FillColor = handles;
		band.FillAlpha = 0.15f;
		band.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
		ImPlot::PlotShaded("##window", edges, bottom, top, 2, band);
		ImPlotSpec reference;
		reference.LineColor = handles;
		reference.Flags = ImPlotInfLinesFlags_Horizontal | ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
		ImPlot::PlotInfLines("##C", &alignment.alignment, 1, reference);
		ImPlot::EndPlot();
	}
} // namespace DefectStudio
