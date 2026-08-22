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
		// while selected" behaviour. Not a full ECS entity yet and not persisted with the project yet
		// (see TODO.md T09 "Tryby zaznaczania") - screenOffset is a user-dragged nudge so overlapping
		// labels can be separated, no 3D gizmo integration.
		struct PinnedMeasurement
		{
			std::vector<std::size_t> atomIndices; // size 2 = bond length, size 3 = angle
			glm::vec2 screenOffset = glm::vec2(0.0f); // camera-right/up world-space nudge
			// Bond-length pins only (size 2): rotate the label to read along the bond's own
			// direction instead of always staying upright. flipped adds 180 degrees on top, for when
			// the aligned reading direction is upside-down from the current camera angle.
			bool alignToBondDirection = true;
			bool flipped = false;
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
			// Applies to every bond-length pin (new and already-pinned) - toggled in bulk by
			// `A` (see RendererLayer::onLabelsToggleBondAlignmentRequested), not per-pin like
			// `flipped` above.
			bool bondLabelsAlignToDirection = true;
		int selectedPinnedMeasurement = -1;
		bool pinnedMeasurementDragging = false;
		glm::vec2 pinnedMeasurementDragLastMouse = glm::vec2(0.0f);
		std::vector<std::size_t> selectedAtomIndices;
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
