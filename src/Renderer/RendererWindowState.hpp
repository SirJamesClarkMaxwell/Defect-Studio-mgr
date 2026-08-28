#pragma once

#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererViewCamera.hpp"

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/StructureComparison.hpp"
#include "Renderer/Scene/SceneRegistry.hpp"

namespace DefectStudio
{
	struct RendererToolbarIconTexture
	{
		unsigned int rendererId = 0;
		int width = 0;
		int height = 0;
		bool loadAttempted = false;
	};

	struct RendererWindowState
	{
		std::string windowId;
		std::string title;
		RendererStructureData structure;
		// One entity per atom/bond, synced with `structure` by SceneSystem::SyncSceneWithStructure
		// (called on load/reload). Owns SelectionComponent/VisibilityComponent - the flat arrays
		// above (structure.atoms[i].visible, selectedAtomIndices below) stay the GPU-instanced
		// rendering hot path and are kept as a mirror via SceneSystem::PushSelectionAndVisibilityToWindowState.
		SceneRegistry sceneRegistry;
		Unique<RendererViewCamera> camera;
		glm::vec2 viewportSize = glm::vec2(640.0f, 480.0f);
		bool showGrid = true;
		bool showCellBox = true;
		bool showBonds = true;
		bool showAtoms = true;
		// Auto bond-length MSDF labels (Etap E) - toggled by `Alt+M`, off by default so existing
		// structures don't suddenly grow new clutter on every bond until a user opts in.
		bool showLabels = false;
		// Pinned bond/angle measurement labels (Etap E) - `M` pins the current selection's measurement
		// (2 atoms -> bond length, 3 atoms -> angle, vertex resolved the same way as the live gizmo
		// drag-following recalc: whichever of the three is bonded to both others). Add-only: pressing
		// `M`/`Shift+M` again over a selection that already includes a pinned pair/triple leaves it
		// alone rather than unpinning it - click a label to select it, then Delete to remove just that
		// one (see RendererPanel::handlePinnedMeasurementInteraction). Independent of live selection so
		// a pin survives deselecting the atoms - that's the whole point versus the old "shows only
		// while selected" behaviour. Not persisted with the project yet (see TODO.md T09 "Tryby
		// zaznaczania"). Real ECS entity as of Etap F (LabelComponent/TransformComponent/
		// SelectionComponent, synced by SceneSystem::SyncLabelEntities/UpdateLabelTransforms/
		// SyncLabelSelection) - gizmo-draggable (RendererPanel::renderLabelTransformGizmo: Translate
		// moves worldOffset along a world axis, Rotate/Scale drive rotationOffsetRadians/scale below
		// via a ring-drag around the pivot instead of per-axis handles, since a billboard label has
		// only one meaningful rotation axis - its own camera-facing normal - and one meaningful scale).
		// Shared styling for every label kind - free labels, pinned bond/angle labels, and (Phase 4)
		// arrow-attached labels - rendered through the same MSDF pipeline
		// (OpenGlRendererBackend::AppendLabelInstances/renderLabels), so one style struct instead of
		// hardcoded constants (kLabelColor) duplicated per label kind. `scale` folds in what used to
		// be a standalone field on PinnedMeasurement/FreeLabel - one style path, not three.
		struct LabelStyle
		{
			glm::vec3 textColor = glm::vec3(0.92f, 0.92f, 0.85f); // today's kLabelColor
			float textAlpha = 1.0f;
			glm::vec3 backgroundColor = glm::vec3(0.0f);
			float backgroundAlpha = 0.0f; // 0 = no background quad drawn
			// Border around the background quad's own edge (label_background.frag's rounded-rect SDF)
			// - not a glyph stroke (see strokeColor/strokeWidth below). World units, same space as
			// padding/cornerRadius; 0 = no border.
			glm::vec3 outlineColor = glm::vec3(0.0f);
			float outlineWidth = 0.0f;
			float cornerRadius = 0.0f;    // world units, background quad corner rounding; 0 = sharp
			glm::vec2 padding = glm::vec2(0.05f); // world units, background bbox margin, local x/y independent
			// MSDF stroke around the glyph itself (labels.frag), independent of the background border
			// above. Screen pixels, NOT world units like outlineWidth/padding/cornerRadius and NOT
			// normalized SDF units either - a fixed on-screen thickness regardless of zoom or glyph
			// size (a median-space width would shrink to sub-pixel invisibility on a small on-screen
			// label, since the SDF's own screenPxRange scales with rendered glyph size). ~1.5-4px is a
			// typical visible range; 0 = no stroke.
			glm::vec3 strokeColor = glm::vec3(0.0f);
			float strokeWidth = 0.0f;
			float scale = 1.0f;
		};

		struct PinnedMeasurement
		{
			std::vector<std::size_t> atomIndices; // size 2 = bond length, size 3 = angle
			// Free 3D world-space nudge from the resolved anchor (bond midpoint / angle vertex) - was a
			// camera-plane-only vec2 before Etap F, widened to a full vec3 so the gizmo can also push a
			// label along the view axis, not just pan it across the screen. The old click-drag-on-label
			// path (RendererPanel::handlePinnedMeasurementInteraction) still writes into this via the
			// same camera-right/up projection as before; the gizmo writes along whichever world axis is
			// grabbed. Both share this one field, no separate representation to keep in sync.
			glm::vec3 worldOffset = glm::vec3(0.0f);
			// Bond-length pins only (size 2): rotate the label to read along the bond's own
			// direction instead of always staying upright. flipped adds 180 degrees on top, for when
			// the aligned reading direction is upside-down from the current camera angle.
			bool alignToBondDirection = true;
			bool flipped = false;
			// Extra in-plane billboard rotation on top of alignToBondDirection/flipped (bond pins) or
			// the always-upright default (angle pins) - gizmo Rotate (RendererPanel::
			// renderLabelTransformGizmo), added to the computed rotationRadians in
			// OpenGlRendererBackend::renderLabels.
			float rotationOffsetRadians = 0.0f;
			// Text color/alpha/outline/background/padding, plus the uniform glyph-size multiplier
			// (style.scale - gizmo Scale, RendererPanel::renderLabelTransformGizmo, applied in
			// OpenGlRendererBackend::AppendLabelInstances around the label's own anchor so it grows/
			// shrinks in place, not toward world origin). Editable from ObjectPropertiesPanel's
			// "Pinned measurement" section.
			LabelStyle style;
			// Bond-length pins only (size 2): the specific RendererBondData::secondAtomPeriodicOffset
			// this pin was created from (zero for a direct/non-periodic bond), relative to
			// atomIndices[0]->atomIndices[1] after the identity sort below. Two atoms can be joined by
			// more than one real bond across different periodic images (e.g. neighbors in both +a and
			// -a directions) - without this, every such bond collapsed onto the same {atomA, atomB}
			// identity and toggling one on/off during a bulk pin could silently cancel the other out,
			// or the label would render at whichever bond the lookup happened to find first regardless
			// of which one was actually pinned.
			glm::vec3 bondPeriodicOffset = glm::vec3(0.0f);
		};
		std::vector<PinnedMeasurement> pinnedMeasurements;
		// Free-floating annotation label (ObjectPropertiesPanel "Free labels" section) - arbitrary
		// user text at an arbitrary world position, for figure-prep call-outs that aren't tied to any
		// bond/angle the way PinnedMeasurement is. Renders through the same MSDF label pipeline
		// (OpenGlRendererBackend::renderLabels -> AppendLabelInstances, which already takes a plain
		// std::string - bond/angle labels are just this same function fed a formatted number).
		// Reposition via typed X/Y/Z in ObjectPropertiesPanel, click-drag in the viewport
		// (selectedFreeLabels/RendererPanel::handleFreeLabelInteraction below), or the same
		// Translate/Rotate/Scale gizmo PinnedMeasurement gets (RendererPanel::
		// renderLabelTransformGizmo treats the two kinds interchangeably). Participates in the shared
		// label undo stack (RendererWindowState::LabelUndoSnapshot) the same way pins do.
		struct FreeLabel
		{
			std::string text = "Label";
			glm::vec3 worldPosition = glm::vec3(0.0f);
			float rotationRadians = 0.0f;
			LabelStyle style;
		};
		std::vector<FreeLabel> freeLabels;
		// Click-select + drag-to-move for freeLabels (RendererPanel::handleFreeLabelInteraction) - same
		// click-drag-along-camera-plane shape as PinnedMeasurement's worldOffset drag above, but
		// mutates worldPosition directly since a free label has no anchor to offset from. Multi-select,
		// same convention as selectedPinnedMeasurements above (back() is the drag/gizmo anchor).
		std::vector<std::size_t> selectedFreeLabels;
		bool freeLabelDragging = false;
		glm::vec2 freeLabelDragLastMouse = glm::vec2(0.0f);
		// Figure-annotation arrow (ObjectPropertiesPanel "Arrows" section) - a straight directional
		// line from start to end, for pointing at a displacement/direction in an export shot.
		// Line: shaft only (bond cylinder mesh/shader, reused as-is). Arrow3D: shaft + a cone head
		// (OpenGlRendererBackend::createConeMesh, "bonds" program reused - bonds.vert is a generic
		// model-transform shader, not cylinder-specific). Arrow2D: a flat quad instead of a shaft,
		// either camera-facing (Billboard) or lying flat in a chosen world plane (FixedPlane) - see
		// OpenGlRendererBackend::renderSceneArrows/ComputeArrowQuadBasis. Renderer-only like
		// FreeLabel/PinnedMeasurement, not persisted with the project yet. Gizmo/attached
		// label/undo for arrows are a later phase - labels already have all three
		// (renderLabelTransformGizmo/AttachedLabel/pinnedMeasurementUndoHistory), arrows don't yet.
		enum class ArrowKind { Line, Arrow2D, Arrow3D };
		enum class Arrow2DOrientation { Billboard, FixedPlane };
		enum class WorldPlane { XY, XZ, YZ };

		struct ArrowStyle
		{
			glm::vec3 color = glm::vec3(0.95f, 0.75f, 0.1f);
			float alpha = 1.0f;
			float shaftWidth = 0.06f; // radius; was the old hardcoded kArrowShaftRadius
			glm::vec3 outlineColor = glm::vec3(0.0f);
			float outlineWidth = 0.0f;
			float headWidth = 0.14f;  // Arrow3D cone base diameter / Arrow2D has no head, unused there
			float headLength = 0.22f; // Arrow3D cone height, unused for Line/Arrow2D
		};

		struct SceneArrow
		{
			ArrowKind kind = ArrowKind::Arrow3D;
			Arrow2DOrientation orientation2D = Arrow2DOrientation::Billboard;
			WorldPlane fixedPlane = WorldPlane::XY;
			glm::vec3 start = glm::vec3(0.0f);
			glm::vec3 end = glm::vec3(0.0f, 0.0f, 1.0f);
			ArrowStyle style;
		};
		std::vector<SceneArrow> sceneArrows;
		// Click-select + drag for sceneArrows (RendererPanel::handleSceneArrowInteraction) - same
		// multi-select/group-drag shape as selectedFreeLabels above, plus which endpoint a single
		// selected arrow's drag actually grabs (irrelevant once more than one is selected - a
		// multi-selection always moves every selected arrow's start AND end together, same rigid
		// group-drag convention as labels).
		std::vector<std::size_t> selectedSceneArrows;
		bool sceneArrowDragging = false;
		glm::vec2 sceneArrowDragLastMouse = glm::vec2(0.0f);
		enum class SceneArrowDragTarget { Start, End, Both };
		SceneArrowDragTarget sceneArrowDragTarget = SceneArrowDragTarget::Both;
		// Drives the Blender-style "adjust last operation" quick-edit window (RendererPanel::
		// renderSceneArrowQuickEditPanel) - set right after an arrow is added via Shift+A/right-click
		// Add/ObjectPropertiesPanel's own "+ Add arrow"; cleared on Escape, on selection changing away
		// from this arrow, or when another arrow is added. Bool+index pair rather than
		// std::optional<std::size_t> - same convention as cursor3DPlaced/cursor3DPosition above, no
		// new include needed.
		bool sceneArrowQuickEditActive = false;
		std::size_t sceneArrowQuickEditIndex = 0;
		// Atoms-displacement comparison (T08 item 0 / T16 item 8) - this window is the "reference"
		// structure; comparisonFilePath is a second, differently-composed-or-not structure loaded
		// once (off the main thread, CompareStructuresJob) and matched against it. Unlike
		// sceneArrows this is auto-generated (hundreds-to-thousands of pairs, not a handful of
		// hand-placed annotations) and drawn as a single batched instanced draw call
		// (OpenGlRendererBackend::renderDisplacementArrows), not per-arrow welded meshes. Renderer-
		// only, like sceneArrows - the file path + threshold are the only two fields mirrored into
		// ProjectManifest (EditorLayer::onDisplacementComparisonStateChanged), the computed result
		// itself is not persisted and is recomputed by pressing "Compare" again after reopening a
		// project.
		struct DisplacementComparisonState
		{
			Path comparisonFilePath;
			// Live UI slider (DisplacementComparisonPanel) - hides matches whose magnitudeAngstrom is
			// ABOVE this value. Hungarian assignment is computed once (permissive cutoff in
			// BuildDisplacementCostMatrix); this only changes what's drawn, no recompute.
			float displayThresholdAngstrom = 0.0f;
			bool visible = true;
			// Skip arrows whose reference atom is hidden (RendererAtomData::visible), so H/Alt+H'ing
			// a species also hides its outgoing displacement arrows - see 2026-08-28 feedback.
			bool onlyForVisibleAtoms = true;
			StructureComparisonResult result;
			// Arrow color ramp (DisplacementComparisonPanel's "Arrow settings") - small/large
			// displacement endpoints of a linear gradient.
			glm::vec3 lowMagnitudeColor = glm::vec3(0.25f, 0.75f, 0.35f);
			glm::vec3 highMagnitudeColor = glm::vec3(0.85f, 0.2f, 0.2f);
			// true = ramp normalizes against the largest currently-visible match (rescales as the
			// threshold slider moves); false = ramp normalizes against a fixed, user-set ceiling
			// (fixedNormalizationMaxAngstrom) so colors stay stable while the slider moves.
			bool normalizeColorToVisibleMax = true;
			float fixedNormalizationMaxAngstrom = 0.5f;
		};
		std::optional<DisplacementComparisonState> displacementComparison;
		// One entry in the label undo/redo stack below - both label kinds together, since a single
		// logical edit (e.g. dragging the gizmo) only ever touches one kind but undo/redo needs to
		// restore the OTHER kind's vector too (it didn't change, so just copies through unchanged).
		// sceneArrows joined this same snapshot/stack for the same reason (see PushPinnedMeasurement
		// UndoSnapshot in RendererLayer.cpp) - one shared "labels" undo scope, not three parallel ones.
		struct LabelUndoSnapshot
		{
			std::vector<PinnedMeasurement> pinnedMeasurements;
			std::vector<FreeLabel> freeLabels;
			std::vector<SceneArrow> sceneArrows;
		};
		// Local per-window undo/redo for pinnedMeasurements AND freeLabels together (Ctrl+Alt+U /
		// Ctrl+Alt+Shift+U) - snapshot-based (whole-vector copies, cheap given how few labels there
		// typically are), pushed by PushPinnedMeasurementUndoSnapshot before every add/remove/flip/
		// drag-start on either kind, one entry per logical edit - the two kinds share this one stack the
		// same way they now share renderLabelTransformGizmo, rather than FreeLabel having no undo of
		// its own. Separate from the global Core/Undo Ctrl+Z stack and from viewUndoHistory below - see
		// RendererEvents::Viewport::UndoLabelsRequested for why.
		std::vector<LabelUndoSnapshot> pinnedMeasurementUndoHistory;
		std::vector<LabelUndoSnapshot> pinnedMeasurementRedoHistory;
			// Applies to every bond-length pin (new and already-pinned) - toggled in bulk by
			// `A` (see RendererLayer::onLabelsToggleBondAlignmentRequested), not per-pin like
			// `flipped` above.
			bool bondLabelsAlignToDirection = true;
		// Multi-select (Ctrl-click/box/circle-select add to this the same way selectedAtomIndices
		// below works for atoms) - primarily so ObjectPropertiesPanel can bulk-edit style across
		// several pins/free labels at once. Gizmo/keyboard-shortcut code still needs "the" pivot/anchor
		// for drag math - by convention that's selectedPinnedMeasurements.back() (the most recently
		// added/clicked one), same "last clicked is primary" convention selectedAtomIndices doesn't
		// need since atoms don't have their own gizmo picking a single representative.
		std::vector<std::size_t> selectedPinnedMeasurements;
		bool pinnedMeasurementDragging = false;
		glm::vec2 pinnedMeasurementDragLastMouse = glm::vec2(0.0f);
		std::vector<std::size_t> selectedAtomIndices;
		// Mirrors selectedAtomIndices but for BondComponent entities (added Etap T08.6 alongside
		// SelectionComponent/VisibilityComponent on bonds) - populated by SceneSystem::
		// PushSelectionAndVisibilityToWindowState, read by bond-delete/connect commands and the
		// bond-pick highlight in OpenGlRendererBackend::renderBonds.
		std::vector<std::size_t> selectedBondIndices;
		float rotationStepDeg = 1.0f;
		float pixelStepPx = 10.0f;
		float percentStep = 10.0f;
		bool dragActive = false;
		bool lastFocusedState = false;
		bool transitionActive = false;
		float transitionElapsed = 0.0f;
		float transitionDuration = 0.14f;
		glm::vec3 transitionStartTarget = glm::vec3(0.0f);
		glm::vec3 transitionEndTarget = glm::vec3(0.0f);
		float transitionStartDistance = 0.0f;
		float transitionEndDistance = 0.0f;
		float transitionStartYaw = 0.0f;
		float transitionEndYaw = 0.0f;
		float transitionStartPitch = 0.0f;
		float transitionEndPitch = 0.0f;
		float transitionStartRoll = 0.0f;
		float transitionEndRoll = 0.0f;
		glm::quat transitionStartOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::quat transitionEndOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		std::string transitionSourceAction;
		std::vector<RendererViewStateChange> viewUndoHistory;
		std::vector<RendererViewStateChange> viewRedoHistory;
		bool viewInteractionActive = false;
		std::string viewInteractionSource;
		RendererViewSnapshot viewInteractionStart;
		// Ctrl+1..4 selection-mode mask (T08 item 7) - which entity kinds handleViewportPick/
		// handleAtomPick/handlePinnedMeasurementInteraction can target with a click. Independent of
		// the show*/visibility flags above (those hide things from view; these gate what a click can
		// select once shown). Ctrl+1 atoms-only, Ctrl+2 +bonds, Ctrl+3 bonds+labels (no atoms - lets
		// a label be gizmo-dragged without risking an accidental atom drag), Ctrl+4 everything.
		// sceneArrows share this same flag rather than getting a pickArrows of their own - one more
		// "label-like annotation" kind under the same umbrella, not a new axis of selection-mode UI.
		bool pickAtoms = true;
		bool pickBonds = true;
		bool pickLabels = true;
		// Set by RendererLayer::onAddAtomPopupToggleRequested (Shift+A), read and cleared by
		// RendererPanel's per-window Render loop, which forwards it into its own popup-request
		// members (drawAddAtomPopup can only be triggered from within RendererPanel itself).
		bool addAtomPopupRequested = false;
		// Box/circle drag-select (Alt+B / Alt+C). Coordinates are viewport-relative pixels, same
		// space as RendererPanel::handleAtomPick's relX/relY.
		SelectionToolMode activeSelectionTool = SelectionToolMode::None;
		bool selectionDragActive = false;
		// Viewport transform gizmo (G/R/S). Rendered whenever selectedAtomIndices is non-empty -
		// see RendererPanel::renderTransformGizmo. Pivot is recomputed as the selection's live
		// centroid every frame rather than cached, so no start-of-drag snapshot is kept here: the
		// domain "before" state for Undo is captured by the commit command itself on drag release.
		GizmoOperation gizmoOperation = GizmoOperation::Translate;
		bool gizmoDragActive = false;
		// Fallback screen-space axis pick/drag - see RendererPanel::renderTransformGizmo. ImGuizmo's
		// own IsOver()/IsUsing() picking has proven unreliable in this app, so this hand-rolled path
		// (ported from an earlier iteration of this project that hit the same problem) is the drag
		// mechanism that actually runs in practice, not a rare-case backup.
		bool fallbackGizmoDragging = false;
		int fallbackGizmoAxis = -1;
		// Blender-style X/Y/Z axis lock during an active fallback drag - overrides fallbackGizmoAxis
		// while held; -1 means no override (drag follows the originally-grabbed axis).
		int fallbackAxisLockOverride = -1;
		glm::vec2 fallbackDragAxisScreenDir = glm::vec2(1.0f, 0.0f);
		glm::vec3 fallbackDragAxisWorldDir = glm::vec3(1.0f, 0.0f, 0.0f);
		float fallbackDragPixelsPerWorld = 1.0f;
		glm::vec2 fallbackLastMousePos = glm::vec2(0.0f);
		// True when the current fallback drag was started by pressing X/Y/Z with no mouse button
		// held (Blender-style modal move/scale) rather than by clicking a handle. A modal drag
		// applies its delta every frame regardless of mouse-button state, confirms on left-click and
		// cancels (reverting to fallbackDragStartPositions) on right-click/Escape - a click-drag
		// instead keeps applying only while the button is held and always commits on release.
		bool fallbackModalDrag = false;
		// Snapshot of selected atoms' cartesian positions taken when ANY fallback drag starts (both
		// click and modal) - the only way to revert on cancel, since the live drag mutates
		// windowState.structure directly before anything is committed to the domain.
		std::vector<glm::vec3> fallbackDragStartPositions;
		// Blender-style numeric override: while a locked-axis fallback drag is active, typed digits
		// accumulate here and replace the mouse-driven delta with an exact typed value (applied from
		// fallbackDragStartPositions, absolute rather than incremental) - Enter confirms, Backspace
		// edits, Escape/right-click cancels same as any other drag. Empty means "no override, follow
		// the mouse" (the pre-existing behavior). Never set during the free trackball rotate
		// (fallbackGizmoAxis == -2), which has no single axis for a typed number to mean anything.
		std::string fallbackNumericInput;
		// Gizmo for the current label selection (RendererPanel::renderLabelTransformGizmo) - same
		// click-a-handle-and-drag shape as the fallback atom gizmo above but its own state, since it
		// drags PinnedMeasurement::worldOffset/FreeLabel::worldPosition fields rather than atom
		// positions. No axis-lock override (X/Y/Z mid-drag re-pick) - not worth the extra state atoms'
		// version justifies - but DOES have the Blender-style modal start (X/Y/Z with no mouse button
		// held, translate only) via labelGizmoModalDrag below, same convention as the atom gizmo's
		// fallbackModalDrag.
		bool labelGizmoDragging = false;
		// True when this drag was started by pressing X/Y/Z with no mouse button held (modal - follows
		// the mouse every frame regardless of button state, confirms on left-click, cancels on
		// right-click/Escape) rather than by clicking a handle (click-drag - follows only while the
		// button is held, commits on release). See the atom gizmo's fallbackModalDrag for the same
		// distinction; no numeric-typed-value entry here though, unlike that one.
		bool labelGizmoModalDrag = false;
		int labelGizmoAxis = -1;
		glm::vec2 labelGizmoLastMousePos = glm::vec2(0.0f);
		glm::vec2 labelGizmoDragAxisScreenDir = glm::vec2(1.0f, 0.0f);
		glm::vec3 labelGizmoDragAxisWorldDir = glm::vec3(1.0f, 0.0f, 0.0f);
		float labelGizmoDragPixelsPerWorld = 1.0f;
		// One entry per label in the selection at drag-start (mixing pins and free labels is fine -
		// isPin picks which vector `index` refers to). Translate/Rotate apply their delta to every
		// target's live field incrementally each frame (same shape as the old single-select code just
		// looped); Scale recomputes from startScale * radial-ratio every frame, so needs the frozen
		// start value same as before. All three also feed Escape/right-click cancel (revert every
		// target to its start* value).
		struct LabelGizmoDragTarget
		{
			bool isPin = false;
			std::size_t index = 0;
			glm::vec3 startPosition = glm::vec3(0.0f);
			float startRotation = 0.0f;
			float startScale = 1.0f;
		};
		std::vector<LabelGizmoDragTarget> labelGizmoDragTargets;
		float labelGizmoDragStartRadial = 0.0f;
		// Gizmo for the current SceneArrow selection (RendererPanel::renderSceneArrowTransformGizmo) -
		// same shape as the label gizmo above (own state, no ICommand/UndoStack, PushPinnedMeasurement-
		// UndoSnapshot on drag start), except Translate can target a SINGLE endpoint instead of always
		// moving the whole item: exactly one arrow selected draws three translate pick points (Start,
		// End, and the midpoint for a rigid whole-arrow move); more than one selected only draws the
		// group-centroid pivot (every selected arrow's both endpoints move together, matching the
		// existing raw-drag system's "multi-selection is always rigid" rule - see sceneArrowDragTarget
		// above). sceneArrowGizmoEndpointTarget records which of the three was actually grabbed so the
		// active drag (and its cancel-revert) knows which field(s) to touch without re-hit-testing every
		// frame. Reuses SceneArrowDragTarget (Start/End/Both, declared above for the raw-drag system) -
		// same three-way meaning, no need for a second identical enum.
		bool sceneArrowGizmoDragging = false;
		bool sceneArrowGizmoModalDrag = false;
		int sceneArrowGizmoAxis = -1;
		SceneArrowDragTarget sceneArrowGizmoEndpointTarget = SceneArrowDragTarget::Both;
		glm::vec2 sceneArrowGizmoLastMousePos = glm::vec2(0.0f);
		glm::vec2 sceneArrowGizmoDragAxisScreenDir = glm::vec2(1.0f, 0.0f);
		glm::vec3 sceneArrowGizmoDragAxisWorldDir = glm::vec3(1.0f, 0.0f, 0.0f);
		float sceneArrowGizmoDragPixelsPerWorld = 1.0f;
		// One entry per selected arrow at drag-start - Translate(Both)/Rotate apply their delta to every
		// target's start/end incrementally each frame; Scale recomputes shaftWidth/headWidth/headLength
		// from the frozen start* value * radial-ratio every frame (same reasoning as the label gizmo's
		// LabelGizmoDragTarget::startScale). All also feed Escape/right-click cancel.
		struct SceneArrowGizmoDragTarget
		{
			std::size_t index = 0;
			glm::vec3 startPosition = glm::vec3(0.0f);
			glm::vec3 endPosition = glm::vec3(0.0f);
			float startShaftWidth = 0.0f;
			float startHeadWidth = 0.0f;
			float startHeadLength = 0.0f;
		};
		std::vector<SceneArrowGizmoDragTarget> sceneArrowGizmoDragTargets;
		float sceneArrowGizmoDragStartRadial = 0.0f;
		// Rotate-only pivot choice (Settings has no bearing here - this is a live per-window toggle, same
		// tier as gizmoOperation itself): Midpoint rotates around the live centroid of the selection
		// (default, matches the label gizmo's rotate); Cursor3D rotates around windowState.cursor3DPosition
		// instead, letting the user stage the cursor at an arrow's own start/end (viewport context menu's
		// "3D Cursor > Move to Arrow Start/End") for an off-center pivot. Scale has no pivot concept
		// (thickness-only, never touches position) so this toggle doesn't affect it.
		enum class ArrowGizmoPivotMode
		{
			Midpoint,
			Cursor3D
		};
		ArrowGizmoPivotMode sceneArrowGizmoPivotMode = ArrowGizmoPivotMode::Midpoint;
		// Which Start/End/midpoint candidate currently owns the drawn/hit-tested axis-triad on a single
		// selected arrow (renderSceneArrowTransformGizmo) - the other two candidates render as plain
		// click-to-activate dots instead of also drawing their own triad, so only one gizmo is ever
		// visible/interactive at a time. Reset to Both (the whole-arrow midpoint) whenever
		// sceneArrowGizmoActiveArrowIndex no longer matches the current single-arrow selection.
		SceneArrowDragTarget sceneArrowGizmoActiveTarget = SceneArrowDragTarget::Both;
		std::size_t sceneArrowGizmoActiveArrowIndex = static_cast<std::size_t>(-1);
		// Continuous Ctrl+Shift+Arrow nudge - polled every frame (RendererPanel::applyViewportInputNavigation)
		// instead of riding GLFW's own key-repeat cadence, which is OS-repeat-rate limited (~10-15Hz)
		// and visibly steps rather than glides. Same start-snapshot/commit-on-release shape as the
		// fallback gizmo drag above, just keyboard-driven instead of mouse-driven: one undo entry per
		// hold, not one per OS repeat tick.
		bool continuousNudgeActive = false;
		std::vector<glm::vec3> continuousNudgeStartPositions;
		// Continuous Alt+Shift+Arrow pan (RendererPanel::applyContinuousPan) - same per-frame-poll
		// shape as the nudge above, brackets a single BeginViewInteraction/CommitViewInteraction pair
		// per hold so the whole glide is one view-undo entry instead of one per frame.
		bool continuousPanActive = false;
		glm::vec2 selectionDragStart = glm::vec2(0.0f);
		glm::vec2 selectionDragCurrent = glm::vec2(0.0f);
		// Circle-select brush radius in viewport pixels - persistent per window, adjusted with the
		// mouse wheel while the circle tool is active (scroll up = bigger, down = smaller).
		float circleSelectRadius = 48.0f;
		// 3D cursor (vertical toolbar "3D point" tool + right-click context menu). Renderer-only,
		// like pinnedMeasurements - not a domain concept, not persisted with the project yet.
		glm::vec3 cursor3DPosition = glm::vec3(0.0f);
		bool cursor3DPlaced = false;
		// Non-destructive whole-scene reposition for framing an export shot (Etap F Phase 1) -
		// forwarded as a render-time uniform (u_SceneOffset) to every geometry pass (atoms/bonds/
		// cell box/grid/labels/isosurface, see OpenGlRendererBackend::RenderWindow), never baked
		// into any CPU-side position - so it never touches the domain CrystalStructure/undo stack,
		// picking/gizmo/measurements, or the per-frame dirty-cache checks. Set only by
		// ExportImagePanel's "Object offset" control, acting on the export dialog's own
		// RendererWindowState (RenderExportDialogState::previewState) - always 0 on a real viewport
		// window's RendererWindowState, since nothing in the interactive viewport writes it anymore
		// (this replaced an earlier v1 that mutated atom.cartesianPosition directly and only worked
		// for atoms/bonds/picking - cell box/grid/scene arrows never got that mutation, so they'd
		// visibly detach; see RendererLayer::RenderToFbo for how this value reaches RenderWindow).
		glm::vec3 viewOffset = glm::vec3(0.0f);
		// GPU compute-shader isosurface mesh for one spin channel's rendered orbital
		// (ElectronicStructurePanel, via RendererLayer::RegenerateOrbitalIsosurface). Two
		// independent channels so spin-up and spin-down can be shown simultaneously - `enabled`
		// gates drawing (data/vertexCount can stay populated while temporarily hidden).
		struct OrbitalOverlayChannel
		{
			bool enabled = false;
			int vertexCount = 0;
			// Positive = red, negative = blue - same convention for both spin channels.
			glm::vec3 positiveLobeColor = glm::vec3(1.0f, 0.0f, 0.0f);
			glm::vec3 negativeLobeColor = glm::vec3(0.0f, 0.0f, 1.0f);
			float lobeAlpha = 0.6f;
		};
		OrbitalOverlayChannel orbitalChannelUp;
		OrbitalOverlayChannel orbitalChannelDown;

		// One-shot: on this window's very first Begin(), RendererPanel docks it into the
		// dockspace's central node (ImGuiCond_FirstUseEver) instead of opening free-floating -
		// never reapplied afterwards, so a later manual re-dock by the user sticks.
		bool dockingInitialized = false;
	};

	// T15-lite export dialog: resolution preset + filename proposed from the structure's source
	// path + pan-to-reframe preview, rendered at the target aspect ratio so the exported image is
	// never stretched (aspect ratio is correct by construction - "crop" here means reframing via
	// pan, not a post-render pixel crop). One dialog instance application-wide (not per-window).
	struct RenderExportDialogState
	{
		enum class ResolutionPreset
		{
			FullHd1080p,
			QuadHd2K,
			UltraHd4K,
			Custom
		};

		bool open = false;
		std::string targetWindowId;
		std::string filename;
		Path saveDirectory = Path("exports");
		ResolutionPreset preset = ResolutionPreset::FullHd1080p;
		int customWidth = 1920;
		int customHeight = 1080;
		// Fractions (0..1) trimmed from each edge on export - a real pixel crop (changes the
		// output aspect ratio), independent of the pan/zoom reframing above.
		float cropLeft = 0.0f;
		float cropRight = 0.0f;
		float cropTop = 0.0f;
		float cropBottom = 0.0f;
		// Off = export uses the live viewport's own background (RendererGlobalRenderSettings::
		// backgroundColor), same as every other render. On = ExportImagePanel builds its own copy of
		// the global settings with backgroundColor replaced by this one, just for the export/preview
		// FBO passes - the live viewport and other windows are untouched.
		bool useCustomBackground = false;
		glm::vec4 backgroundColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		// 1 = native WAVECAR grid (matches the interactive view). 2/3 = trilinear-upsample the grid
		// before meshing (see UpsampleOrbitalGrid) for a smoother-looking lobe in the export/batch-
		// export image - export-only, since upsampling costs real CPU time per band and the
		// interactive view needs to stay responsive while scrubbing.
		int orbitalSupersample = 1;
		// Owns its own camera (copied from the target window's live camera when the dialog opens,
		// then mutated in place each frame by the preset/pan controls) - kept separate from the
		// canonical RendererWindowState list, never touches the live window's own camera/state.
		RendererWindowState previewState;

		// Orbital overlay in the export image (see ExportImagePanel::renderOrbitalExportControls) -
		// previewState.orbitalChannelUp/Down.enabled ARE the "show orbitals" toggles, no separate
		// flag needed; these two are just the batch-specific extras.
		std::vector<int> selectedOrbitalBands; // checked rows in the export dialog's orbital table
		struct OrbitalBatchExportState
		{
			bool running = false;
			std::vector<int> pendingBands; // consumed front-to-back
			int totalCount = 0;
			int completedCount = 0;
			int skippedCount = 0; // bands whose grid fetch failed - logged, not fatal to the rest
		};
		OrbitalBatchExportState orbitalBatchExport;
	};
} // namespace DefectStudio
