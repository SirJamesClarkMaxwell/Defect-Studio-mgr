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
		std::optional<std::size_t> startAnchorAtom;
		std::optional<std::size_t> endAnchorAtom;
		SceneArrowTransformTarget target = SceneArrowTransformTarget::Both;
	};

	struct OrbitalTransformStart
	{
		std::size_t index = 0;
		glm::vec3 centerA = glm::vec3(0.0f);
		glm::vec3 centerB = glm::vec3(0.0f);
		glm::vec3 rotationEuler = glm::vec3(0.0f);
		float scale = 1.0f;
		// Whether the orbital was anchored to atoms when the drag began. Translating an anchored
		// orbital drops the anchor: the anchor is rewritten from the atoms every frame, so keeping
		// it would silently undo the drag on the next one.
		bool anchored = false;
		// A single-centre orbital keeps centerB around unread so switching preset back and forth
		// does not lose the bond the user set up (RendererWindowState::SceneOrbital). It defaults
		// to (1.5, 0, 0), so treating it as a real centre put the gizmo halfway to a point nothing
		// is drawn at. Only a two-centre preset contributes centerB to the pivot, or takes a write.
		bool twoCenter = false;
	};

	struct PlaneTransformStart
	{
		std::size_t index = 0;
		glm::vec3 center = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
		glm::vec3 tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec2 halfExtents = glm::vec2(1.0f);
	};

	struct SceneTransformSelectionSnapshot
	{
		std::vector<AtomTransformStart> atoms;
		std::vector<LabelTransformStart> labels;
		std::vector<ArrowTransformStart> arrows;
		// G/R/S for orbitals and planes: translate moves the centre(s), rotate turns the object
		// about the pivot (an orbital through its rotationEuler, a plane by carrying its normal and
		// tangent round), scale grows the drawn size (orbital.scale, plane.halfExtents).
		std::vector<OrbitalTransformStart> orbitals;
		std::vector<PlaneTransformStart> planes;
	};

	// Spatial fields come from the shared ModalTransform core. The scalar values preserve the label
	// and orbital meanings of R/S; arrows use the spatial transform for both endpoints.
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
