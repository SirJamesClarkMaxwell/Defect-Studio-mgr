#pragma once

#include <vector>
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	struct RendererWindowState;
	void DrawSceneOrbitalAimControls(RendererWindowState &windowState, const std::vector<SceneObjectId> &selection);
	void DrawSceneAxisAlignmentMenu(RendererWindowState &windowState);
	void DrawSceneAxisAlignmentControls(RendererWindowState &windowState);
}
