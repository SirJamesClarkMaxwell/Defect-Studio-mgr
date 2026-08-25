#pragma once

#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererViewCamera.hpp"

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
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
			// Uniform multiplier on the label's rendered glyph size - gizmo Scale (RendererPanel::
			// renderLabelTransformGizmo), applied in OpenGlRendererBackend::AppendLabelInstances around
			// the label's own anchor (so it grows/shrinks in place, not toward world origin).
			float scale = 1.0f;
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
		// v1: reposition via typed X/Y/Z, no gizmo drag yet - see ObjectPropertiesPanel. Renderer-
		// only like PinnedMeasurement/cursor3D, not persisted with the project yet.
		struct FreeLabel
		{
			std::string text = "Label";
			glm::vec3 worldPosition = glm::vec3(0.0f);
			float scale = 1.0f;
			float rotationRadians = 0.0f;
		};
		std::vector<FreeLabel> freeLabels;
		// Figure-annotation arrow (ObjectPropertiesPanel "Arrows" section) - a straight directional
		// line from start to end, for pointing at a displacement/direction in an export shot.
		// ponytail: shaft only, no arrowhead cone yet (would need a new procedural mesh + GPU
		// pipeline - reused the existing bond cylinder mesh/shader instead to ship this without new,
		// unverified geometry code). Add a small cone mesh + OpenGlRendererBackend::
		// renderSceneArrows head pass when the plain shaft isn't legible enough. Renderer-only like
		// FreeLabel/PinnedMeasurement, not persisted with the project yet.
		struct SceneArrow
		{
			glm::vec3 start = glm::vec3(0.0f);
			glm::vec3 end = glm::vec3(0.0f, 0.0f, 1.0f);
			glm::vec3 color = glm::vec3(0.95f, 0.75f, 0.1f);
		};
		std::vector<SceneArrow> sceneArrows;
		// Local per-window undo/redo for pinnedMeasurements (Ctrl+Alt+U / Ctrl+Alt+Shift+U) - snapshot-
		// based (whole-vector copies, cheap given how few pins there typically are), pushed by
		// PushPinnedMeasurementUndoSnapshot before every add/remove/flip/drag-start, one entry per
		// logical edit. Separate from the global Core/Undo Ctrl+Z stack and from viewUndoHistory below
		// - see RendererEvents::Viewport::UndoLabelsRequested for why.
		std::vector<std::vector<PinnedMeasurement>> pinnedMeasurementUndoHistory;
		std::vector<std::vector<PinnedMeasurement>> pinnedMeasurementRedoHistory;
			// Applies to every bond-length pin (new and already-pinned) - toggled in bulk by
			// `A` (see RendererLayer::onLabelsToggleBondAlignmentRequested), not per-pin like
			// `flipped` above.
			bool bondLabelsAlignToDirection = true;
		int selectedPinnedMeasurement = -1;
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
		// Translate-only gizmo for the selected pinned measurement label (RendererPanel::
		// renderLabelTransformGizmo) - same click-a-handle-and-drag shape as the fallback atom gizmo
		// above but its own state, since it drags a single PinnedMeasurement::worldOffset rather than
		// a list of atom positions. No modal (keypress-only) variant and no axis-lock override - one
		// point, not worth the extra state atoms' multi-select version justifies.
		bool labelGizmoDragging = false;
		int labelGizmoAxis = -1;
		glm::vec2 labelGizmoLastMousePos = glm::vec2(0.0f);
		glm::vec2 labelGizmoDragAxisScreenDir = glm::vec2(1.0f, 0.0f);
		glm::vec3 labelGizmoDragAxisWorldDir = glm::vec3(1.0f, 0.0f, 0.0f);
		float labelGizmoDragPixelsPerWorld = 1.0f;
		glm::vec3 labelGizmoDragStartOffset = glm::vec3(0.0f);
		// Rotate/Scale ring-drag start snapshot (labelGizmoAxis == -2, no X/Y/Z handle - see
		// renderLabelTransformGizmo) - used only for Escape/right-click cancel; the live update itself
		// is incremental (Rotate) or ratio-of-radial-distance (Scale), not computed from these.
		float labelGizmoDragStartRotation = 0.0f;
		float labelGizmoDragStartScale = 1.0f;
		float labelGizmoDragStartRadial = 0.0f;
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
		// Non-destructive whole-structure reposition for framing a shot (viewport toolbar "Object
		// offset" field) - added directly to every atom's cartesianPosition (same field the gizmo
		// drag/nudge already mutate live, see RendererPanelInput.cpp), so it's picked up for free by
		// picking/gizmo/measurements/export (they all read this same RendererStructureData) and by
		// the per-frame position-hash dirty check (OpenGlRendererBackend::RenderWindow) with zero
		// extra renderer code. Never touches the domain CrystalStructure/undo stack. Only tracked
		// here so the UI knows the current value and can compute a delta on the next edit; wiped out
		// (silently, by design - "view only") by the next RebuildAndSync (any real atom edit, undo/
		// redo, or reload), which rebuilds structure fresh from the untouched domain. The isosurface
		// overlay doesn't share this struct (separate WAVECAR-grid pipeline) so it gets the same
		// value via its own u_SceneOffset uniform instead - see RendererLayer::RenderToFbo.
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
