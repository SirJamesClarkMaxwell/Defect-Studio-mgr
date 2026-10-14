#include "Core/dspch.hpp"

#include "Renderer/Scene/ModalSnapTargets.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

#include "Renderer/RendererTypes.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		bool Finite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		std::string AtomLabel(const RendererStructureData &structure, int index)
		{
			if (index < 0 || static_cast<std::size_t>(index) >= structure.atoms.size())
				return "?";
			return structure.atoms[index].element + std::to_string(index + 1);
		}
	}

	std::vector<SnapTarget> CollectSnapTargets(const RendererStructureData &structure,
		std::span<const std::size_t> movedAtoms, bool includeCell)
	{
		const std::unordered_set<std::size_t> moved(movedAtoms.begin(), movedAtoms.end());
		std::vector<SnapTarget> targets;
		for (std::size_t index = 0; index < structure.atoms.size(); ++index)
		{
			const RendererAtomData &atom = structure.atoms[index];
			if (!moved.contains(index) && atom.visible && Finite(atom.cartesianPosition))
				targets.push_back({SnapTargetKind::Atom, atom.cartesianPosition, static_cast<int>(index)});
		}
		for (const RendererBondData &bond : structure.bonds)
		{
			const std::size_t first = bond.firstAtomIndex, second = bond.secondAtomIndex;
			if (!bond.visible || first >= structure.atoms.size() || second >= structure.atoms.size() ||
				moved.contains(first) || moved.contains(second))
				continue;
			const RendererAtomData &a = structure.atoms[first], &b = structure.atoms[second];
			// Use the displayed bond, including its existing periodic offset, without adding atom images.
			const glm::vec3 midpoint = (a.cartesianPosition + b.cartesianPosition + bond.secondAtomPeriodicOffset) * 0.5f;
			if (a.visible && b.visible && Finite(midpoint))
				targets.push_back({SnapTargetKind::BondMidpoint, midpoint, static_cast<int>(first), glm::ivec3(0),
					static_cast<int>(second)});
		}
		const glm::mat3 &cell = structure.lattice;
		if (!includeCell || (!structure.periodic && structure.cellEdges.empty()) ||
			!Finite(cell[0]) || !Finite(cell[1]) || !Finite(cell[2]) || std::abs(glm::determinant(cell)) <= 1.0e-6f)
			return targets;

		for (int x = 0; x < 2; ++x)
			for (int y = 0; y < 2; ++y)
				for (int z = 0; z < 2; ++z)
					targets.push_back({SnapTargetKind::CellCorner, cell * glm::vec3(x, y, z)});
		for (int axis = 0; axis < 3; ++axis)
		{
			for (int first = 0; first < 2; ++first)
				for (int second = 0; second < 2; ++second)
				{
					glm::vec3 fraction(0.5f);
					fraction[(axis + 1) % 3] = static_cast<float>(first);
					fraction[(axis + 2) % 3] = static_cast<float>(second);
					targets.push_back({SnapTargetKind::CellEdgeMidpoint, cell * fraction});
				}
			for (int side = 0; side < 2; ++side)
			{
				glm::vec3 fraction(0.5f);
				fraction[axis] = static_cast<float>(side);
				targets.push_back({SnapTargetKind::CellFaceCentre, cell * fraction});
			}
		}
		return targets;
	}

	std::optional<SnapTarget> PickSnapTarget(std::span<const SnapTarget> targets,
		const ModalTransformView &view, const glm::vec2 &mouse, float radiusPixels)
	{
		if (!std::isfinite(radiusPixels) || radiusPixels < 0.0f ||
			view.viewportSize.x <= 0.0f || view.viewportSize.y <= 0.0f)
			return std::nullopt;
		std::optional<SnapTarget> nearest;
		float bestDistance = radiusPixels * radiusPixels;
		float bestDepth = std::numeric_limits<float>::max();
		const glm::mat4 viewProjection = view.projection * view.view;
		for (const SnapTarget &target : targets)
		{
			const glm::vec4 clip = viewProjection * glm::vec4(target.position, 1.0f);
			if (!Finite(glm::vec3(clip)) || !std::isfinite(clip.w) || clip.w <= 0.0001f ||
				clip.z < -clip.w || clip.z > clip.w)
				continue;
			const auto projected = SelectionHitTest::ProjectToScreen(viewProjection, view.viewportSize, target.position);
			if (!projected)
				continue;
			const glm::vec2 offset = view.viewportOrigin + *projected - mouse;
			const float distance = glm::dot(offset, offset);
			const float depth = clip.z / clip.w;
			if (distance < bestDistance || (distance == bestDistance && depth < bestDepth))
			{
				nearest = target;
				bestDistance = distance;
				bestDepth = depth;
			}
		}
		return nearest;
	}

	glm::vec3 TranslationOntoTarget(const glm::vec3 &anchor, const glm::vec3 &target,
		const TransformConstraint &constraint, const TransformBases &bases)
	{
		return ConstrainTranslation(target - anchor, constraint, bases);
	}

	std::optional<glm::mat3> MakePickedBasis(const glm::vec3 &a, const glm::vec3 &b)
	{
		const glm::vec3 direction = b - a;
		const float length = glm::length(direction);
		if (!Finite(direction) || !std::isfinite(length) || length <= 1.0e-6f)
			return std::nullopt;
		const glm::vec3 x = direction / length;
		const glm::vec3 helper = std::abs(x.z) < 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
		const glm::vec3 y = glm::normalize(glm::cross(helper, x));
		return glm::mat3(x, y, glm::cross(x, y));
	}

	std::string FormatSnapTargetHeader(const SnapTarget &target, const RendererStructureData &structure,
		const glm::vec3 &translatedAnchor)
	{
		std::ostringstream output;
		output << "  → ";
		switch (target.kind)
		{
			case SnapTargetKind::Atom: output << "atom " << AtomLabel(structure, target.atomIndex); break;
			case SnapTargetKind::BondMidpoint:
				output << "środek wiązania " << AtomLabel(structure, target.atomIndex) << "–"
					<< AtomLabel(structure, target.secondAtomIndex); break;
			case SnapTargetKind::CellCorner: output << "narożnik komórki"; break;
			case SnapTargetKind::CellEdgeMidpoint: output << "środek krawędzi"; break;
			case SnapTargetKind::CellFaceCentre: output << "środek ściany"; break;
		}
		const float residual = glm::distance(translatedAnchor, target.position);
		if (residual > 1.0e-4f)
			output << " (odległość od celu " << std::fixed << std::setprecision(2) << residual << " Å)";
		return output.str();
	}
} // namespace DefectStudio
