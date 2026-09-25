#pragma once

#include <string>

#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Domain/DomainIds.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class RendererLayer;
	class AtomStyleTable;

	// Registers `structure` in the active project's StructureRegistry and opens it as a new
	// renderer window - the same three-step sequence RendererRuntimeOpenCoordinator::
	// onJobCompleted runs for file-imported structures (RegenerateAutoBonds -> Structures().Add ->
	// BuildRendererStructureData -> AddWindow), factored out so in-app-BUILT structures (New
	// Structure wizard, supercell generation, materials collection "Open") can reuse it without a
	// file-based OpenDefectJob. Synchronous - only wrap the CALLER's own structure-building step in
	// a Job if it needs one (e.g. SupercellBridge's surface-orientation suggestion); this function
	// itself never touches Python.
	void OpenCrystalStructureAsWindow(
		CrystalStructure structure,
		const std::string &displayName,
		DomainLayer &domainLayer,
		RendererLayer &rendererLayer,
		const ElementPropertiesTable &elementPropertiesTable,
		const AtomStyleTable &atomStyleTable,
		bool showCellBox = true,
		bool showGrid = true,
		bool exportPotcar = false);

	// Opens a registered structure window by StructureId lookup. The structure must already be
	// registered in DomainLayer - this function does not register, only opens a renderer window.
	// Used when opening from project tree (already-registered structures) or other UI that knows
	// the StructureId.
	// Opens a renderer window with no structure behind it - an empty scene for the scene objects
	// that don't need atoms (arrows, labels, planes, orbitals). Nothing is registered in the
	// domain, so the window's structureId stays unset and it owns no StructureRecord.
	void OpenEmptyRendererWindow(RendererLayer &rendererLayer, const std::string &title);

	void OpenRegisteredStructureAsWindow(
		StructureId id,
		DomainLayer &domainLayer,
		RendererLayer &rendererLayer,
		const AtomStyleTable &atomStyleTable);
} // namespace DefectStudio
