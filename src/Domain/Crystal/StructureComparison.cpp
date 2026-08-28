#include "Core/dspch.hpp"

#include "Domain/Crystal/StructureComparison.hpp"

#include "Domain/Crystal/PeriodicGeometry.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace DefectStudio
{
	namespace
	{
		// Same order of magnitude as a converged-relaxation displacement, but comfortably above
		// float noise - two structures meant to describe "the same cell" (POSCAR/CONTCAR pair, or
		// two defect variants built from the same supercell) shouldn't disagree on lattice vectors
		// by more than this.
		constexpr float kLatticeMatchToleranceAngstrom = 0.05f;

		[[nodiscard]] bool LatticesMatch(const glm::mat3 &a, const glm::mat3 &b)
		{
			for (int column = 0; column < 3; ++column)
				if (glm::length(a[column] - b[column]) > kLatticeMatchToleranceAngstrom)
					return false;
			return true;
		}

		[[nodiscard]] std::string MakeLatticeMismatchWarning()
		{
			return "Reference and comparison structures have different lattice vectors (beyond " +
				std::to_string(kLatticeMatchToleranceAngstrom) +
				" Angstrom tolerance) - periodic wrapping disabled, raw (non-periodic) distances used, "
				"so matches near a cell boundary may be missing or reported as vacancy/interstitial.";
		}

		// Minimum-image wrapping only means something when both atoms live in the SAME cell. With
		// mismatched lattices (e.g. two files from a vacuum-thickness convergence test, different c
		// vector) wrapping the comparison atom by the REFERENCE cell's vector is meaningless - it can
		// shift it by nearly a whole cell length, producing a huge "displacement" that's actually just
		// wrap-math applied to the wrong cell (2026-08-28 feedback: arrows shooting far outside the
		// structure). Falls back to the plain Cartesian delta when lattices don't match; the existing
		// cutoff then naturally drops pairs that are genuinely far apart instead of a bogus near-cutoff
		// wrapped match.
		[[nodiscard]] glm::vec3 DisplacementDelta(
			bool latticeMismatch, const glm::mat3 &latticeMatrix, const glm::vec3 &a, const glm::vec3 &b)
		{
			if (latticeMismatch)
				return a - b;
			return MinimumImageCartesianDelta(latticeMatrix, a, b);
		}
	} // namespace

	Result<DisplacementCostMatrix> BuildDisplacementCostMatrix(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		float cutoffScale,
		bool restrictToSameSpecies)
	{
		const glm::mat3 latticeMatrix = reference.cell.ToMatrix();

		DisplacementCostMatrix matrix;
		matrix.latticeMismatch = !LatticesMatch(latticeMatrix, comparison.cell.ToMatrix());
		matrix.comparisonCount = comparison.atoms.size();
		matrix.referenceCount = reference.atoms.size();
		matrix.costs.assign(matrix.comparisonCount * matrix.referenceCount, kUnmatchedDisplacementCost);

		for (std::size_t comparisonIndex = 0; comparisonIndex < matrix.comparisonCount; ++comparisonIndex)
		{
			const AtomSite &comparisonAtom = comparison.atoms[comparisonIndex];
			const float comparisonRadius = elementPropertiesTable.Get(comparisonAtom.species).covalentRadius;

			for (std::size_t referenceIndex = 0; referenceIndex < matrix.referenceCount; ++referenceIndex)
			{
				const AtomSite &referenceAtom = reference.atoms[referenceIndex];
				if (restrictToSameSpecies && referenceAtom.species != comparisonAtom.species)
					continue;

				const float referenceRadius = elementPropertiesTable.Get(referenceAtom.species).covalentRadius;
				const float cutoff = cutoffScale * (comparisonRadius + referenceRadius);
				const glm::vec3 delta = DisplacementDelta(
					matrix.latticeMismatch, latticeMatrix, referenceAtom.position, comparisonAtom.position);
				const float distance = glm::length(delta);
				if (distance <= cutoff)
					matrix.costs[comparisonIndex * matrix.referenceCount + referenceIndex] = distance;
			}
		}

		return matrix;
	}

	StructureComparisonResult BuildComparisonResultFromAssignment(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const DisplacementCostMatrix &costMatrix,
		std::span<const int> comparisonToReferenceAssignment)
	{
		StructureComparisonResult result;
		if (costMatrix.latticeMismatch)
			result.latticeMismatchWarning = MakeLatticeMismatchWarning();
		std::vector<bool> referenceMatched(costMatrix.referenceCount, false);
		const glm::mat3 latticeMatrix = reference.cell.ToMatrix();

		const std::size_t pairCount = std::min(comparisonToReferenceAssignment.size(), costMatrix.comparisonCount);
		for (std::size_t comparisonIndex = 0; comparisonIndex < pairCount; ++comparisonIndex)
		{
			const int referenceIndex = comparisonToReferenceAssignment[comparisonIndex];
			const bool isAssigned = referenceIndex >= 0 &&
				static_cast<std::size_t>(referenceIndex) < costMatrix.referenceCount;
			const float cost = isAssigned
				? costMatrix.At(comparisonIndex, static_cast<std::size_t>(referenceIndex))
				: kUnmatchedDisplacementCost;

			if (isAssigned && cost < kUnmatchedDisplacementCost)
			{
				referenceMatched[static_cast<std::size_t>(referenceIndex)] = true;

				AtomDisplacement displacement;
				displacement.referenceAtomIndex = static_cast<std::size_t>(referenceIndex);
				displacement.comparisonAtomIndex = comparisonIndex;
				displacement.referencePosition = reference.atoms[static_cast<std::size_t>(referenceIndex)].position;
				displacement.comparisonPosition = comparison.atoms[comparisonIndex].position;
				displacement.comparisonPositionWrapped = displacement.referencePosition +
					DisplacementDelta(
						costMatrix.latticeMismatch, latticeMatrix, displacement.comparisonPosition,
						displacement.referencePosition);
				displacement.magnitudeAngstrom = cost;
				result.matches.push_back(displacement);
			}
			else
			{
				const AtomSite &comparisonAtom = comparison.atoms[comparisonIndex];
				result.unmatchedComparisonAtoms.push_back(
					UnmatchedComparisonAtom{comparisonAtom.position, comparisonAtom.species});
			}
		}

		for (std::size_t referenceIndex = 0; referenceIndex < costMatrix.referenceCount; ++referenceIndex)
			if (!referenceMatched[referenceIndex])
				result.unmatchedReferenceAtomIndices.push_back(referenceIndex);

		return result;
	}
} // namespace DefectStudio
