#include "Renderer/Scene/SceneHideVolume.hpp"

#include <cmath>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kSingularDeterminantEpsilon = 1.0e-6f;

		struct PreparedHideVolume
		{
			glm::vec3 center = glm::vec3(0.0f);
			glm::mat3 worldToFrame = glm::mat3(1.0f);
			bool valid = true;
		};

		PreparedHideVolume PrepareHideVolume(
			const SceneHideVolume &volume, const glm::mat3 &lattice, const RendererStructureData &structure)
		{
			PreparedHideVolume prepared;
			prepared.center = ResolveHideVolumeCenter(volume, structure).value_or(volume.center);

			if (volume.frame == HideVolumeFrame::Fractional)
			{
				if (std::abs(glm::determinant(lattice)) <= kSingularDeterminantEpsilon)
				{
					prepared.valid = false;
					return prepared;
				}
				prepared.worldToFrame = glm::inverse(lattice);
			}

			return prepared;
		}

		bool PointInPreparedHideVolume(
			const SceneHideVolume &volume, glm::vec3 worldPoint, const PreparedHideVolume &prepared)
		{
			if (!prepared.valid)
				return false;

			const glm::vec3 framePoint = prepared.worldToFrame * worldPoint;
			const glm::vec3 localPoint = glm::transpose(volume.orientation) * (framePoint - prepared.center);
			bool inside = false;
			switch (volume.kind)
			{
			case HideVolumeKind::Sphere:
				inside = glm::dot(localPoint, localPoint) <= volume.halfExtents.x * volume.halfExtents.x;
				break;
			case HideVolumeKind::Box:
				inside = std::abs(localPoint.x) <= volume.halfExtents.x &&
					std::abs(localPoint.y) <= volume.halfExtents.y &&
					std::abs(localPoint.z) <= volume.halfExtents.z;
				break;
			case HideVolumeKind::Cylinder:
				inside = std::abs(localPoint.z) <= volume.halfExtents.z &&
					localPoint.x * localPoint.x + localPoint.y * localPoint.y <=
						volume.halfExtents.x * volume.halfExtents.x;
				break;
			}

			return inside != volume.invert;
		}
	} // namespace

	std::optional<glm::vec3> ResolveHideVolumeCenter(
		const SceneHideVolume &volume, const RendererStructureData &structure)
	{
		if (volume.frame != HideVolumeFrame::Anchored || volume.anchorAtoms.empty())
			return std::nullopt;

		glm::vec3 center(0.0f);
		std::size_t resolvedCount = 0;
		for (const std::size_t atomIndex : volume.anchorAtoms)
		{
			if (atomIndex < structure.atoms.size())
			{
				center += structure.atoms[atomIndex].cartesianPosition;
				++resolvedCount;
			}
		}

		if (resolvedCount == 0)
			return std::nullopt;
		return center / static_cast<float>(resolvedCount);
	}

	bool PointInHideVolume(
		const SceneHideVolume &volume,
		glm::vec3 worldPoint,
		const glm::mat3 &lattice,
		const RendererStructureData &structure)
	{
		return PointInPreparedHideVolume(volume, worldPoint, PrepareHideVolume(volume, lattice, structure));
	}

	std::vector<std::size_t> AtomsCoveredByHideVolumes(
		const RendererStructureData &structure, std::span<const SceneHideVolume> volumes)
	{
		std::vector<bool> covered(structure.atoms.size(), false);
		for (const SceneHideVolume &volume : volumes)
		{
			if (!volume.visible)
				continue;

			const PreparedHideVolume prepared = PrepareHideVolume(volume, structure.lattice, structure);
			for (std::size_t atomIndex = 0; atomIndex < structure.atoms.size(); ++atomIndex)
			{
				if (!covered[atomIndex])
					covered[atomIndex] = PointInPreparedHideVolume(
						volume, structure.atoms[atomIndex].cartesianPosition, prepared);
			}
		}

		std::vector<std::size_t> result;
		for (std::size_t atomIndex = 0; atomIndex < covered.size(); ++atomIndex)
		{
			if (covered[atomIndex])
				result.push_back(atomIndex);
		}
		return result;
	}

	void ApplyHideVolumeMaskToWindowState(RendererWindowState &windowState)
	{
		const std::vector<std::size_t> visibleCoveredAtoms =
			AtomsCoveredByHideVolumes(windowState.structure, windowState.sceneHideVolumes);

		std::vector<SceneHideVolume> renderableVolumes;
		renderableVolumes.reserve(windowState.sceneHideVolumes.size());
		for (const SceneHideVolume &volume : windowState.sceneHideVolumes)
		{
			if (!volume.renderable)
				continue;

			SceneHideVolume renderableVolume = volume;
			renderableVolume.visible = true;
			renderableVolumes.push_back(std::move(renderableVolume));
		}
		const std::vector<std::size_t> renderableCoveredAtoms =
			AtomsCoveredByHideVolumes(windowState.structure, renderableVolumes);

		std::vector<bool> visibleCovered(windowState.structure.atoms.size(), false);
		for (const std::size_t atomIndex : visibleCoveredAtoms)
			if (atomIndex < visibleCovered.size())
				visibleCovered[atomIndex] = true;

		std::vector<bool> renderableCovered(windowState.structure.atoms.size(), false);
		for (const std::size_t atomIndex : renderableCoveredAtoms)
			if (atomIndex < renderableCovered.size())
				renderableCovered[atomIndex] = true;

		for (std::size_t atomIndex = 0; atomIndex < windowState.structure.atoms.size(); ++atomIndex)
		{
			if (visibleCovered[atomIndex])
				windowState.structure.atoms[atomIndex].visible = false;
			if (renderableCovered[atomIndex])
				windowState.structure.atoms[atomIndex].renderable = false;
		}

		for (RendererBondData &bond : windowState.structure.bonds)
		{
			const std::size_t firstAtomIndex = bond.firstAtomIndex;
			const std::size_t secondAtomIndex = bond.secondAtomIndex;
			const bool visibleEndpointCovered =
				(firstAtomIndex < visibleCovered.size() && visibleCovered[firstAtomIndex]) ||
				(secondAtomIndex < visibleCovered.size() && visibleCovered[secondAtomIndex]);
			const bool renderableEndpointCovered =
				(firstAtomIndex < renderableCovered.size() && renderableCovered[firstAtomIndex]) ||
				(secondAtomIndex < renderableCovered.size() && renderableCovered[secondAtomIndex]);

			if (visibleEndpointCovered)
				bond.visible = false;
			if (renderableEndpointCovered)
				bond.renderable = false;
		}
	}
} // namespace DefectStudio
