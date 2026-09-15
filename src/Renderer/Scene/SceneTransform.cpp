#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransform.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneObject.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-6f;

		[[nodiscard]] glm::mat3 ArrowBasis(const ArrowTransformStart &arrow)
		{
			const glm::vec3 direction = arrow.end - arrow.start;
			if (glm::dot(direction, direction) <= kEpsilon * kEpsilon)
				return glm::mat3(1.0f);

			const glm::vec3 z = glm::normalize(direction);
			const glm::vec3 reference = std::abs(z.z) < 0.9f
				? glm::vec3(0.0f, 0.0f, 1.0f)
				: glm::vec3(0.0f, 1.0f, 0.0f);
			const glm::vec3 x = glm::normalize(glm::cross(reference, z));
			return glm::mat3(x, glm::cross(z, x), z);
		}

		[[nodiscard]] glm::mat3 LabelBasis(float rotationRadians)
		{
			return glm::mat3(glm::rotate(glm::mat4(1.0f), rotationRadians, glm::vec3(0.0f, 0.0f, 1.0f)));
		}

		[[nodiscard]] glm::vec3 ItemPivot(
			TransformPivotMode mode, const glm::vec3 &origin, const glm::vec3 &selectionPivot)
		{
			return mode == TransformPivotMode::IndividualOrigins ? origin : selectionPivot;
		}

		[[nodiscard]] bool ResolvePinBasePosition(
			const RendererWindowState &window, std::size_t index, glm::vec3 &position)
		{
			if (index >= window.pinnedMeasurements.size())
				return false;
			RendererWindowState::PinnedMeasurement pin = window.pinnedMeasurements[index];
			pin.worldOffset = glm::vec3(0.0f);
			return SceneSystem::ResolvePinnedMeasurementPosition(window.structure, pin, position);
		}

		[[nodiscard]] SceneArrowTransformTarget ResolveSceneArrowTransformTarget(
			const RendererWindowState &window, ModalTransformOp operation)
		{
			if (operation != ModalTransformOp::Translate || window.selectedSceneArrows.size() != 1)
				return SceneArrowTransformTarget::Both;

			const std::size_t index = AnnotationIndex(window.sceneArrows, window.selectedSceneArrows.front());
			if (index >= window.sceneArrows.size() || window.sceneArrowGizmoActiveArrowIndex != index)
				return SceneArrowTransformTarget::Both;

			using Target = RendererWindowState::SceneArrowDragTarget;
			if (window.sceneArrowGizmoActiveTarget == Target::Start)
				return SceneArrowTransformTarget::Start;
			if (window.sceneArrowGizmoActiveTarget == Target::End)
				return SceneArrowTransformTarget::End;
			return SceneArrowTransformTarget::Both;
		}
	} // namespace

	SceneTransformSelectionSnapshot CaptureSceneTransformSelection(
		const RendererWindowState &window, SceneArrowTransformTarget arrowTarget)
	{
		SceneTransformSelectionSnapshot snapshot;
		for (const std::size_t index : window.selectedAtomIndices)
		{
			if (index < window.structure.atoms.size())
				snapshot.atoms.push_back({index, window.structure.atoms[index].cartesianPosition});
		}

		for (const SceneObjectId id : window.selectedPinnedMeasurements)
		{
			const std::size_t index = AnnotationIndex(window.pinnedMeasurements, id);
			if (index >= window.pinnedMeasurements.size())
				continue;
			const RendererWindowState::PinnedMeasurement &pin = window.pinnedMeasurements[index];
			glm::vec3 position(0.0f);
			if (!SceneSystem::ResolvePinnedMeasurementPosition(window.structure, pin, position))
				continue;
			snapshot.labels.push_back(
				{true, index, position, pin.worldOffset, pin.rotationOffsetRadians, pin.style.scale});
		}
		for (const SceneObjectId id : window.selectedFreeLabels)
		{
			const std::size_t index = AnnotationIndex(window.freeLabels, id);
			if (index >= window.freeLabels.size())
				continue;
			const RendererWindowState::FreeLabel &label = window.freeLabels[index];
			snapshot.labels.push_back(
				{false, index, label.worldPosition, label.worldPosition, label.rotationRadians, label.style.scale});
		}
		for (const SceneObjectId id : window.selectedSceneArrows)
		{
			const std::size_t index = AnnotationIndex(window.sceneArrows, id);
			if (index >= window.sceneArrows.size())
				continue;
			const RendererWindowState::SceneArrow &arrow = window.sceneArrows[index];
			snapshot.arrows.push_back(
				{index, arrow.start, arrow.end, arrow.style.shaftWidth, arrow.style.headWidth,
					arrow.style.headLength, arrowTarget});
		}
		return snapshot;
	}

	SceneTransformSelectionSnapshot CaptureSceneTransformSelectionForOperation(
		const RendererWindowState &window, ModalTransformOp operation)
	{
		return CaptureSceneTransformSelection(window, ResolveSceneArrowTransformTarget(window, operation));
	}

	std::vector<glm::vec3> SceneTransformPivotPositions(const SceneTransformSelectionSnapshot &snapshot)
	{
		std::vector<glm::vec3> positions;
		positions.reserve(snapshot.atoms.size() + snapshot.labels.size() + snapshot.arrows.size() * 2);
		for (const AtomTransformStart &atom : snapshot.atoms)
			positions.push_back(atom.position);
		for (const LabelTransformStart &label : snapshot.labels)
			positions.push_back(label.position);
		for (const ArrowTransformStart &arrow : snapshot.arrows)
		{
			if (arrow.target != SceneArrowTransformTarget::End)
				positions.push_back(arrow.start);
			if (arrow.target != SceneArrowTransformTarget::Start)
				positions.push_back(arrow.end);
		}
		return positions;
	}

	std::optional<glm::mat3> SceneTransformLocalBasis(const SceneTransformSelectionSnapshot &snapshot)
	{
		if (!snapshot.arrows.empty())
			return ArrowBasis(snapshot.arrows.back());
		if (!snapshot.labels.empty())
			return LabelBasis(snapshot.labels.back().rotationRadians);
		return std::nullopt;
	}

	bool HasAtomTransformTargets(const SceneTransformSelectionSnapshot &snapshot)
	{
		return !snapshot.atoms.empty();
	}

	bool HasSceneObjectTransformTargets(const SceneTransformSelectionSnapshot &snapshot)
	{
		return !snapshot.labels.empty() || !snapshot.arrows.empty();
	}

	void ApplySceneTransformSelection(
		RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, ModalTransformOp operation, TransformPivotMode pivotMode,
		const glm::vec3 &selectionPivot)
	{
		for (const AtomTransformStart &start : snapshot.atoms)
		{
			if (start.index >= window.structure.atoms.size())
				continue;
			window.structure.atoms[start.index].cartesianPosition = ApplyTransformDelta(
				delta.spatial, start.position, ItemPivot(pivotMode, start.position, selectionPivot));
		}

		for (const LabelTransformStart &start : snapshot.labels)
		{
			if (start.pinned)
			{
				if (start.index >= window.pinnedMeasurements.size())
					continue;
				RendererWindowState::PinnedMeasurement &pin = window.pinnedMeasurements[start.index];
				if (operation == ModalTransformOp::Translate)
				{
					glm::vec3 basePosition(0.0f);
					if (ResolvePinBasePosition(window, start.index, basePosition))
					{
						const glm::vec3 desired = ApplyTransformDelta(
							delta.spatial, start.position,
							ItemPivot(pivotMode, start.position, selectionPivot));
						pin.worldOffset = desired - basePosition;
					}
				}
				else if (operation == ModalTransformOp::Rotate)
					pin.rotationOffsetRadians = start.rotationRadians + delta.rotationRadians;
				else
					pin.style.scale = std::clamp(start.scale * delta.scaleFactor, 0.1f, 8.0f);
				continue;
			}

			if (start.index >= window.freeLabels.size())
				continue;
			RendererWindowState::FreeLabel &label = window.freeLabels[start.index];
			if (operation == ModalTransformOp::Translate)
				label.worldPosition = ApplyTransformDelta(
					delta.spatial, start.position, ItemPivot(pivotMode, start.position, selectionPivot));
			else if (operation == ModalTransformOp::Rotate)
				label.rotationRadians = start.rotationRadians + delta.rotationRadians;
			else
				label.style.scale = std::clamp(start.scale * delta.scaleFactor, 0.1f, 8.0f);
		}

		for (const ArrowTransformStart &start : snapshot.arrows)
		{
			if (start.index >= window.sceneArrows.size())
				continue;
			RendererWindowState::SceneArrow &arrow = window.sceneArrows[start.index];
			if (operation == ModalTransformOp::Translate)
			{
				if (start.target != SceneArrowTransformTarget::End)
					arrow.start = ApplyTransformDelta(delta.spatial, start.start, selectionPivot);
				if (start.target != SceneArrowTransformTarget::Start)
					arrow.end = ApplyTransformDelta(delta.spatial, start.end, selectionPivot);
			}
			else if (operation == ModalTransformOp::Rotate)
			{
				const glm::vec3 origin = (start.start + start.end) * 0.5f;
				const glm::vec3 pivot = ItemPivot(pivotMode, origin, selectionPivot);
				arrow.start = ApplyTransformDelta(delta.spatial, start.start, pivot);
				arrow.end = ApplyTransformDelta(delta.spatial, start.end, pivot);
			}
			else
			{
				arrow.style.shaftWidth = std::clamp(start.shaftWidth * delta.scaleFactor, 0.005f, 1.0f);
				arrow.style.headWidth = std::clamp(start.headWidth * delta.scaleFactor, 0.01f, 1.0f);
				arrow.style.headLength = std::clamp(start.headLength * delta.scaleFactor, 0.01f, 2.0f);
			}
		}
	}

	void RestoreSceneTransformSelection(
		RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot)
	{
		for (const AtomTransformStart &start : snapshot.atoms)
			if (start.index < window.structure.atoms.size())
				window.structure.atoms[start.index].cartesianPosition = start.position;
		for (const LabelTransformStart &start : snapshot.labels)
		{
			if (start.pinned && start.index < window.pinnedMeasurements.size())
			{
				RendererWindowState::PinnedMeasurement &pin = window.pinnedMeasurements[start.index];
				pin.worldOffset = start.storedPosition;
				pin.rotationOffsetRadians = start.rotationRadians;
				pin.style.scale = start.scale;
			}
			else if (!start.pinned && start.index < window.freeLabels.size())
			{
				RendererWindowState::FreeLabel &label = window.freeLabels[start.index];
				label.worldPosition = start.storedPosition;
				label.rotationRadians = start.rotationRadians;
				label.style.scale = start.scale;
			}
		}
		for (const ArrowTransformStart &start : snapshot.arrows)
		{
			if (start.index >= window.sceneArrows.size())
				continue;
			RendererWindowState::SceneArrow &arrow = window.sceneArrows[start.index];
			arrow.start = start.start;
			arrow.end = start.end;
			arrow.style.shaftWidth = start.shaftWidth;
			arrow.style.headWidth = start.headWidth;
			arrow.style.headLength = start.headLength;
		}
	}
} // namespace DefectStudio
