#pragma once

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PymatgenBridge.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// Structure loader backed by puntukas (C:\Users\fzabi\punktukas-tools, module `puntukas`) instead
	// of pymatgen. Subprocess-only by design (no embedded/nanobind fast path) - this is a low-frequency
	// user action (Open Defect), not a hot loop. Reuses PymatgenStructureData/Site: both loaders emit
	// the same JSON contract, so the payload shape is genuinely shared, not pymatgen-specific.
	//
	// Known gap: puntukas's own Poscar parser drops per-atom Selective Dynamics flags (parses only the
	// block header, not the T/T/T columns), so PymatgenStructureSite::selectiveDynamics is always
	// nullopt via this bridge. That's a limitation in puntukas itself, not this bridge.
	class PuntukasBridge final
	{
	public:
		[[nodiscard]] Result<PymatgenStructureData> LoadStructure(const Path &filePath) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
