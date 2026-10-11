#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransformHideVolumes.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

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

		// World -> the volume's own frame space: identity for Anchored, fractional for Fractional.
		// nullopt for a singular lattice.
		[[nodiscard]] std::optional<glm::mat3> FrameFromWorld(const SceneHideVolume &volume, const glm::mat3 &lattice)
		{
			if (volume.frame != HideVolumeFrame::Fractional)
				return glm::mat3(1.0f);
			if (std::abs(glm::determinant(lattice)) <= kEpsilon)
				return std::nullopt;
			return glm::inverse(lattice);
		}

		// Gram-Schmidt, keeping column 2 (the cylinder axis) exact: `orientation` must stay orthonormal.
		[[nodiscard]] glm::mat3 Orthonormalised(const glm::mat3 &axes)
		{
			const glm::vec3 z = glm::normalize(axes[2]);
			glm::vec3 x = axes[0] - glm::dot(axes[0], z) * z;
			x = glm::dot(x, x) > kEpsilon ? glm::normalize(x)
				: glm::normalize(glm::cross(std::abs(z.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f), z));
			return glm::mat3(x, glm::cross(z, x), z);
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
			glm::mat3 worldAxes = it->orientation;
			if (it->frame == HideVolumeFrame::Fractional)
				for (int axis = 0; axis < 3; ++axis)
				{
					const glm::vec3 world = window.structure.lattice * it->orientation[axis];
					worldAxes[axis] = glm::dot(world, world) > kEpsilon ? glm::normalize(world) : it->orientation[axis];
				}
			snapshot.hideVolumes.push_back({static_cast<std::size_t>(it - window.sceneHideVolumes.begin()),
				HideVolumeWorldCenter(*it, window.structure), it->center, it->orientation, it->halfExtents, it->anchorAtoms,
				worldAxes});
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
			// The world-space linear part and rotation, carried into the frame `orientation` lives in.
			const std::optional<glm::mat3> frameFromWorld = FrameFromWorld(volume, window.structure.lattice);
			if (!frameFromWorld)
				continue;
			const glm::mat3 worldFromFrame = glm::inverse(*frameFromWorld);
			if (operation == ModalTransformOp::Scale)
			{
				const glm::mat3 linear = *frameFromWorld * delta.spatial.linear * worldFromFrame;
				glm::vec3 axisScale(1.0f);
				for (int axis = 0; axis < 3; ++axis)
					axisScale[axis] = glm::length(linear * start.orientation[axis]);
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
				// ponytail: in a non-cubic cell a world rotation is not a rotation of fractional space;
				// re-orthonormalising keeps the volume valid with the cylinder axis where it was turned to.
				volume.orientation = Orthonormalised(
					*frameFromWorld * glm::mat3_cast(glm::normalize(delta.spatial.rotation)) * worldFromFrame * start.orientation);
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
