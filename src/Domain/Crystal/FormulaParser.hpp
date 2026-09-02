#pragma once

#include <string>
#include <vector>

namespace DefectStudio
{
	struct Element
	{
		std::string symbol;
		int count = 1;
	};

	class FormulaParser
	{
	public:
		// Parse a chemical formula string (e.g., "GaAs", "Al2O3", "ZnS2").
		// Returns a vector of (symbol, count) pairs in the order they appear in the formula.
		// Empty formula or parse error returns empty vector.
		[[nodiscard]] static std::vector<Element> Parse(const std::string &formula);
	};
}
