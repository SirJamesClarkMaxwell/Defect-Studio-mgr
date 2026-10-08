#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransformHideVolumes.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-6f;

		[[nodiscard]] glm::vec3 StoredCenter(const SceneHideVolume &volume, const glm::mat3 &lattice, const glm::vec3 &world)
		{
			if (volume.frame != HideVolumeFrame::Fractional)
				return world;
			return std::abs(glm::determinant(lattice)) > kEpsilon ? glm::inverse(lattice) * world : volume.center;
		}

		void RefreshMask(RendererWindowState &window)
		{
			SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
		}
	} // namespace

	void CaptureSceneTransformHideVolumes(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot)
	{
		for (const SceneObjectId id : window.selectedSceneHideVolumes)
		{
			const auto it = std::find_if(window.sceneHideVolumes.begin(), window.sceneHideVolumes.end(),
				[id](const SceneHideVolume &volume) { return volume.id == id; });
			if (it == window.sceneHideVolumes.end())
				continue;
			snapshot.hideVolumes.push_back({static_cast<std::size_t>(it - window.sceneHideVolumes.begin()),
				HideVolumeWorldCenter(*it, window.structure), it->center, it->orientation, it->halfExtents, it->anchorAtoms});
		}
	}

	void ApplySceneTransformHideVolumes(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, const ModalTransformOp operation, const TransformPivotMode pivotMode,
		const glm::vec3 &selectionPivot)
	{
		if (snapshot.hideVolumes.empty())
			return;
		for (const HideVolumeTransformStart &start : snapshot.hideVolumes)
		{
			if (start.index >= window.sceneHideVolumes.size())
				continue;
			SceneHideVolume &volume = window.sceneHideVolumes[start.index];
			volume.center = start.center;
			volume.orientation = start.orientation;
			volume.halfExtents = start.halfExtents;
			volume.anchorAtoms = start.anchorAtoms;
			if (operation == ModalTransformOp::Scale)
			{
				// ponytail: in the Fractional frame the axes live in fractional space; the world-space
				// stretch is read along them as if the cell were cubic. Exact for Anchored volumes.
				glm::vec3 axisScale(1.0f);
				for (int axis = 0; axis < 3; ++axis)
					axisScale[axis] = glm::length(delta.spatial.linear * start.orientation[axis]);
				if (volume.kind == HideVolumeKind::Sphere)
					volume.halfExtents.x = start.halfExtents.x * delta.scaleFactor;
				else if (volume.kind == HideVolumeKind::Cylinder)
				{
					// The radius follows whichever cross-section axis the drag actually stretched.
					const float radial = std::abs(axisScale.x - 1.0f) >= std::abs(axisScale.y - 1.0f) ? axisScale.x : axisScale.y;
					volume.halfExtents.x = start.halfExtents.x * radial;
					volume.halfExtents.z = start.halfExtents.z * axisScale.z;
				}
				else
					volume.halfExtents = start.halfExtents * axisScale;
				volume.halfExtents = glm::clamp(volume.halfExtents, glm::vec3(0.01f), glm::vec3(1000.0f));
				continue;
			}
			const glm::vec3 pivot = operation == ModalTransformOp::Rotate && pivotMode == TransformPivotMode::IndividualOrigins
				? start.worldCenter
				: selectionPivot;
			const glm::vec3 world = ApplyTransformDelta(delta.spatial, start.worldCenter, pivot);
			if (operation == ModalTransformOp::Rotate)
				volume.orientation = glm::mat3_cast(glm::normalize(delta.spatial.rotation)) * start.orientation;
			if (glm::distance(world, start.worldCenter) > kEpsilon)
			{
				volume.anchorAtoms.clear();
				volume.center = StoredCenter(volume, window.structure.lattice, world);
			}
		}
		RefreshMask(window);
	}

	void RestoreSceneTransformHideVolumes(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot)
	{
		if (snapshot.hideVolumes.empty())
			return;
		for (const HideVolumeTransformStart &start : snapshot.hideVolumes)
		{
			if (start.index >= window.sceneHideVolumes.size())
				continue;
			SceneHideVolume &volume = window.sceneHideVolumes[start.index];
			volume.center = start.center;
			volume.orientation = start.orientation;
			volume.halfExtents = start.halfExtents;
			volume.anchorAtoms = start.anchorAtoms;
		}
		RefreshMask(window);
	}
} // namespace DefectStudio
