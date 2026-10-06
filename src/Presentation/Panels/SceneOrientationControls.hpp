#pragma once

#include <vector>
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	struct RendererWindowState;
	void DrawSceneOrbitalAimControls(RendererWindowState &windowState, const std::vector<SceneObjectId> &selection);
	void DrawSceneAxisAlignmentMenu(RendererWindowState &windowState);
	void DrawSceneAxisAlignmentControls(RendererWindowState &windowState);
	void DrawScenePlaneDefectPlacementMenu(RendererWindowState &windowState, bool addPlane = false);
	void DrawSceneDefectPlacementMenu(RendererWindowState &windowState);
	void DrawSceneDefectPlacementControls(RendererWindowState &windowState, bool planePresets = false);
	void DrawScenePlaneRotationControls(RendererWindowState &windowState, const std::vector<SceneObjectId> &selection);
}
