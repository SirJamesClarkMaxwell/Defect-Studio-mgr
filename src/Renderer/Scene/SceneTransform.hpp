#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Path/PathTypes.hpp"
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
		glm::vec3 anchorOffset = glm::vec3(0.0f);
	};

	struct ArrowTransformStart
	{
		std::size_t index = 0;
		std::vector<glm::vec3> points;
		std::optional<glm::vec3> controlPoint;
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

	// A selected vacancy marker (structure.vacancies index) and the defect axes. Both are domain data:
	// the drag moves the renderer copies, the commit writes the domain through renderer.vacancy.set /
	// renderer.defect_frame.set (SceneTransformDefectMarkers.hpp).
	struct VacancyTransformStart
	{
		std::size_t index = 0;
		glm::vec3 position = glm::vec3(0.0f);
	};

	struct DefectFrameTransformStart
	{
		glm::vec3 origin = glm::vec3(0.0f);
		glm::vec3 x = glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 y = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 z = glm::vec3(0.0f, 0.0f, 1.0f);
	};

	// The path transform captured at the start of a modal operation. G/R/S compose onto these nine
	// authored transform fields; nodes and handles are deliberately not walked or rewritten.
	struct PathTransformStart
	{
		SceneObjectId id;
		PathTransform transform;
	};

	// One element of one path, as it stood when the drag began. `position` is the node's authored
	// LOCAL position for a node, and the handle's offset from its node for a handle - both are the
	// value the element actually stores, so restoring is an assignment rather than a subtraction.
	//
	// `path` is repeated on every entry although Edit Mode only ever has one path open, because the
	// apply reads entries without reaching back for the session, and an entry that cannot say which
	// path it belongs to is a foot-gun the first time a second path can be edited.
	struct PathElementTransformStart
	{
		SceneObjectId path;
		PathElementId element;
		bool isHandle = false;
		glm::vec3 position{0.0f};
		// S14: a node whose binding is not Free. G/R/S moves its binding OFFSET (world space - a bound
		// node is untransformed) instead of its authored position, which the binding would override
		// anyway; `bindingOffset` is the start value restored on cancel. Unused for handles.
		bool bound = false;
		glm::vec3 bindingOffset{0.0f};
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
		// G/R/S compose onto each selected path's object transform.
		std::vector<PathTransformStart> paths;
		// Edit Mode's elements, inside one path whose own transform does NOT change. `paths` and
		// this are mutually exclusive by construction - see CaptureSceneTransformPathElements for
		// why filling both would move a node twice.
		std::vector<PathElementTransformStart> pathElements;
		// G moves vacancies and the defect origin; R also turns the defect axes; S only spreads the
		// points (the axes stay unit vectors).
		std::vector<VacancyTransformStart> vacancies;
		std::optional<DefectFrameTransformStart> defectFrame;
	};

	// Spatial fields come from the shared ModalTransform core. The scalar values preserve the label
	// and orbital meanings of R/S; arrows use the spatial transform for their complete path, and
	// planes resolve its linear component against their tangent/bitangent extents.
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
	// The anchor is where the gizmo widget stands; the pivot is what the transform maths turns about.
	// They differ for Bezier handles (the anchor is the handle, the pivot its owner node) and labels
	// (the anchor is where the label was drawn). With the defect axes in the selection both are the
	// axes' origin: the axes act as the parent of everything transformed with them.
	[[nodiscard]] std::vector<glm::vec3> SceneTransformPivotPositions(
		const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] std::vector<glm::vec3> SceneTransformPivotPositions(
		const RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] std::vector<glm::vec3> SceneTransformAnchorPositions(
		const RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] std::optional<glm::mat3> SceneTransformLocalBasis(
		const SceneTransformSelectionSnapshot &snapshot);
	// local = SceneTransformLocalBasis, lattice = the cell, defect = the structure's defect axes
	// (absent when it has none or they are hidden). A selection without axes of its own (atoms,
	// vacancies) gets the defect axes as its Local frame.
	// The orientation the gizmo and G/R/S use: the chosen one, except that the defect axes, when
	// they are being transformed, use their own axes instead of Global (X X still gives Global).
	[[nodiscard]] TransformOrientation SceneTransformOrientation(
		TransformOrientation chosen, const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] TransformBases SceneTransformBases(
		const RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] bool HasAtomTransformTargets(const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] bool HasSceneObjectTransformTargets(const SceneTransformSelectionSnapshot &snapshot);
	[[nodiscard]] bool HasDefectMarkerTransformTargets(const SceneTransformSelectionSnapshot &snapshot);

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
