#include "Core/dspch.hpp"
#include "Renderer/Scene/SceneAxisAlignment.hpp"

#include <cmath>
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio
{
	namespace
	{
		bool IsRotationBasis(const glm::mat3 &basis)
		{
			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j)
					if (!std::isfinite(basis[i][j]))
						return false;
				if (std::abs(glm::dot(basis[i], basis[i]) - 1.0f) > 1e-3f ||
					std::abs(glm::dot(basis[i], basis[(i + 1) % 3])) > 1e-3f)
					return false;
			}
			return std::abs(glm::determinant(basis) - 1.0f) < 1e-3f;
		}
	}

	std::optional<glm::quat> SceneAxisAlignmentRotation(
		const glm::mat3 &objectBasis, const glm::mat3 &defectBasis, int ownAxis, int defectAxis)
	{
		if (ownAxis < 0 || ownAxis > 2 || defectAxis < 0 || defectAxis > 2 ||
			!IsRotationBasis(objectBasis) || !IsRotationBasis(defectBasis))
			return std::nullopt;
		const int secondary = ownAxis == 0 ? 1 : 0;
		const int reference = defectAxis == 0 ? 1 : 0;
		glm::mat3 desired(1.0f);
		desired[ownAxis] = defectBasis[defectAxis];
		desired[secondary] = defectBasis[reference];
		const int remaining = 3 - ownAxis - secondary;
		desired[remaining] = glm::normalize(glm::cross(desired[(remaining + 1) % 3], desired[(remaining + 2) % 3]));
		return glm::normalize(glm::quat_cast(desired * glm::transpose(objectBasis)));
	}

	std::vector<SceneAxisAlignmentTarget> CollectSceneAxisAlignmentTargets(const RendererWindowState &windowState)
	{
		const auto all = CaptureSceneTransformSelection(windowState);
		std::vector<SceneAxisAlignmentTarget> targets;
		auto add = [&](SceneTransformSelectionSnapshot one) {
			if (const auto basis = SceneTransformLocalBasis(one))
				targets.push_back({std::move(one), *basis});
		};
		for (const auto &plane : all.planes)
			add(SceneTransformSelectionSnapshot{.planes = {plane}});
		for (const auto &path : all.paths)
			add(SceneTransformSelectionSnapshot{.paths = {path}});
		for (const auto &orbital : all.orbitals)
			if (orbital.index < windowState.sceneOrbitals.size() && !orbital.twoCenter &&
				windowState.sceneOrbitals[orbital.index].lcaoComponents.empty())
				targets.push_back({SceneTransformSelectionSnapshot{.orbitals = {orbital}},
					glm::mat3_cast(glm::quat(glm::radians(orbital.rotationEuler)))});
		return targets;
	}

	std::size_t AlignSelectedSceneObjectAxes(RendererWindowState &windowState, int ownAxis, int defectAxis)
	{
		const auto &frame = windowState.structure.defectFrame;
		if (!frame)
			return 0;
		const glm::mat3 axes(frame->x, frame->y, frame->z);
		std::size_t count = 0;
		for (const auto &target : CollectSceneAxisAlignmentTargets(windowState))
		{
			const auto rotation = SceneAxisAlignmentRotation(target.basis, axes, ownAxis, defectAxis);
			if (!rotation)
				continue;
			for (const auto &plane : target.snapshot.planes)
				windowState.scenePlanes[plane.index].anchorAtoms.clear();
			SceneTransformDelta delta;
			delta.spatial.rotation = *rotation;
			ApplySceneTransformSelection(windowState, target.snapshot, delta, ModalTransformOp::Rotate,
				TransformPivotMode::IndividualOrigins, glm::vec3(0.0f));
			++count;
		}
		return count;
	}
}
