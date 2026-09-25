#pragma once

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PymatgenBridge.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// Structure loader backed by puntukas (C:\Users\fzabi\puntukas_tools2, module `puntukas`) instead
	// of pymatgen. Subprocess-only by design (no embedded/nanobind fast path) - this is a low-frequency
	// user action (Open Defect), not a hot loop. Reuses PymatgenStructureData/Site: both loaders emit
	// the same JSON contract, so the payload shape is genuinely shared, not pymatgen-specific.
	//
	// Known gap: the bridge does not yet translate ase Selective Dynamics constraints, so
	// PymatgenStructureSite::selectiveDynamics remains nullopt via this bridge.
	class PuntukasBridge final
	{
	public:
		[[nodiscard]] Result<PymatgenStructureData> LoadStructure(const Path &filePath) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
