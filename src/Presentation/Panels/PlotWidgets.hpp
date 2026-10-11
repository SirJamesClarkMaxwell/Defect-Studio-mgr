#pragma once

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h> // ImPlotPlot::FrameRect/PlotRect for the TeX axis labels

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

	// A small subset of TeX for plot labels: "_x" / "_{..}" subscript, "^x" / "^{..}" superscript,
	// \mu \Delta \varepsilon \epsilon \lambda \sigma \cdot. Each run is UTF-8 text at one level
	// (-1 sub, 0 normal, +1 super). ponytail: no nesting beyond one level, no fractions - matplotlib
	// gets real LaTeX in the Figure module.
	struct TexRun
	{
		std::string text;
		int level = 0;
	};

	inline std::vector<TexRun> ParseTexLite(const std::string_view tex)
	{
		static constexpr std::pair<std::string_view, std::string_view> kSymbols[]{{"varepsilon", "ε"}, {"epsilon", "ε"},
			{"Delta", "Δ"}, {"lambda", "λ"}, {"sigma", "σ"}, {"mu", "μ"}, {"cdot", "·"}};
		std::vector<TexRun> runs;
		const auto append = [&](const std::string_view text, const int level) {
			if (runs.empty() || runs.back().level != level)
				runs.push_back({{}, level});
			runs.back().text += text;
		};
		for (std::size_t i = 0; i < tex.size();)
		{
			int level = 0;
			if ((tex[i] == '_' || tex[i] == '^') && i + 1 < tex.size())
			{
				level = tex[i] == '_' ? -1 : 1;
				++i;
			}
			std::string_view piece;
			if (level != 0 && tex[i] == '{')
			{
				const std::size_t close = tex.find('}', i);
				piece = tex.substr(i + 1, (close == std::string_view::npos ? tex.size() : close) - i - 1);
				i = close == std::string_view::npos ? tex.size() : close + 1;
			}
			else if (tex[i] == '\\')
			{
				std::size_t end = i + 1;
				while (end < tex.size() && std::isalpha(static_cast<unsigned char>(tex[end])))
					++end;
				piece = tex.substr(i, end - i);
				i = end;
			}
			else
			{
				std::size_t end = i + 1;
				while (end < tex.size() && (static_cast<unsigned char>(tex[end]) & 0xC0) == 0x80)
					++end; // keep a UTF-8 character whole
				piece = tex.substr(i, end - i);
				i = end;
			}
			// Symbols inside the piece.
			for (std::size_t k = 0; k < piece.size();)
			{
				if (piece[k] == '\\')
				{
					std::size_t end = k + 1;
					while (end < piece.size() && std::isalpha(static_cast<unsigned char>(piece[end])))
						++end;
					const std::string_view name = piece.substr(k + 1, end - k - 1);
					const auto symbol = std::find_if(std::begin(kSymbols), std::end(kSymbols), [&](const auto &entry) { return entry.first == name; });
					append(symbol != std::end(kSymbols) ? symbol->second : piece.substr(k, end - k), level);
					k = end;
				}
				else
				{
					const std::size_t next = std::min(piece.find('\\', k), piece.size());
					append(piece.substr(k, next - k), level);
					k = next;
				}
			}
		}
		return runs;
	}

	inline ImVec2 TexTextSize(const std::vector<TexRun> &runs, const float size)
	{
		ImVec2 total(0.0f, size);
		for (const TexRun &run : runs)
			total.x += ImGui::GetFont()->CalcTextSizeA(run.level ? 0.7f * size : size, FLT_MAX, 0.0f, run.text.c_str()).x;
		return total;
	}

	// `tex` with its top-left at `pos`; `vertical`: rotated to read bottom to top, `pos` is then
	// the bottom-left of the rotated block.
	inline void DrawTexText(ImDrawList *draw, const ImVec2 pos, const ImU32 colour, const std::string_view tex, const bool vertical)
	{
		const float size = ImGui::GetFontSize();
		const int first = draw->VtxBuffer.Size;
		float x = pos.x;
		for (const TexRun &run : ParseTexLite(tex))
		{
			const float runSize = run.level ? 0.7f * size : size;
			const float y = pos.y + (run.level < 0 ? 0.45f * size : run.level > 0 ? -0.1f * size : 0.0f);
			draw->AddText(ImGui::GetFont(), runSize, ImVec2(x, y), colour, run.text.c_str());
			x += ImGui::GetFont()->CalcTextSizeA(runSize, FLT_MAX, 0.0f, run.text.c_str()).x;
		}
		if (vertical) // rotate what was just drawn by -90 degrees about pos: (x, y) -> (y, -x)
			for (int v = first; v < draw->VtxBuffer.Size; ++v)
			{
				ImVec2 &p = draw->VtxBuffer[v].pos;
				p = ImVec2(pos.x + (p.y - pos.y), pos.y - (p.x - pos.x));
			}
	}

	// Inside BeginPlot/EndPlot (after the axes were set up with " " labels, which reserves their
	// space): the frame to put TeX labels on after EndPlot.
	inline ImRect CurrentPlotFrame(ImRect *plotRect)
	{
		const ImPlotPlot *plot = ImPlot::GetCurrentPlot();
		*plotRect = plot->PlotRect;
		return plot->FrameRect;
	}

	// After EndPlot: x label centred under the plot, y label rotated left of it (nullptr = none).
	inline void DrawTexAxisLabels(const ImRect &frame, const ImRect &plot, const char *xLabel, const char *yLabel)
	{
		ImDrawList *draw = ImGui::GetWindowDrawList();
		const ImU32 colour = ImGui::GetColorU32(ImGuiCol_Text);
		const ImVec2 padding = ImPlot::GetStyle().PlotPadding;
		const float size = ImGui::GetFontSize();
		if (xLabel)
		{
			const ImVec2 extent = TexTextSize(ParseTexLite(xLabel), size);
			DrawTexText(draw, ImVec2(0.5f * (plot.Min.x + plot.Max.x - extent.x), frame.Max.y - padding.y - extent.y), colour, xLabel, false);
		}
		if (yLabel)
		{
			const ImVec2 extent = TexTextSize(ParseTexLite(yLabel), size);
			DrawTexText(draw, ImVec2(frame.Min.x + padding.x, 0.5f * (plot.Min.y + plot.Max.y + extent.x)), colour, yLabel, true);
		}
	}
} // namespace DefectStudio
