#pragma once

#include <string>
#include <vector>

#include "Domain/Crystal/CrystalPrimitives.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	class RendererLayer;

	// Scratch state of the editor that is NOT part of BondGenerationSettings: the half-typed
	// add-a-pair row. One instance per caller, since two callers can have one open at once.
	struct BondSettingsEditorState
	{
		char newPairFirst[8] = "";
		char newPairSecond[8] = "";
		float newPairScale = 1.18f;
	};

	// The bond cutoff editor: global scale, per-pair overrides in angstrom, and the pairs actually
	// present in the structure. Draws into the current ImGui window. `settings` is mutated live,
	// every frame of a drag; the return value is true only on a FINISHED edit (drag released, pair
	// added or removed), which is the caller's cue to rebuild bonds.
	//
	// A free function because two callers need exactly this block and neither owns the other: the
	// Bond Settings panel, for a structure registered in the project, and the New Structure wizard,
	// for a draft that has no domain record for the panel's command to act on.
	bool DrawBondSettingsEditor(
		BondGenerationSettings &settings,
		const std::vector<std::string> &presentSpecies,
		const ElementPropertiesTable &elementPropertiesTable,
		RendererLayer &layer,
		BondSettingsEditorState &state);
} // namespace DefectStudio
