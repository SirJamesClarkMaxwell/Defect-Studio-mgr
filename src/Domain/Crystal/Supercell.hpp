#pragma once

#include <array>
#include <cstdlib>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	// Integer transformation matrix: newLattice.row[i] = sum_j rows[i][j] * unitCell.row[j] - same
	// row convention pymatgen's SupercellTransformation uses (rows={{2,1,0},{0,3,0},{0,0,1}} means
	// a" = 2a+b, b" = 3b, c" = c), so a UI power-user typing a matrix from a paper/pymatgen script
	// maps directly onto this field with no transpose surprises.
	struct SupercellMatrix
	{
		std::array<glm::ivec3, 3> rows = {
			glm::ivec3(1, 0, 0),
			glm::ivec3(0, 1, 0),
			glm::ivec3(0, 0, 1)};

		[[nodiscard]] static SupercellMatrix Diagonal(int a, int b, int c)
		{
			SupercellMatrix m;
			m.rows = { glm::ivec3(a, 0, 0), glm::ivec3(0, b, 0), glm::ivec3(0, 0, c) };
			return m;
		}

		// Also the resulting atom-count multiplier (BuildSupercell produces exactly
		// unitCell.atoms.size() * Determinant() atoms) - must be > 0 for BuildSupercell to accept it.
		[[nodiscard]] int Determinant() const
		{
			const glm::ivec3 &r0 = rows[0];
			const glm::ivec3 &r1 = rows[1];
			const glm::ivec3 &r2 = rows[2];
			return r0.x * (r1.y * r2.z - r1.z * r2.y)
				- r0.y * (r1.x * r2.z - r1.z * r2.x)
				+ r0.z * (r1.x * r2.y - r1.y * r2.x);
		}

		// Drives the UI: when true, the "Simple" N-x-M-x-K tab can show a faithful readout of this
		// matrix instead of falling back to "Matrix" mode.
		[[nodiscard]] bool IsDiagonal() const
		{
			return rows[0].y == 0 && rows[0].z == 0
				&& rows[1].x == 0 && rows[1].z == 0
				&& rows[2].x == 0 && rows[2].y == 0;
		}
	};

	// Pure, sync. Replicates unitCell.atoms across every lattice translation that lands inside the
	// transformed cell, dropping duplicate boundary images. Bonds are NOT carried over or
	// regenerated here (caller runs RegenerateAutoBonds on the result, same as every other
	// structure-building path in this repo - see OpenDefectJob's completion handler in
	// RendererRuntimeOpenCoordinator.cpp) - Supercell.cpp stays pure geometry, no
	// ElementPropertiesTable dependency.
	[[nodiscard]] Result<CrystalStructure> BuildSupercell(
		const CrystalStructure &unitCell,
		const SupercellMatrix &transform);
} // namespace DefectStudio
