#pragma once

#include <string>
#include <span>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "IO/PlotSeriesStyle.hpp"
#include "IO/DataTableIO.hpp"

namespace DefectStudio
{
	enum class PlotSeriesKind { Line, Stems, Scatter, VerticalLine, Segments };

	struct PlotMarker
	{
		enum class Shape { Circle, Diamond, Square };
		Shape shape = Shape::Circle;
		float size = 5.0f;
		bool filled = true;
	};

	struct PlotSeriesData
	{
		std::string id;
		std::string label;
		PlotSeriesKind kind = PlotSeriesKind::Line;
		std::vector<double> x, y;
		int yAxis = 0;
		Path source;
		PlotSeriesStyle style;
		PlotMarker marker;
	};

	struct PlotAxisData
	{
		std::string labelTex;
		std::string unit;
		double minimum = 0, maximum = 0;
		bool log = false;
	};

	struct PlotData
	{
		std::string id;
		std::string title;
		PlotAxisData x, y, y2;
		bool hasY2 = false;
		std::vector<PlotSeriesData> series;
	};

	[[nodiscard]] std::vector<DataTableColumn> PlotDataColumns(const PlotData &plot, const Path &project,
		bool onlyVisible = true);
	[[nodiscard]] std::string PlotSourceComment(const Path &source, const Path &project);
	void ResolvePlotColors(PlotData &plot, std::span<const glm::vec4> colors);
}
