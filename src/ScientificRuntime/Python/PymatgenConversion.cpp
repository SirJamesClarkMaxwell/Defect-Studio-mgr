#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PymatgenConversion.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace DefectStudio
{
	CrystalStructure ConvertPymatgenStructureToCrystalStructure(
		const PymatgenStructureData &structureData,
		std::string name)
	{
		CrystalStructure structure;
		structure.name = !name.empty() ? std::move(name) : structureData.reducedFormula;
		for (std::size_t index = 0; index < structure.cell.vectors.size(); ++index)
			structure.cell.vectors[index] = structureData.lattice[static_cast<int>(index)];
		const glm::mat3 latticeMatrix = structure.cell.ToMatrix();

		structure.atoms.reserve(structureData.sites.size());
		for (std::size_t index = 0; index < structureData.sites.size(); ++index)
		{
			const PymatgenStructureSite &site = structureData.sites[index];
			AtomSite atom;
			atom.species = site.element;
			// Wrap into the primary [0,1) cell before storing - pymatgen/puntukas sites are not
			// guaranteed to already be wrapped (VASP relaxation commonly drifts boundary atoms
			// outside the nominal cell without re-wrapping), and an atom sitting a full cell-width
			// or more outside the primary repeat rendered as an orphaned "ghost" fragment with no
			// visible bond to anything, even though it's chemically bonded to whatever sits at its
			// wrapped position.
			glm::vec3 wrappedFractional = site.fractionalPosition;
			wrappedFractional -= glm::floor(wrappedFractional);
			atom.fractional = wrappedFractional;
			atom.position = latticeMatrix * wrappedFractional;
			atom.index = static_cast<int>(index);
			atom.hasSelectiveDynamics = site.selectiveDynamics.has_value();
			atom.selectiveDynamics = site.selectiveDynamics.value_or(std::array<bool, 3>{true, true, true});
			atom.charge = site.charge.value_or(0.0f);
			atom.magnetization = site.magmom.value_or(0.0f);
			atom.occupancy = site.occupancy;
			structure.atoms.push_back(std::move(atom));
		}
		return structure;
	}
} // namespace DefectStudio
