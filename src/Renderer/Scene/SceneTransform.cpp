#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransform.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneObject.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
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

		[[nodiscard]] glm::mat3 PlaneBasis(const PlaneTransformStart &plane)
		{
			return glm::mat3(plane.tangent, glm::cross(plane.normal, plane.tangent), plane.normal);
		}

		[[nodiscard]] glm::vec3 RotatedEulerDegrees(
			const glm::vec3 &startEulerDegrees, const glm::quat &rotation)
		{
			const glm::quat start = glm::quat(glm::radians(startEulerDegrees));
			return glm::degrees(glm::eulerAngles(glm::normalize(rotation * start)));
		}

		void RotatePlaneFrame(
			RendererWindowState::ScenePlane &plane, const PlaneTransformStart &start, const glm::quat &rotation)
		{
			plane.normal = glm::normalize(rotation * start.normal);
			const glm::vec3 rotatedTangent = rotation * start.tangent;
			const glm::vec3 projected = rotatedTangent - glm::dot(rotatedTangent, plane.normal) * plane.normal;
			plane.tangent = glm::dot(projected, projected) > kEpsilon * kEpsilon
				? glm::normalize(projected)
				: glm::normalize(glm::cross(
					std::abs(plane.normal.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f),
					plane.normal));
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
			snapshot.arrows.push_back({index, arrow.start, arrow.end, arrowTarget});
		}
		for (const SceneObjectId id : window.selectedSceneOrbitals)
		{
			const std::size_t index = AnnotationIndex(window.sceneOrbitals, id);
			if (index >= window.sceneOrbitals.size())
				continue;
			const RendererWindowState::SceneOrbital &orbital = window.sceneOrbitals[index];
			const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, window.structure);
			snapshot.orbitals.push_back(
				{index, centers.centerA, centers.centerB, orbital.rotationEuler, orbital.scale,
					!orbital.anchorAtoms.empty(), IsTwoCenterPreset(orbital.preset)});
		}
		for (const SceneObjectId id : window.selectedScenePlanes)
		{
			const std::size_t index = AnnotationIndex(window.scenePlanes, id);
			if (index >= window.scenePlanes.size())
				continue;
			const RendererWindowState::ScenePlane &plane = window.scenePlanes[index];
			snapshot.planes.push_back(
				{index, plane.center, plane.normal, plane.tangent, plane.halfExtents});
		}

		// Atoms are the gizmo's target only when nothing in the scene layer is selected. Fitting a
		// plane to three atoms leaves those atoms selected, and without this a G on the new plane
		// dragged the structure along with it. Atoms stay selected for everything else - "Match
		// position", the Add menus - they simply stop being transform targets while a scene object
		// is.
		if (!HasSceneObjectTransformTargets(snapshot))
		{
			for (const std::size_t index : window.selectedAtomIndices)
			{
				if (index < window.structure.atoms.size())
					snapshot.atoms.push_back({index, window.structure.atoms[index].cartesianPosition});
			}
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
		positions.reserve(
			snapshot.atoms.size() + snapshot.labels.size() + snapshot.arrows.size() * 2 +
			snapshot.orbitals.size() * 2 + snapshot.planes.size());
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
		for (const OrbitalTransformStart &orbital : snapshot.orbitals)
		{
			positions.push_back(orbital.centerA);
			if (orbital.twoCenter)
				positions.push_back(orbital.centerB);
		}
		for (const PlaneTransformStart &plane : snapshot.planes)
			positions.push_back(plane.center);
		return positions;
	}

	std::optional<glm::mat3> SceneTransformLocalBasis(const SceneTransformSelectionSnapshot &snapshot)
	{
		if (!snapshot.arrows.empty())
			return ArrowBasis(snapshot.arrows.back());
		if (!snapshot.labels.empty())
			return LabelBasis(snapshot.labels.back().rotationRadians);
		if (!snapshot.planes.empty())
			return PlaneBasis(snapshot.planes.back());
		return std::nullopt;
	}

	bool HasAtomTransformTargets(const SceneTransformSelectionSnapshot &snapshot)
	{
		return !snapshot.atoms.empty();
	}

	bool HasSceneObjectTransformTargets(const SceneTransformSelectionSnapshot &snapshot)
	{
		return !snapshot.labels.empty() || !snapshot.arrows.empty() || !snapshot.orbitals.empty() ||
			!snapshot.planes.empty();
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
				const glm::vec3 origin = (start.start + start.end) * 0.5f;
				const glm::vec3 pivot = ItemPivot(pivotMode, origin, selectionPivot);
				arrow.start = ApplyTransformDelta(delta.spatial, start.start, pivot);
				arrow.end = ApplyTransformDelta(delta.spatial, start.end, pivot);
			}
		}

		for (const OrbitalTransformStart &start : snapshot.orbitals)
		{
			if (start.index >= window.sceneOrbitals.size())
				continue;
			RendererWindowState::SceneOrbital &orbital = window.sceneOrbitals[start.index];
			if (operation == ModalTransformOp::Translate)
			{
				orbital.centerA = ApplyTransformDelta(delta.spatial, start.centerA, selectionPivot);
				if (start.twoCenter)
					orbital.centerB = ApplyTransformDelta(delta.spatial, start.centerB, selectionPivot);
				if (start.anchored)
					orbital.anchorAtoms.clear();
			}
			else if (operation == ModalTransformOp::Rotate)
			{
				const glm::vec3 origin =
					start.twoCenter ? (start.centerA + start.centerB) * 0.5f : start.centerA;
				const glm::vec3 pivot = ItemPivot(pivotMode, origin, selectionPivot);
				orbital.centerA = ApplyTransformDelta(delta.spatial, start.centerA, pivot);
				if (start.twoCenter)
					orbital.centerB = ApplyTransformDelta(delta.spatial, start.centerB, pivot);
				orbital.rotationEuler = RotatedEulerDegrees(start.rotationEuler, delta.spatial.rotation);
			}
			else
			{
				orbital.scale = std::clamp(start.scale * delta.scaleFactor, 0.05f, 20.0f);
			}
		}

		for (const PlaneTransformStart &start : snapshot.planes)
		{
			if (start.index >= window.scenePlanes.size())
				continue;
			RendererWindowState::ScenePlane &plane = window.scenePlanes[start.index];
			if (operation == ModalTransformOp::Translate)
			{
				plane.center = ApplyTransformDelta(delta.spatial, start.center, selectionPivot);
			}
			else if (operation == ModalTransformOp::Rotate)
			{
				const glm::vec3 pivot = ItemPivot(pivotMode, start.center, selectionPivot);
				plane.center = ApplyTransformDelta(delta.spatial, start.center, pivot);
				RotatePlaneFrame(plane, start, delta.spatial.rotation);
			}
			else
			{
				plane.halfExtents = glm::clamp(
					start.halfExtents * delta.scaleFactor, glm::vec2(0.01f), glm::vec2(1000.0f));
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
		}
		for (const OrbitalTransformStart &start : snapshot.orbitals)
		{
			if (start.index >= window.sceneOrbitals.size())
				continue;
			RendererWindowState::SceneOrbital &orbital = window.sceneOrbitals[start.index];
			orbital.centerA = start.centerA;
			orbital.centerB = start.centerB;
			orbital.rotationEuler = start.rotationEuler;
			orbital.scale = start.scale;
			if (start.anchored && window.modalTransformSceneObjectsBefore.has_value() &&
				start.index < window.modalTransformSceneObjectsBefore->sceneOrbitals.size())
			{
				orbital.anchorAtoms = window.modalTransformSceneObjectsBefore->sceneOrbitals[start.index].anchorAtoms;
			}
		}
		for (const PlaneTransformStart &start : snapshot.planes)
		{
			if (start.index >= window.scenePlanes.size())
				continue;
			RendererWindowState::ScenePlane &plane = window.scenePlanes[start.index];
			plane.center = start.center;
			plane.normal = start.normal;
			plane.tangent = start.tangent;
			plane.halfExtents = start.halfExtents;
		}
	}
} // namespace DefectStudio
