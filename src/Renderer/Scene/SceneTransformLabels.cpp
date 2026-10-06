#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransformLabels.hpp"

#include <algorithm>

#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneObject.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool ResolvePinBasePosition(
			const RendererWindowState &window, std::size_t index, glm::vec3 &position)
		{
			if (index >= window.pinnedMeasurements.size())
				return false;
			RendererWindowState::PinnedMeasurement pin = window.pinnedMeasurements[index];
			pin.worldOffset = glm::vec3(0.0f);
			return SceneSystem::ResolvePinnedMeasurementPosition(window.structure, pin, position);
		}

		glm::vec3 ItemPivot(TransformPivotMode mode, const glm::vec3 &origin, const glm::vec3 &selectionPivot)
		{
			return mode == TransformPivotMode::IndividualOrigins ? origin : selectionPivot;
		}
	} // namespace

	void CaptureSceneTransformLabels(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot,
		const std::vector<SceneObjectId> &freeLabels)
	{
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
		for (const SceneObjectId id : freeLabels)
		{
			const std::size_t index = AnnotationIndex(window.freeLabels, id);
			if (index >= window.freeLabels.size())
				continue;
			const RendererWindowState::FreeLabel &label = window.freeLabels[index];
			snapshot.labels.push_back(
				{false, index, label.worldPosition, label.worldPosition, label.rotationRadians, label.style.scale, label.anchorOffset});
		}
	}

	void ApplySceneTransformLabels(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, ModalTransformOp operation, TransformPivotMode pivotMode,
		const glm::vec3 &selectionPivot)
	{
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
			{
				label.worldPosition = ApplyTransformDelta(
					delta.spatial, start.position, ItemPivot(pivotMode, start.position, selectionPivot));
				if (const auto anchor = ResolveFreeLabelAnchor(window, label))
					label.anchorOffset = label.worldPosition - *anchor;
			}
			else if (operation == ModalTransformOp::Rotate)
				label.rotationRadians = start.rotationRadians + delta.rotationRadians;
			else
				label.style.scale = std::clamp(start.scale * delta.scaleFactor, 0.1f, 8.0f);
		}

	}

	void RestoreSceneTransformLabels(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot)
	{
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
				label.anchorOffset = start.anchorOffset;
				label.rotationRadians = start.rotationRadians;
				label.style.scale = start.scale;
			}
		}
	}
} // namespace DefectStudio
