#include "Core/dspch.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"

#include <array>
#include <cmath>

namespace DefectStudio
{
	void ApplySeriesStyle(const PlotSeriesStyle &style, ImPlotSpec &spec)
	{
		spec.LineColor = style.color ? ImVec4(style.color->x, style.color->y, style.color->z, style.color->w) : IMPLOT_AUTO_COL;
		spec.FillColor = spec.LineColor;
		spec.LineWeight = style.lineWeight;
		spec.FillAlpha = style.fillAlpha;
	}

	bool SeriesLegendPopup(const char *label, PlotSeriesStyle &style, bool allowFill)
	{
		bool committed = false;
		if (!ImPlot::GetItem(label)) return false;
		auto &item = *ImPlot::GetItem(label);
		if (style.visible != item.Show)
		{
			style.visible = item.Show;
			committed = true; // Also persist the normal left-click legend toggle.
		}
		if (!ImPlot::BeginLegendPopup(label, ImGuiMouseButton_Right)) return committed;
		const auto itemColor = ImGui::ColorConvertU32ToFloat4(item.Color);
		const auto restoreAutoColor = [&] {
			item.Color = ImGui::ColorConvertFloat4ToU32(ImPlot::GetColormapColor(ImPlot::GetCurrentContext()->CurrentItems->GetItemIndex(&item)));
		};
		bool automatic = !style.color;
		if (ImGui::Checkbox("Auto", &automatic))
		{
			if (automatic) { style.color.reset(); restoreAutoColor(); }
			else style.color = glm::vec4(itemColor.x, itemColor.y, itemColor.z, itemColor.w);
			committed = true;
		}
		ImGui::BeginDisabled(automatic);
		glm::vec4 color = style.color.value_or(glm::vec4(itemColor.x, itemColor.y, itemColor.z, itemColor.w));
		if (ImGui::ColorEdit4("Kolor", &color.x)) style.color = color;
		committed |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::EndDisabled();
		ImGui::DragFloat("Grubość", &style.lineWeight, 0.05f, 0.5f, 6.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		committed |= ImGui::IsItemDeactivatedAfterEdit();
		committed |= ImGui::Checkbox("Widoczna", &style.visible);
		if (allowFill)
		{
			committed |= ImGui::Checkbox("Wypełnienie do zera", &style.filled);
			ImGui::BeginDisabled(!style.filled);
			ImGui::DragFloat("Alfa wypełnienia", &style.fillAlpha, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			committed |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::EndDisabled();
		}
		committed |= ImGui::Combo("Linia", &style.lineStyle, "Ciągła\0Kreskowana\0Kropkowana\0");
		if (ImGui::Button("Przywróć domyślne")) { style = {}; restoreAutoColor(); committed = true; }
		item.Show = style.visible;
		ImPlot::EndLegendPopup();
		return committed;
	}

	std::vector<glm::dvec2> PlotDashSegments(std::span<const glm::dvec2> points,
		const glm::dvec4 &bounds, int lineStyle)
	{
		std::vector<glm::dvec2> segments;
		const double period = lineStyle == 2 ? 6.0 : 12.0;
		const double on = lineStyle == 2 ? 1.0 : 8.0;
		double phase = 0.0;
		for (std::size_t i = 1; i < points.size(); ++i)
		{
			const auto a = points[i - 1], delta = points[i] - a;
			const double length = glm::length(delta);
			if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(length)) { phase = 0.0; continue; }
			if (length == 0.0) continue;
			double first = 0.0, last = 1.0;
			for (int axis = 0; axis < 2; ++axis)
			{
				if (delta[axis] == 0.0)
				{
					if (a[axis] < bounds[axis] || a[axis] > bounds[axis + 2]) last = -1.0;
					continue;
				}
				const double t0 = (bounds[axis] - a[axis]) / delta[axis];
				const double t1 = (bounds[axis + 2] - a[axis]) / delta[axis];
				first = std::max(first, std::min(t0, t1));
				last = std::min(last, std::max(t0, t1));
			}
			if (first < last)
			{
				const auto base = a + first * delta;
				const double visibleLength = (last - first) * length;
				const double offset = std::fmod(phase + std::fmod(first * length, period), period);
				for (double start = -offset; start < visibleLength; start += period)
				{
					const double lo = std::max(0.0, start), hi = std::min(visibleLength, start + on);
					if (hi > lo)
					{
						segments.push_back(base + (lo / length) * delta);
						segments.push_back(base + (hi / length) * delta);
					}
				}
			}
			phase = std::fmod(phase + std::fmod(length, period), period);
		}
		return segments;
	}

	void PlotStyledLine(const char *label, std::span<const double> xs, std::span<const double> ys,
		const PlotSeriesStyle &style, ImPlotItemFlags flags)
	{
		IM_ASSERT(xs.size() == ys.size());
		ImPlotSpec spec;
		ApplySeriesStyle(style, spec);
		// Initialize visibility once; the legend and popup then share the item's Show state.
		ImPlot::HideNextItem(!style.visible, ImPlotCond_Once);
		spec.Flags = flags | (style.filled ? ImPlotLineFlags_Shaded : 0);
		if (xs.empty()) { ImPlot::PlotDummy(label, spec); return; }
		if (style.lineStyle != 0) spec.LineWeight = 0.0f;
		ImPlot::PlotLine(label, xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
		if (style.lineStyle == 0 || !ImPlot::GetItem(label)->Show) return;
		std::vector<glm::dvec2> points;
		points.reserve(xs.size());
		for (std::size_t i = 0; i < xs.size(); ++i)
		{
			const auto pixel = ImPlot::PlotToPixels(xs[i], ys[i]);
			points.emplace_back(pixel.x, pixel.y);
		}
		const auto pos = ImPlot::GetPlotPos(), size = ImPlot::GetPlotSize();
		const auto segments = PlotDashSegments(points, {pos.x, pos.y, pos.x + size.x, pos.y + size.y}, style.lineStyle);
		std::vector<double> dx, dy;
		for (const auto &pixel : segments)
		{
			const auto point = ImPlot::PixelsToPlot(static_cast<float>(pixel.x), static_cast<float>(pixel.y));
			dx.push_back(point.x); dy.push_back(point.y);
		}
		spec.LineWeight = style.lineWeight;
		spec.Flags = flags | ImPlotItemFlags_NoFit | ImPlotLineFlags_Segments;
		ImPlot::PlotLine(label, dx.data(), dy.data(), static_cast<int>(dx.size()), spec);
	}

	void PlotStyledStems(const char *label, std::span<const double> xs, std::span<const double> ys,
		const PlotSeriesStyle &style)
	{
		IM_ASSERT(xs.size() == ys.size());
		ImPlotSpec spec;
		ApplySeriesStyle(style, spec);
		ImPlot::HideNextItem(!style.visible, ImPlotCond_Once);
		if (xs.empty()) { ImPlot::PlotDummy(label, spec); return; }
		if (style.lineStyle != 0) spec.LineWeight = 0.0f;
		ImPlot::PlotStems(label, xs.data(), ys.data(), static_cast<int>(xs.size()), 0.0, spec);
		if (style.lineStyle == 0 || !ImPlot::GetItem(label)->Show) return;
		auto line = style;
		line.filled = false;
		for (std::size_t i = 0; i < xs.size(); ++i)
			PlotStyledLine(label, std::array{xs[i], xs[i]}, std::array{0.0, ys[i]}, line, ImPlotItemFlags_NoFit);
	}

	void PlotStyledInfLine(const char *label, double x, const PlotSeriesStyle &style)
	{
		if (style.lineStyle == 0)
		{
			ImPlotSpec spec;
			ApplySeriesStyle(style, spec);
			ImPlot::HideNextItem(!style.visible, ImPlotCond_Once);
			spec.Flags = ImPlotItemFlags_NoFit;
			ImPlot::PlotInfLines(label, &x, 1, spec);
			return;
		}
		const auto limits = ImPlot::GetPlotLimits();
		auto line = style;
		line.filled = false;
		PlotStyledLine(label, std::array{x, x}, std::array{limits.Y.Min, limits.Y.Max}, line, ImPlotItemFlags_NoFit);
	}
}
