#pragma once

#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	// G/R/S on the selected vacancy markers and the selected defect axes, on the renderer copies
	// only (window.structure.vacancies / window.structure.defectFrame). The commit to the domain is
	// the caller's (ViewportModalTransform), through the undoable set commands.
	void CaptureSceneTransformDefectMarkers(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot);
	void ApplySceneTransformDefectMarkers(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, TransformPivotMode pivotMode, const glm::vec3 &selectionPivot);
	void RestoreSceneTransformDefectMarkers(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot);
} // namespace DefectStudio
