#pragma once

#include <string>

#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	class RendererLayer;
	class AtomStyleTable;

	// Shows a structure in a renderer window WITHOUT registering it in the domain - the counterpart
	// to OpenCrystalStructureAsWindow, for structures the user has not accepted yet.
	//
	// The New Structure wizard rebuilds its CrystalStructure on every field change. Opening a window
	// per rebuild is what produced the pile of "New Structure", "New Structure (basis)" tabs; adding
	// each one to StructureRegistry instead would fill the domain registry and the undo history with
	// structures nobody asked for, and Ctrl+Z after Create would have to walk back through every
	// slider micro-edit.
	//
	// Pass the id returned by the previous call to refresh that window in place; pass an empty id
	// (or the id of a window the user has since closed) to open a fresh one. Returns the id to keep.
	// The window carries an empty domainStructureId, so ResolveAtomEditTarget deliberately refuses
	// it - a preview is not an edit target.
	[[nodiscard]] std::string ShowCrystalStructurePreview(
		const std::string &existingWindowId,
		const CrystalStructure &structure,
		const std::string &displayName,
		RendererLayer &rendererLayer,
		const ElementPropertiesTable &elementPropertiesTable,
		const AtomStyleTable &atomStyleTable,
		bool showCellBox = true,
		bool showGrid = true);

	// Closes a preview window if it is still open. No-op on an empty or unknown id, so a caller can
	// hand over whatever it last got back without checking first.
	void CloseCrystalStructurePreview(const std::string &windowId, RendererLayer &rendererLayer);
} // namespace DefectStudio
