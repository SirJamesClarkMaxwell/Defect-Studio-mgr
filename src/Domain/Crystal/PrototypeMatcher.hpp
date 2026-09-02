#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Domain/Crystal/FormulaParser.hpp"
#include "Domain/Crystal/PrototypeDefinition.hpp"

namespace DefectStudio
{
	struct SiteAssignment
	{
		std::vector<std::string> species; // one per site, in site order
		bool isAmbiguous = false;         // true if multiple valid assignments exist
	};

	class PrototypeMatcher
	{
	public:
		// Attempt to match a formula to a prototype's sites.
		// Returns an assignment where species[i] is the element for the i-th site.
		// isAmbiguous=true if multiple valid assignments exist (user should fix it in the table).
		// Returns nullopt if no valid assignment is possible.
		[[nodiscard]] static std::optional<SiteAssignment> MatchFormulaToPrototype(
			const std::vector<Element> &formula,
			const PrototypeDefinition &prototype);
	};
}
