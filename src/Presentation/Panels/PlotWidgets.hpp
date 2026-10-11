#pragma once

#include <algorithm>

#include <imgui.h>
#include <implot.h>

namespace DefectStudio
{
	// A thin bar under a plot: drag it to change the plot's height (kept by the caller).
	inline void PlotHeightHandle(const char *id, float &height, const float minHeight = 120.0f)
	{
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		ImGui::InvisibleButton(id, ImVec2(width, 6.0f));
		const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
		if (hot)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		if (ImGui::IsItemActive())
			height = std::max(minHeight, height + ImGui::GetIO().MouseDelta.y);
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		const float y = 0.5f * (min.y + max.y);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, y), ImVec2(max.x, y),
			ImGui::GetColorU32(hot ? ImGuiCol_SeparatorActive : ImGuiCol_Separator), hot ? 3.0f : 1.0f);
	}

	// Inside the current ImPlot plot: the valence band (red, below `vbm`) and the conduction band
	// (blue, above `cbm`), each fading away from its edge. `alongX`: the energy is the x axis.
	inline void DrawBandEdges(const double vbm, const double cbm, const bool alongX)
	{
		const ImPlotRect limits = ImPlot::GetPlotLimits();
		const ImU32 red = IM_COL32(220, 60, 60, 120), redClear = IM_COL32(220, 60, 60, 0);
		const ImU32 blue = IM_COL32(60, 110, 235, 120), blueClear = IM_COL32(60, 110, 235, 0);
		ImDrawList *draw = ImPlot::GetPlotDrawList();
		ImPlot::PushPlotClipRect();
		if (alongX)
		{
			if (limits.X.Min < vbm)
				draw->AddRectFilledMultiColor(ImPlot::PlotToPixels(limits.X.Min, limits.Y.Max),
					ImPlot::PlotToPixels(vbm, limits.Y.Min), redClear, red, red, redClear);
			if (limits.X.Max > cbm)
				draw->AddRectFilledMultiColor(ImPlot::PlotToPixels(cbm, limits.Y.Max),
					ImPlot::PlotToPixels(limits.X.Max, limits.Y.Min), blue, blueClear, blueClear, blue);
		}
		else
		{
			if (limits.Y.Min < vbm)
				draw->AddRectFilledMultiColor(ImPlot::PlotToPixels(limits.X.Min, vbm),
					ImPlot::PlotToPixels(limits.X.Max, limits.Y.Min), red, red, redClear, redClear);
			if (limits.Y.Max > cbm)
				draw->AddRectFilledMultiColor(ImPlot::PlotToPixels(limits.X.Min, limits.Y.Max),
					ImPlot::PlotToPixels(limits.X.Max, cbm), blueClear, blueClear, blue, blue);
		}
		ImPlot::PopPlotClipRect();
	}
} // namespace DefectStudio
