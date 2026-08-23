#include "Core/dspch.hpp"

#include "Domain/Crystal/StructureComparison.hpp"

#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace DefectStudio
{
	std::vector<AtomDisplacement> ComputeAtomDisplacements(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		float cutoffScale,
		float minimumDisplacementAngstrom)
	{
		std::vector<AtomDisplacement> displacements;
		std::vector<bool> referenceUsed(reference.atoms.size(), false);
		const float minimumDisplacementSquared = minimumDisplacementAngstrom * minimumDisplacementAngstrom;

		for (const AtomSite &comparisonAtom : comparison.atoms)
		{
			const float comparisonRadius = elementPropertiesTable.Get(comparisonAtom.species).covalentRadius;

			std::size_t bestIndex = reference.atoms.size();
			float bestDistanceSquared = std::numeric_limits<float>::max();

			for (std::size_t referenceIndex = 0; referenceIndex < reference.atoms.size(); ++referenceIndex)
			{
				if (referenceUsed[referenceIndex])
					continue;

				const AtomSite &referenceAtom = reference.atoms[referenceIndex];
				if (referenceAtom.species != comparisonAtom.species)
					continue;

				const float referenceRadius = elementPropertiesTable.Get(referenceAtom.species).covalentRadius;
				const float cutoff = cutoffScale * (comparisonRadius + referenceRadius);
				const glm::vec3 delta = referenceAtom.position - comparisonAtom.position;
				const float distanceSquared = glm::dot(delta, delta);
				if (distanceSquared > cutoff * cutoff)
					continue;
				if (distanceSquared < bestDistanceSquared)
				{
					bestDistanceSquared = distanceSquared;
					bestIndex = referenceIndex;
				}
			}

			if (bestIndex >= reference.atoms.size())
				continue; // No same-species reference atom in range - vacancy/substitution, no arrow.
			if (bestDistanceSquared < minimumDisplacementSquared)
				continue; // Matched but didn't meaningfully move.

			referenceUsed[bestIndex] = true;
			AtomDisplacement displacement;
			displacement.referenceAtomIndex = bestIndex;
			displacement.referencePosition = reference.atoms[bestIndex].position;
			displacement.comparisonPosition = comparisonAtom.position;
			displacement.magnitudeAngstrom = std::sqrt(bestDistanceSquared);
			displacements.push_back(displacement);
		}

		return displacements;
	}
} // namespace DefectStudio
