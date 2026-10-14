#include "Core/dspch.hpp"

#include "Domain/Crystal/CellEditing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Domain/Crystal/LatticeBasisExpansion.hpp"

namespace DefectStudio
{
	namespace
	{
		bool Finite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		std::vector<std::size_t> SelectedAtoms(const CrystalStructure &structure, std::span<const std::size_t> indices)
		{
			std::vector<std::size_t> selected;
			for (const auto index : indices)
			{
				if (index >= structure.atoms.size() || !Finite(structure.atoms[index].position))
					return {};
				if (std::find(selected.begin(), selected.end(), index) == selected.end())
					selected.push_back(index);
			}
			return selected;
		}
	} // namespace

	CrystalStructure RepeatAtoms(const CrystalStructure &structure, std::span<const std::size_t> indices,
		const glm::vec3 &shift, int count, std::vector<std::size_t> &outCoincident)
	{
		outCoincident.clear();
		const auto selected = SelectedAtoms(structure, indices);
		if (count <= 0 || selected.empty() || !Finite(shift) ||
			structure.atoms.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
			selected.size() > (static_cast<std::size_t>(std::numeric_limits<int>::max()) - structure.atoms.size()) /
				static_cast<std::size_t>(count))
			return structure;
		CrystalStructure result = structure;
		result.atoms.reserve(result.atoms.size() + selected.size() * static_cast<std::size_t>(count));
		for (int k = 1; k <= count; ++k)
		{
			for (const auto index : selected)
			{
				AtomSite atom = structure.atoms[index];
				atom.position += static_cast<float>(k) * shift;
				atom.fractional = result.CartesianToFractional(atom.position);
				if (!Finite(atom.position) || !Finite(atom.fractional))
					return structure;
				atom.index = static_cast<int>(result.atoms.size());
				result.atoms.push_back(std::move(atom));
			}
		}
		outCoincident = FindCoincidentAtomIndices(result.atoms);
		return result;
	}

	CrystalStructure MirrorAtoms(const CrystalStructure &structure, std::span<const std::size_t> indices,
		const glm::vec3 &point, const glm::vec3 &normal, bool copy, std::vector<std::size_t> &outCoincident)
	{
		outCoincident.clear();
		const auto selected = SelectedAtoms(structure, indices);
		if (selected.empty() || !Finite(point) || !Finite(normal) ||
			(copy && (structure.atoms.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
				selected.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) - structure.atoms.size())))
			return structure;
		const float magnitude = std::max({std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)});
		const glm::vec3 direction = magnitude > 0.0f ? glm::normalize(normal / magnitude) : glm::vec3(0.0f);
		CrystalStructure result = structure;
		for (const auto index : selected)
		{
			AtomSite atom = structure.atoms[index];
			atom.position = magnitude == 0.0f ? 2.0f * point - atom.position :
				atom.position - 2.0f * glm::dot(atom.position - point, direction) * direction;
			atom.fractional = result.CartesianToFractional(atom.position);
			if (!Finite(atom.position) || !Finite(atom.fractional))
				return structure;
			if (copy)
			{
				atom.index = static_cast<int>(result.atoms.size());
				result.atoms.push_back(std::move(atom));
			}
			else
				result.atoms[index] = std::move(atom);
		}
		outCoincident = FindCoincidentAtomIndices(result.atoms);
		return result;
	}
} // namespace DefectStudio
