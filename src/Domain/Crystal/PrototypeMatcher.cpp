#include "Core/dspch.hpp"

#include "Domain/Crystal/PrototypeMatcher.hpp"

#include <algorithm>
#include <unordered_map>

namespace DefectStudio
{
	std::optional<SiteAssignment> PrototypeMatcher::MatchFormulaToPrototype(
		const std::vector<Element> &formula,
		const PrototypeDefinition &prototype)
	{
		if (formula.empty() || prototype.sites.empty())
			return std::nullopt;

		// Verify total count matches
		int formulaTotal = 0;
		for (const auto &elem : formula)
			formulaTotal += elem.count;

		int siteTotal = 0;
		for (const auto &site : prototype.sites)
			siteTotal += site.multiplicity;

		if (formulaTotal != siteTotal)
			return std::nullopt;

		// Group sites by multiplicity
		std::unordered_map<int, std::vector<size_t>> sitesByCount;
		for (size_t i = 0; i < prototype.sites.size(); ++i)
		{
			sitesByCount[prototype.sites[i].multiplicity].push_back(i);
		}

		SiteAssignment result;
		result.species.resize(prototype.sites.size());
		bool isAmbiguous = false;

		// Try to assign formula elements to sites
		std::vector<std::pair<int, size_t>> assignments; // (formula index, site index)

		for (size_t i = 0; i < formula.size(); ++i)
		{
			int count = formula[i].count;
			auto it = sitesByCount.find(count);

			if (it == sitesByCount.end() || it->second.empty())
				return std::nullopt; // No site with matching multiplicity

			// Check for ambiguity: multiple unassigned sites have the same multiplicity
			if (it->second.size() > 1)
				isAmbiguous = true;

			// Assign to the first available site with this multiplicity
			size_t siteIdx = it->second.back();
			it->second.pop_back();

			result.species[siteIdx] = formula[i].symbol;
			assignments.push_back({i, siteIdx});
		}

		result.isAmbiguous = isAmbiguous;
		return result;
	}
}
