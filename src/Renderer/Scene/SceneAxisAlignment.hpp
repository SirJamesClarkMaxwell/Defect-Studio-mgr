#pragma once

#include <optional>
#include <vector>
#include <glm/gtc/quaternion.hpp>
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	// Axes are 0=x, 1=y, 2=z (plane normal). Keeps own x on defect x unless either
	// is the primary axis, then uses y. Both frames must be finite right-handed rotations.
	[[nodiscard]] std::optional<glm::quat> SceneAxisAlignmentRotation(
		const glm::mat3 &objectBasis, const glm::mat3 &defectBasis, int ownAxis, int defectAxis);

	struct SceneAxisAlignmentTarget
	{
		SceneTransformSelectionSnapshot snapshot;
		glm::mat3 basis;
	};
	[[nodiscard]] std::vector<SceneAxisAlignmentTarget> CollectSceneAxisAlignmentTargets(
		const RendererWindowState &windowState);
	// Uses the regular individual-origin transform path. Explicitly detaches fitted planes
	// so their atom resolver cannot overwrite the requested orientation. Caller owns undo.
	std::size_t AlignSelectedSceneObjectAxes(RendererWindowState &windowState, int ownAxis, int defectAxis);
	struct SceneObjectOriginTarget
	{
		SceneTransformSelectionSnapshot snapshot;
		glm::vec3 origin;
	};
	// Explicitly selected objects only. Pinned measurements, BondFrame paths and atom-owned LCAO have no movable origin.
	[[nodiscard]] std::vector<SceneObjectOriginTarget> CollectSceneObjectOriginTargets(const RendererWindowState &window);
	// Position only, using normal G transforms (two-centre orbitals move by their midpoint).
	// Planes/orbitals detach atom anchors. Caller owns the one batch undo snapshot.
	std::size_t MoveSelectedSceneObjectOriginsToDefect(RendererWindowState &window);
}
