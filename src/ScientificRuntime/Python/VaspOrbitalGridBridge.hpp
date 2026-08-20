#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct VaspOrbitalGridData
	{
		glm::ivec3 dimensions = glm::ivec3(0);
		glm::mat3 cell = glm::mat3(1.0f); // direct real-space cell (Angstrom), cell[i] = lattice vector i
		// Real part of the real-space Kohn-Sham wavefunction, C-order (x slowest, z fastest).
		// Signed, not a probability density - keeps +/- lobe sign for isosurface coloring.
		std::vector<float> values;
		float energy = 0.0f;
		float occupation = 0.0f;
	};

	// Extracts one orbital's real-space wavefunction grid from WAVECAR via puntukas
	// (Wavecar.phi -> PWWavefunction.real_space_wfs()). The grid is too large for the usual
	// JSON-line contract - the Python script writes it to a temp raw float32 file and reports its
	// path/shape in the JSON line; this bridge reads that file and deletes it before returning.
	class VaspOrbitalGridBridge final
	{
	public:
		[[nodiscard]] Result<VaspOrbitalGridData> LoadOrbitalGrid(
			const Path &calculationDirectory,
			int spinChannel,
			int kpointIndex,
			int bandIndex) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
