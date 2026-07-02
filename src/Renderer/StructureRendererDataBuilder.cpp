#include "Core/dspch.hpp"

#include "Renderer/StructureRendererDataBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

namespace DefectStudio
{
	namespace
	{
		struct RendererGridCellKey
		{
			int x = 0;
			int y = 0;
			int z = 0;

			[[nodiscard]] bool operator==(const RendererGridCellKey &other) const
			{
				return x == other.x && y == other.y && z == other.z;
			}
		};

		struct RendererGridCellKeyHasher
		{
			[[nodiscard]] std::size_t operator()(const RendererGridCellKey &key) const noexcept
			{
				std::size_t seed = static_cast<std::size_t>(key.x) * 73856093u;
				seed ^= static_cast<std::size_t>(key.y) * 19349663u;
				seed ^= static_cast<std::size_t>(key.z) * 83492791u;
				return seed;
			}
		};

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

		[[nodiscard]] RendererGridCellKey CellForPosition(const glm::vec3 &position, float cellSize)
		{
			return RendererGridCellKey{
				static_cast<int>(std::floor(position.x / cellSize)),
				static_cast<int>(std::floor(position.y / cellSize)),
				static_cast<int>(std::floor(position.z / cellSize))};
		}

		[[nodiscard]] std::vector<RendererBondData> BuildBonds(
			const std::vector<RendererAtomData> &atoms,
			const ElementPropertiesTable &elementPropertiesTable)
		{
			constexpr float kCellSize = 4.72f;
			constexpr float kCutoffScale = 1.18f;
			std::unordered_map<RendererGridCellKey, std::vector<std::uint32_t>, RendererGridCellKeyHasher> buckets;
			buckets.reserve(atoms.size() * 2);

			for (std::uint32_t index = 0; index < atoms.size(); ++index)
			{
				const RendererGridCellKey cell = CellForPosition(atoms[index].cartesianPosition, kCellSize);
				buckets[cell].push_back(index);
			}

			std::vector<RendererBondData> bonds;
			bonds.reserve(atoms.size());
			for (std::uint32_t first = 0; first < atoms.size(); ++first)
			{
				const RendererAtomData &atomA = atoms[first];
				const RendererGridCellKey baseCell = CellForPosition(atomA.cartesianPosition, kCellSize);
				for (int dx = -1; dx <= 1; ++dx)
				{
					for (int dy = -1; dy <= 1; ++dy)
					{
						for (int dz = -1; dz <= 1; ++dz)
						{
							const RendererGridCellKey neighbor{
								baseCell.x + dx,
								baseCell.y + dy,
								baseCell.z + dz};
							auto found = buckets.find(neighbor);
							if (found == buckets.end())
								continue;

							for (std::uint32_t second : found->second)
							{
								if (second <= first)
									continue;

								const RendererAtomData &atomB = atoms[second];
								const float cutoff = kCutoffScale * (
									elementPropertiesTable.Get(atomA.element).covalentRadius +
									elementPropertiesTable.Get(atomB.element).covalentRadius);
								const float cutoffSquared = cutoff * cutoff;
								const glm::vec3 delta = atomA.cartesianPosition - atomB.cartesianPosition;
								const float distanceSquared = glm::dot(delta, delta);
								if (distanceSquared > cutoffSquared || distanceSquared < 0.00001f)
									continue;

								RendererBondData bond;
								bond.firstAtomIndex = first;
								bond.secondAtomIndex = second;
								bond.radius = std::max(0.05f, 0.22f * std::min(atomA.radius, atomB.radius));
								bond.gradient.start = atomA.color;
								bond.gradient.finish = atomB.color;
								bonds.push_back(bond);
							}
						}
					}
				}
			}
			return bonds;
		}
	}

	RendererStructureData BuildRendererStructureData(
		const CrystalStructure &structure,
		const Path &sourcePath,
		std::string name,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable,
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
		data.bonds = BuildBonds(data.atoms, elementPropertiesTable);

		const float det = glm::determinant(data.lattice);
		if (std::abs(det) > 1e-6f)
		{
			const glm::mat3 invT = glm::transpose(glm::inverse(data.lattice));
			data.reciprocalLattice = invT;
		}

		return data;
	}
} // namespace DefectStudio
