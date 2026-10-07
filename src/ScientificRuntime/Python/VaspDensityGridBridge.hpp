#pragma once

#include "Core/Utils/Path.hpp"
#include "Domain/Electronic/DensityGrid.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// Reads one component of a CHGCAR via puntukas (scripts/python/examples/vasp_density_grid_load.py),
	// optionally minus the same component of a reference CHGCAR on the same grid. Same temp-file
	// contract as VaspOrbitalGridBridge: the grid comes back through a raw float32 file this bridge
	// reads and deletes. A 180^3 spin-polarised CHGCAR takes ~12 s to parse - call it from a job.
	class VaspDensityGridBridge final
	{
	public:
		[[nodiscard]] Result<DensityGrid> LoadDensityGrid(
			const Path &chgcarPath, DensityComponent component, const Path &referencePath = {}) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
