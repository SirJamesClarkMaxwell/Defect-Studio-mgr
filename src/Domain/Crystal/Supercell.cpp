#include "Core/dspch.hpp"

#include "Domain/Crystal/Supercell.hpp"

#include <cmath>
#include <string>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError MakeInvalidSupercellMatrixError(int determinant)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"Supercell transform matrix must have a positive determinant.",
				"Determinant was " + std::to_string(determinant) + ".",
				"Pick a transform whose three row vectors form a right-handed, non-degenerate basis "
				"(e.g. swap two rows if the determinant is negative, or fix a zero row/column).",
				"Supercell",
				"domain.supercell.invalid_determinant"};
		}
	} // namespace

	Result<CrystalStructure> BuildSupercell(
		const CrystalStructure &unitCell,
		const SupercellMatrix &transform)
	{
		const int determinant = transform.Determinant();
		if (determinant <= 0)
			return MakeInvalidSupercellMatrixError(determinant);

		CrystalStructure result;
		result.name = unitCell.name + " supercell";
		result.isPeriodic = unitCell.isPeriodic;

		const std::array<glm::vec3, 3> &oldVectors = unitCell.cell.vectors;
		std::array<glm::vec3, 3> newVectors;
		for (int row = 0; row < 3; ++row)
		{
			newVectors[row] =
				static_cast<float>(transform.rows[row].x) * oldVectors[0] +
				static_cast<float>(transform.rows[row].y) * oldVectors[1] +
				static_cast<float>(transform.rows[row].z) * oldVectors[2];
		}
		result.cell.vectors = newVectors;

		// Conservative search bound: any lattice point that can end up inside the new cell has
		// integer coefficients (against the OLD basis) bounded by the sum of absolute entries of
		// the transform matrix. Looser than necessary but always correct and cheap for realistic
		// supercell sizes.
		// ponytail: O((2R+1)^3 * atomCount) brute-force search; R grows with how sheared the
		// transform is, not just its determinant. Tighten via the new lattice's inverse (bounding
		// box of the new cell's 8 corners in old-basis coordinates) if a pathologically sheared
		// matrix ever makes this slow in practice.
		int searchRadius = 1;
		for (const glm::ivec3 &row : transform.rows)
			searchRadius += std::abs(row.x) + std::abs(row.y) + std::abs(row.z);

		constexpr float kFractionalEpsilon = 1e-5f;
		result.atoms.reserve(unitCell.atoms.size() * static_cast<std::size_t>(determinant));

		int nextIndex = 0;
		for (int i = -searchRadius; i <= searchRadius; ++i)
		{
			for (int j = -searchRadius; j <= searchRadius; ++j)
			{
				for (int k = -searchRadius; k <= searchRadius; ++k)
				{
					const glm::vec3 shift =
						static_cast<float>(i) * oldVectors[0] +
						static_cast<float>(j) * oldVectors[1] +
						static_cast<float>(k) * oldVectors[2];

					for (const AtomSite &atom : unitCell.atoms)
					{
						const glm::vec3 cartesian = atom.position + shift;
						const glm::vec3 fractional = result.CartesianToFractional(cartesian);

						const bool inNewCell =
							fractional.x >= -kFractionalEpsilon && fractional.x < 1.0f - kFractionalEpsilon &&
							fractional.y >= -kFractionalEpsilon && fractional.y < 1.0f - kFractionalEpsilon &&
							fractional.z >= -kFractionalEpsilon && fractional.z < 1.0f - kFractionalEpsilon;
						if (!inNewCell)
							continue;

						AtomSite newAtom = atom;
						newAtom.position = cartesian;
						newAtom.fractional = fractional;
						newAtom.index = nextIndex++;
						result.atoms.push_back(std::move(newAtom));
					}
				}
			}
		}

		return result;
	}
} // namespace DefectStudio
