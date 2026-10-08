#pragma once

#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	// G/R/S on the selected hide volumes. Translate moves the centre (and drops the atom anchor, as
	// for planes - the anchor would pull it back next frame); rotate turns `orientation` and keeps
	// the anchor while the centre stays put; scale grows `halfExtents` along the volume's own axes.
	// Every apply and restore re-folds the atom mask, so atoms appear and vanish while dragging.
	void CaptureSceneTransformHideVolumes(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot);
	void ApplySceneTransformHideVolumes(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, ModalTransformOp operation, TransformPivotMode pivotMode,
		const glm::vec3 &selectionPivot);
	void RestoreSceneTransformHideVolumes(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot);
} // namespace DefectStudio
