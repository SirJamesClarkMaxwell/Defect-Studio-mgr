#include "Core/dspch.hpp"

#include "Domain/Crystal/FormulaParser.hpp"

#include <cctype>

namespace DefectStudio
{
	std::vector<Element> FormulaParser::Parse(const std::string &formula)
	{
		std::vector<Element> result;

		if (formula.empty())
			return result;

		for (size_t i = 0; i < formula.length(); ++i)
		{
			// Expect uppercase letter (start of element symbol)
			if (!std::isupper(formula[i]))
				continue;

			std::string symbol;
			symbol += formula[i];

			// Collect lowercase letters (e.g., "He", "Cl")
			while (i + 1 < formula.length() && std::islower(formula[i + 1]))
			{
				symbol += formula[++i];
			}

			// Collect digits for the count
			int count = 0;
			while (i + 1 < formula.length() && std::isdigit(formula[i + 1]))
			{
				count = count * 10 + (formula[++i] - '0');
			}

			result.push_back({symbol, count == 0 ? 1 : count});
		}

		return result;
	}
}
