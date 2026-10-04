#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Blender-style modal G/R/S core: constraint cycling, mouse -> transform mapping, increment snapping,
// typed numeric entry and the header readout. Pure math - no ImGui, no window state - so every rule
// here is unit-tested; the viewport gizmos only translate key/mouse input into these calls and apply
// the resulting TransformDelta to their own start-of-operation snapshots.
namespace DefectStudio
{
	enum class ModalTransformOp
	{
		Translate,
		Rotate,
		Scale
	};

	// Meaning of X/Y/Z. Global = world axes, Local = the object kind's own frame (absent for atoms),
	// Lattice = cell vectors a/b/c (not necessarily orthogonal or unit length), Defect = the
	// structure's defect axes (CrystalStructure::defectFrame) whatever is selected.
	enum class TransformOrientation
	{
		Global,
		Local,
		Lattice,
		Defect
	};

	enum class TransformPivotMode
	{
		Median,
		Cursor3D,
		IndividualOrigins
	};

	enum class ConstraintKind
	{
		None,
		Axis,
		Plane
	};

	enum class SnapMode
	{
		Off,
		Increment,
		Fine
	};

	// Optional frames the constraint keys can resolve to. Columns are the basis vectors.
	struct TransformBases
	{
		std::optional<glm::mat3> local;
		std::optional<glm::mat3> lattice;
		std::optional<glm::mat3> defect;
	};

	// Axis: move/rotate/scale along basis column `axis`. Plane: `axis` is the EXCLUDED column -
	// Shift+X spans columns 1 and 2 (under Lattice: b and c, not the plane perpendicular to a).
	struct TransformConstraint
	{
		ConstraintKind kind = ConstraintKind::None;
		int axis = -1;
		TransformOrientation space = TransformOrientation::Global;
	};

	struct TransformSnapSteps
	{
		float translate = 0.1f;
		float rotateDegrees = 5.0f;
		float scale = 0.1f;
	};

	struct ModalTransformView
	{
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 projection = glm::mat4(1.0f);
		glm::vec2 viewportOrigin = glm::vec2(0.0f);
		glm::vec2 viewportSize = glm::vec2(1.0f);
	};

	// Total transform since the operation started (never a per-frame increment). Applied to a start
	// snapshot as pivot + rotation * (linear * (start - pivot)) + translation.
	struct TransformDelta
	{
		glm::vec3 translation = glm::vec3(0.0f);
		glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::mat3 linear = glm::mat3(1.0f);
	};

	struct ModalTransformSession
	{
		ModalTransformOp op = ModalTransformOp::Translate;
		TransformOrientation orientation = TransformOrientation::Global;
		TransformBases bases;
		glm::vec3 pivot = glm::vec3(0.0f);
		// Anchor is where the thing is; pivot is what it turns about.
		glm::vec3 anchor = glm::vec3(0.0f);
		glm::vec2 startMouse = glm::vec2(0.0f);
		glm::vec2 lastMouse = glm::vec2(0.0f);
		// Rotate only: signed screen angle summed frame by frame so turns past 180 degrees keep going.
		float accumulatedAngleRadians = 0.0f;
		TransformConstraint constraint;
		// Non-empty = numeric override active (even when it does not parse yet, e.g. "-").
		std::string numericText;
	};

	[[nodiscard]] ModalTransformSession BeginModalTransform(
		ModalTransformOp op, TransformOrientation orientation, TransformBases bases, const glm::vec3 &pivot,
		const glm::vec2 &mouse);

	// Global, or a missing Local/Lattice frame, resolves to identity.
	[[nodiscard]] glm::mat3 ResolveBasis(TransformOrientation space, const TransformBases &bases);

	// One X/Y/Z press (plane = Shift held). Same axis and kind as `current` advances the cycle
	// primary -> secondary -> None; a different axis or kind restarts at primary.
	// primary = `orientation` (Global when its frame is missing); secondary = Global when primary is
	// not Global, otherwise Local, or Defect when there is no Local frame; a secondary whose frame is
	// missing is skipped straight to None.
	[[nodiscard]] TransformConstraint CycleConstraint(
		const TransformConstraint &current, int axis, bool plane, TransformOrientation orientation,
		const TransformBases &bases);

	// Axis: component along the normalized column. Plane: orthogonal projection onto the span of the
	// two remaining columns. None: unchanged.
	[[nodiscard]] glm::vec3 ConstrainTranslation(
		const glm::vec3 &worldDelta, const TransformConstraint &constraint, const TransformBases &bases);

	// B * diag * B^-1 with `factor` on the constrained column(s) (all three for None), 1 elsewhere.
	[[nodiscard]] glm::mat3 ConstrainedScaleMatrix(
		float factor, const TransformConstraint &constraint, const TransformBases &bases);

	// Nearest multiple of step; step <= 0 returns value unchanged.
	[[nodiscard]] float SnapValue(float value, float step);
	// Off -> 0; Increment -> the op's step; Fine -> step / 10.
	[[nodiscard]] float ResolveSnapStep(ModalTransformOp op, SnapMode mode, const TransformSnapSteps &steps);
	// Ctrl -> Increment, Ctrl+Shift -> Fine, otherwise Off.
	[[nodiscard]] SnapMode SnapModeFromModifiers(bool ctrl, bool shift);

	// '0'-'9' append; '.' only once; '-' toggles a leading minus; anything else is ignored.
	void AppendNumericChar(ModalTransformSession &session, char character);
	void EraseNumericChar(ModalTransformSession &session);
	// nullopt when empty or not yet a number ("-", ".", "-.").
	[[nodiscard]] std::optional<float> NumericValue(const ModalTransformSession &session);

	// Call once per frame. Numeric override (numericText non-empty) ignores the mouse and snapping:
	// Translate moves `value` along the constraint axis (None -> column 0 of the orientation basis,
	// Plane -> first in-plane column), Rotate turns `value` degrees right-handed about the rotation
	// axis, Scale uses `value` as the factor; an unparsable text counts as 0 (Translate/Rotate) or 1
	// (Scale). Mouse path: Translate = difference of the view-ray hits at start and now (None: plane
	// through pivot facing the camera; Axis: closest point on the axis line; Plane: ray/plane hit;
	// degenerate view-parallel cases give zero), snapped per coordinate of the normalized constraint
	// basis. Rotate = accumulated screen angle around the projected pivot, counter-clockwise on screen
	// turning counter-clockwise as seen by the viewer; axis = constraint axis (Plane: its excluded
	// column; None: toward the viewer); snapped in degrees. Scale = |mouse - pivotScreen| /
	// |startMouse - pivotScreen|, snapped.
	[[nodiscard]] TransformDelta EvaluateModalTransform(
		ModalTransformSession &session, const ModalTransformView &view, const glm::vec2 &mouse, SnapMode snap,
		const TransformSnapSteps &steps);

	[[nodiscard]] glm::vec3 ApplyTransformDelta(
		const TransformDelta &delta, const glm::vec3 &start, const glm::vec3 &pivot);

	// Median and IndividualOrigins -> mean of positions (IndividualOrigins callers then pass each
	// item's own start as the pivot to ApplyTransformDelta); Cursor3D -> cursor, or the mean when no
	// cursor is placed. Empty positions -> cursor or origin.
	[[nodiscard]] glm::vec3 ComputeTransformPivot(
		TransformPivotMode mode, const std::vector<glm::vec3> &positions, const std::optional<glm::vec3> &cursor3D);

	// One line, e.g. "Move  X (Global)  D 2.000 A  Snap 0.1". Names the op (Move/Rotate/Scale), the
	// constraint axis letters (X/Y/Z, or a/b/c under Lattice; plane lists both letters), the space
	// name, the current value (typed text when numeric is active) and the snap step when snapping.
	[[nodiscard]] std::string FormatModalTransformHeader(
		const ModalTransformSession &session, const TransformDelta &delta, SnapMode snap,
		const TransformSnapSteps &steps);
} // namespace DefectStudio
