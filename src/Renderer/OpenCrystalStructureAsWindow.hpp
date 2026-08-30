#pragma once

#include <string>

#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class RendererLayer;
	struct AtomStyleTable;

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
		bool showGrid = true);
} // namespace DefectStudio
