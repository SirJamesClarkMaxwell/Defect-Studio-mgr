#include "Core/dspch.hpp"

#include "Renderer/StructureRendererDataBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/matrix.hpp>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::vector<RendererCellEdge> BuildCellEdges(const glm::mat3 &lattice)
		{
			const glm::vec3 a = lattice[0];
			const glm::vec3 b = lattice[1];
			const glm::vec3 c = lattice[2];
			const glm::vec3 p000 = glm::vec3(0.0f, 0.0f, 0.0f);
			const glm::vec3 p100 = a;
			const glm::vec3 p010 = b;
			const glm::vec3 p001 = c;
			const glm::vec3 p110 = a + b;
			const glm::vec3 p101 = a + c;
			const glm::vec3 p011 = b + c;
			const glm::vec3 p111 = a + b + c;

			return std::vector<RendererCellEdge>{
				{p000, p100}, {p000, p010}, {p000, p001},
				{p100, p110}, {p100, p101}, {p010, p110},
				{p010, p011}, {p001, p101}, {p001, p011},
				{p110, p111}, {p101, p111}, {p011, p111}};
		}

		[[nodiscard]] std::vector<RendererBondData> BuildRendererBonds(
			const CrystalStructure &structure,
			const std::vector<RendererAtomData> &atoms)
		{
			std::vector<RendererBondData> bonds;
			bonds.reserve(structure.bonds.size());
			for (const Bond &domainBond : structure.bonds)
			{
				if (!domainBond.visible ||
					domainBond.firstAtomIndex >= atoms.size() ||
					domainBond.secondAtomIndex >= atoms.size() ||
					domainBond.firstAtomIndex > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
					domainBond.secondAtomIndex > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
					continue;

				const RendererAtomData &atomA = atoms[domainBond.firstAtomIndex];
				const RendererAtomData &atomB = atoms[domainBond.secondAtomIndex];
				RendererBondData bond;
				bond.firstAtomIndex = static_cast<std::uint32_t>(domainBond.firstAtomIndex);
				bond.secondAtomIndex = static_cast<std::uint32_t>(domainBond.secondAtomIndex);
				bond.radius = std::max(0.05f, 0.22f * std::min(atomA.radius, atomB.radius));
				bond.gradient.start = atomA.color;
				bond.gradient.finish = atomB.color;
				bonds.push_back(bond);
			}
			return bonds;
		}
	}

	RendererStructureData BuildRendererStructureData(
		const CrystalStructure &structure,
		const Path &sourcePath,
		std::string name,
		const AtomStyleTable &atomStyleTable,
		std::string domainStructureId)
	{
		RendererStructureData data;
		data.domainStructureId = std::move(domainStructureId);
		data.name = !name.empty() ? std::move(name) : structure.name;
		data.sourcePath = sourcePath;
		data.lattice = structure.cell.ToMatrix();
		data.cellEdges = BuildCellEdges(data.lattice);

		data.atoms.reserve(structure.atoms.size());
		for (const AtomSite &site : structure.atoms)
		{
			RendererAtomData atom;
			atom.element = site.species;
			atom.radius = atomStyleTable.DisplayRadius(atom.element);
			atom.color = atomStyleTable.Color(atom.element);
			atom.cartesianPosition = site.position;
			atom.visible = true;
			data.atoms.push_back(std::move(atom));
		}
		data.bonds = BuildRendererBonds(structure, data.atoms);

		const float det = glm::determinant(data.lattice);
		if (std::abs(det) > 1e-6f)
		{
			const glm::mat3 invT = glm::transpose(glm::inverse(data.lattice));
			data.reciprocalLattice = invT;
		}

		return data;
	}
} // namespace DefectStudio
