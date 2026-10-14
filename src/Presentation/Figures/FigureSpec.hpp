#pragma once

#include <string>
#include <vector>

#include "Presentation/Figures/PlotData.hpp"

namespace DefectStudio
{
	struct FigureSpec
	{
		int schemaVersion = 1;
		std::string name;
		double widthCm = 8.5, heightCm = 6.5;
		std::string style = "thesis";
		std::vector<std::string> formats{"pdf"};
		int dpi = 300;
		PlotData plot;
		std::string createdBy;
	};

	[[nodiscard]] std::string SerializeFigureSpec(const FigureSpec &spec);
	[[nodiscard]] bool ParseFigureSpec(const std::string &json, FigureSpec &out, std::string &outError);
}
