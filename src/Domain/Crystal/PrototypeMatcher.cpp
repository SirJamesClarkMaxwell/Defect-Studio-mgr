#include "Core/dspch.hpp"

#include "Domain/Crystal/PrototypeMatcher.hpp"

#include <algorithm>

namespace DefectStudio
{
	std::optional<SiteAssignment> PrototypeMatcher::MatchFormulaToPrototype(
		const std::vector<Element> &formula,
		const PrototypeDefinition &prototype)
	{
		if (formula.empty() || prototype.sites.empty())
			return std::nullopt;

		int siteTotal = 0;
		for (const SiteDefinition &site : prototype.sites)
			siteTotal += site.Multiplicity();

		int formulaTotal = 0;
		for (const Element &element : formula)
			formulaTotal += element.count;

		if (formulaTotal <= 0 || siteTotal <= 0)
			return std::nullopt;

		// A formula is a STOICHIOMETRIC RATIO, not an atom count: "Si" means every site is silicon,
		// not that the cell holds one atom. Matching count-for-count is why the wizard rejected the
		// single most common case ("Si" against diamond, 1 != 8) while accepting nothing useful.
		if (siteTotal % formulaTotal != 0)
			return std::nullopt;
		const int scale = siteTotal / formulaTotal;

		SiteAssignment result;
		result.species.resize(prototype.sites.size());

		// Sites are consumed in declaration order, elements in formula order - the cation-first
		// writing convention ("BN" puts B on the first site) the design doc settled on. A site is
		// never split between two elements.
		std::size_t siteIndex = 0;
		for (const Element &element : formula)
		{
			int remaining = element.count * scale;
			while (remaining > 0 && siteIndex < prototype.sites.size())
			{
				const int multiplicity = prototype.sites[siteIndex].Multiplicity();
				if (multiplicity > remaining)
					return std::nullopt;
				result.species[siteIndex] = element.symbol;
				remaining -= multiplicity;
				++siteIndex;
			}
			if (remaining > 0)
				return std::nullopt;
		}

		if (siteIndex != prototype.sites.size())
			return std::nullopt;

		// Every site has the same multiplicity, so nothing but formula order decided which element
		// landed where - exactly the case the editable mapping table exists to let the user correct.
		const bool allMultiplicitiesEqual = std::all_of(
			prototype.sites.begin(),
			prototype.sites.end(),
			[&](const SiteDefinition &site) { return site.Multiplicity() == prototype.sites.front().Multiplicity(); });
		result.isAmbiguous = formula.size() > 1 && allMultiplicitiesEqual;

		return result;
	}
} // namespace DefectStudio
