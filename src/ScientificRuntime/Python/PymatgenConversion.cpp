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

		structure.atoms.reserve(structureData.sites.size());
		for (std::size_t index = 0; index < structureData.sites.size(); ++index)
		{
			const PymatgenStructureSite &site = structureData.sites[index];
			AtomSite atom;
			atom.species = site.element;
			atom.position = site.cartesianPosition;
			atom.fractional = site.fractionalPosition;
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
