#pragma once

#include <string>
#include "Core/Utils/Path.hpp"
#include "Domain/Vibrations/CcdAnalysis.hpp"

namespace DefectStudio::CcdAnalysisIO
{
	// summary.json is required; missing optional TSVs leave their vectors empty. Header-driven
	// columns, finite sorted grids, NaN allowed only for missing point energies / PL columns.
	// Failure names the file and preserves out. Catches IO/parser exceptions at this boundary.
	// Optional CCD displacement failures populate out.loadWarnings without failing the result load.
	[[nodiscard]] bool Load(const Path &directory, CcdAnalysisResult &out, std::string &error);
}
