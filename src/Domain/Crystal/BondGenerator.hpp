#pragma once

#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	void RegenerateAutoBonds(
		CrystalStructure &structure,
		const ElementPropertiesTable &elementPropertiesTable);

	// Key format for BondGenerationSettings::perPairCutoffOverride ("C-N", alphabetically sorted so
	// insertion order doesn't matter) - exported so callers building/editing the override map (the
	// Bond Settings panel) produce keys BondGenerator's own lookup (CutoffScaleForPair) actually
	// matches, rather than duplicating the sort-and-join logic and risking drift.
	[[nodiscard]] std::string BondPairKey(std::string first, std::string second);
} // namespace DefectStudio
