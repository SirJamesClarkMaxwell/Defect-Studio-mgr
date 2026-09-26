#pragma once

#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	void CaptureSceneTransformPaths(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot);
	void ApplySceneTransformPaths(
		RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, ModalTransformOp operation,
		TransformPivotMode pivotMode, const glm::vec3 &selectionPivot);
	void RestoreSceneTransformPaths(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot);
}
