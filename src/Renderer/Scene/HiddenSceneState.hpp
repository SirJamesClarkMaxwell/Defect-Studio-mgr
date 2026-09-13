#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	// Ephemeral hide state (H / Alt+H, outliner eye) is not a domain concept, so an atom-edit
	// command that rebuilds RendererStructureData has to carry it across the rebuild itself.
	// Render indices are not stable when the atom count changes: deleting atom 3 turns atom 4 into
	// atom 3, so carrying hidden atoms by raw index hides the wrong atoms. Atoms are remapped
	// through the removal; bonds are carried by their (atom, atom) endpoints, because delete
	// regenerates the bond list and bond indices mean nothing afterwards.
	struct HiddenSceneState
	{
		std::vector<std::size_t> atomIndices;                            // ascending
		std::vector<std::pair<std::size_t, std::size_t>> bondEndpoints; // (min, max) atom index, ascending
		bool operator==(const HiddenSceneState &) const = default;
	};

	// Atoms and bonds with visible == false. Bond endpoints are normalized to (min, max).
	[[nodiscard]] HiddenSceneState CaptureHiddenSceneState(const RendererStructureData &structure);

	// `removedAtomIndices` are indices in the pre-removal structure, any order, duplicates allowed.
	// Hidden atoms that were removed are dropped; the rest shift down by the number of removed atoms
	// below them. Bonds touching a removed atom are dropped; the rest have both endpoints remapped.
	[[nodiscard]] HiddenSceneState RemapHiddenSceneStateAfterAtomRemoval(
		const HiddenSceneState &state, const std::vector<std::size_t> &removedAtomIndices);

	// Render bond indices in `structure` whose endpoints match an entry of `bondEndpoints`, ascending.
	// ponytail: periodic images of the same atom pair share endpoints, so hiding one image and
	// rebuilding hides all of them; carry Bond::periodicShift too if that ever matters.
	[[nodiscard]] std::vector<std::size_t> ResolveHiddenBondIndices(
		const RendererStructureData &structure,
		const std::vector<std::pair<std::size_t, std::size_t>> &bondEndpoints);
} // namespace DefectStudio
