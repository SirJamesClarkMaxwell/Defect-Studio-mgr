#pragma once

#include "Renderer/Scene/SceneTransform.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	void CaptureSceneTransformLabels(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot,
		const std::vector<SceneObjectId> &freeLabels);
	void ApplySceneTransformLabels(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, ModalTransformOp operation, TransformPivotMode pivotMode,
		const glm::vec3 &selectionPivot);
	void RestoreSceneTransformLabels(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot);
} // namespace DefectStudio
