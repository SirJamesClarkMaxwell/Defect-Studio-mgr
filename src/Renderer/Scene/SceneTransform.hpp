#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Scene/ModalTransform.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	enum class SceneArrowTransformTarget
	{
		Start,
		End,
		Both,
	};

	struct AtomTransformStart
	{
		std::size_t index = 0;
		glm::vec3 position = glm::vec3(0.0f);
	};

	struct LabelTransformStart
	{
		bool pinned = false;
		std::size_t index = 0;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 storedPosition = glm::vec3(0.0f);
		float rotationRadians = 0.0f;
		float scale = 1.0f;
	};

	struct ArrowTransformStart
	{
		std::size_t index = 0;
		glm::vec3 start = glm::vec3(0.0f);
		glm::vec3 end = glm::vec3(0.0f);
		float shaftWidth = 0.0f;
		float headWidth = 0.0f;
		float headLength = 0.0f;
		SceneArrowTransformTarget target = SceneArrowTransformTarget::Both;
	};

	struct SceneTransformSelectionSnapshot
	{
		std::vector<AtomTransformStart> atoms;
		std::vector<LabelTransformStart> labels;
		std::vector<ArrowTransformStart> arrows;
	};

	// Spatial fields come from the shared ModalTransform core. The two scalar values preserve the
	// object-kind meanings of R/S: labels rotate in their billboard plane and labels/arrows scale
	// their own size fields rather than acquiring a general 3D transform component.
	struct SceneTransformDelta
	{
		TransformDelta spatial;
		float rotationRadians = 0.0f;
		float scaleFactor = 1.0f;
	};

	[[nodiscard]] SceneTransformSelectionSnapshot CaptureSceneTransformSelection(
		const RendererWindowState &window,
		SceneArrowTransformTarget arrowTarget = SceneArrowTransformTarget::Both);
	// A single active endpoint is its own Translate item. Rotate and Scale retain the whole-arrow
	// meanings used before the unified modal driver.
	[[nodiscard]] SceneTransformSelectionSnapshot CaptureSceneTransformSelectionForOperation(
		const RendererWindowState &window, ModalTransformOp operation);
	[[nodiscard]] std::vector<glm::vec3> SceneTransformPivotPositions(
		const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] std::optional<glm::mat3> SceneTransformLocalBasis(
		const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] bool HasAtomTransformTargets(const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] bool HasSceneObjectTransformTargets(const SceneTransformSelectionSnapshot &snapshot);

	void ApplySceneTransformSelection(
		RendererWindowState &window,
		const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta,
		ModalTransformOp operation,
		TransformPivotMode pivotMode,
		const glm::vec3 &selectionPivot);
	void RestoreSceneTransformSelection(
		RendererWindowState &window,
		const SceneTransformSelectionSnapshot &snapshot);
} // namespace DefectStudio
