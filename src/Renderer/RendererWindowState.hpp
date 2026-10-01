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
#include "Domain/DomainIds.hpp"
#include "Domain/Electronic/HydrogenicOrbital.hpp"
#include "Renderer/Path/PathEditSession.hpp"
#include "Renderer/Path/PathStore.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/Scene/ModalTransform.hpp"
#include "Renderer/Scene/SceneRegistry.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

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
		// Bonds that cross a periodic cell boundary (Bond::periodicShift) run to an image atom that is
		// not drawn, so they read as stubs poking out of the cell. Correct, and the only way a 2D
		// sheet shows its edge connectivity - but noise on a supercell, hence the switch.
		bool showPeriodicBonds = true;
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
		// SyncLabelSelection) - the unified modal transform moves worldOffset, while Rotate/Scale
		// preserve the label-specific rotationOffsetRadians/style.scale meanings below.
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
			// Stable identity, allocated by SceneRegistry::AllocateObjectId at creation (task 20).
			// Survives resyncs, deletions of other objects and undo/redo snapshots - unlike the
			// object's position in the vector, which does not.
			SceneObjectId id;
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
			// Task 24 persistence: file-stable identity (32 hex chars, SceneObjectPersistence), shared by
			// every window's copy of the same saved object - unlike `id`, which is per window. A
			// duplicated object must get a new one (clear it; the next sync assigns it).
			std::string persistKey;
			// Set on load when the stored atom references no longer bind (SceneObjectPersistence::
			// AtomReferenceBinds). The pin then anchors at frozenAtomPositions, ignores atomIndices,
			// and ObjectPropertiesPanel shows the broken link.
			bool linkBroken = false;
			std::vector<glm::vec3> frozenAtomPositions; // same order/size as atomIndices, valid when linkBroken
			std::vector<std::string> frozenAtomElements;
			// The Scene Outliner's two columns, see Renderer/Scene/SceneVisibility.hpp. `visible` is
			// the eye (drawn in the viewport, what H toggles), `renderable` the camera (drawn in an
			// exported render). Independent on purpose.
			bool visible = true;
			bool renderable = true;
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
			// Stable identity, allocated by SceneRegistry::AllocateObjectId at creation (task 20).
			// Survives resyncs, deletions of other objects and undo/redo snapshots - unlike the
			// object's position in the vector, which does not.
			SceneObjectId id;
			std::string text = "Label";
			glm::vec3 worldPosition = glm::vec3(0.0f);
			float rotationRadians = 0.0f;
			LabelStyle style;
			std::string persistKey; // see PinnedMeasurement::persistKey
			// The Scene Outliner's two columns, see Renderer/Scene/SceneVisibility.hpp. `visible` is
			// the eye (drawn in the viewport, what H toggles), `renderable` the camera (drawn in an
			// exported render). Independent on purpose.
			bool visible = true;
			bool renderable = true;
		};
		std::vector<FreeLabel> freeLabels;
		// Click-select + drag-to-move for freeLabels (RendererPanel::handleFreeLabelInteraction) - same
		// click-drag-along-camera-plane shape as PinnedMeasurement's worldOffset drag above, but
		// mutates worldPosition directly since a free label has no anchor to offset from. Multi-select,
		// same convention as selectedPinnedMeasurements above (back() is the drag/gizmo anchor).
		std::vector<SceneObjectId> selectedFreeLabels;
		bool freeLabelDragging = false;
		glm::vec2 freeLabelDragLastMouse = glm::vec2(0.0f);
		// Figure-annotation arrow (ObjectPropertiesPanel "Arrows" section) - an ordered path with
		// independently styled tips, for pointing at a displacement/direction in an export shot.
		// Line and Arrow3D use the renderer path/tip mesh with the generic bond shader. Arrow2D remains
		// a flat quad instead of a path mesh,
		// either camera-facing (Billboard) or lying flat in a chosen world plane (FixedPlane) - see
		// OpenGlRendererBackend::renderSceneArrows/ComputeArrowQuadBasis. Renderer-only like
		// FreeLabel/PinnedMeasurement, persisted in scene_objects.yaml. Gizmo/attached
		// label/undo for arrows are a later phase - labels already have all three
		// (renderLabelTransformGizmo/AttachedLabel), arrows don't yet.
		enum class ArrowKind { Line, Arrow2D, Arrow3D };
		// Tip style is independent of ArrowKind and chosen separately for each end. This deliberately
		// small TikZ-inspired vocabulary is renderer data; its geometry parameters live in
		// Renderer/Scene/SceneArrowGeometry rather than in Domain or IO.
		enum class ArrowTip { None, Plain, Barbed, Open, Bar, Circle };
		enum class Arrow2DOrientation { Billboard, FixedPlane };
		enum class WorldPlane { XY, XZ, YZ };

		struct ArrowStyle
		{
			glm::vec3 color = glm::vec3(0.95f, 0.75f, 0.1f);
			// Two-stop colour ramp along the shaft, start -> end, reusing the same
			// RendererColorGradient that already drives structure bond colouring rather than
			// inventing a second gradient type. Off by default, so every arrow that existed
			// before keeps using the flat `color` above and nothing changes under it.
			bool useGradient = false;
			RendererColorGradient gradient;
			float alpha = 1.0f;
			float shaftWidth = 0.06f; // radius; was the old hardcoded kArrowShaftRadius
			bool dashed = false;
			float dashLength = 0.25f; // world units, along the shaft
			float gapLength = 0.15f;  // world units
			glm::vec3 outlineColor = glm::vec3(0.0f);
			float outlineWidth = 0.0f;
			// World units for Line/Arrow3D (32b-1: either can carry a tip, not just Arrow3D's cone -
			// see ArrowTip/GetArrowTipParameters), screen pixels for Arrow2D. Never reinterpret one
			// unit system as the other - ApplySceneArrowKindChange re-derives both on every switch
			// that crosses that boundary.
			float headWidth = 0.14f;
			float headLength = 0.22f;
		};

		struct SceneArrow
		{
			// Stable identity, allocated by SceneRegistry::AllocateObjectId at creation (task 20).
			// Survives resyncs, deletions of other objects and undo/redo snapshots - unlike the
			// object's position in the vector, which does not.
			SceneObjectId id;
			ArrowKind kind = ArrowKind::Arrow3D;
			Arrow2DOrientation orientation2D = Arrow2DOrientation::Billboard;
			WorldPlane fixedPlane = WorldPlane::XY;
			// Ordered world-space path. Two entries with no control point are the legacy straight arrow.
			// A single quadratic control point applies only to a two-point path; longer paths bend at
			// their explicit points.
			std::vector<glm::vec3> points = {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
			std::optional<glm::vec3> controlPoint;
			int curveSegments = 24;
			ArrowTip startTip = ArrowTip::None;
			ArrowTip endTip = ArrowTip::Plain;

			[[nodiscard]] glm::vec3 &start() { return points.front(); }
			[[nodiscard]] const glm::vec3 &start() const { return points.front(); }
			[[nodiscard]] glm::vec3 &end() { return points.back(); }
			[[nodiscard]] const glm::vec3 &end() const { return points.back(); }
			// Which atom each end follows. A missing optional is a free coordinate; a stale index
			// also leaves the stored coordinate untouched, matching the orbital/plane anchor rule.
			std::optional<std::size_t> startAnchorAtom;
			std::optional<std::size_t> endAnchorAtom;
			// Gap at each anchored end in that atom's own radii. 1.0 reaches the sphere surface.
			float atomBuffer = 1.15f;
			ArrowStyle style;
			std::string persistKey; // see PinnedMeasurement::persistKey
			// The Scene Outliner's two columns, see Renderer/Scene/SceneVisibility.hpp. `visible` is
			// the eye (drawn in the viewport, what H toggles), `renderable` the camera (drawn in an
			// exported render). Independent on purpose.
			bool visible = true;
			bool renderable = true;
		};
		std::vector<SceneArrow> sceneArrows;

		// A hydrogenic orbital drawn as a scene annotation (task 26): the user picks a preset -
		// s/p/d, an sp/sp2/sp3 hybrid lobe, or a sigma/pi/delta molecular orbital with its
		// antibonding partner - and it is evaluated analytically and meshed as an isosurface with
		// its two phases coloured separately. Nothing here is read from a calculation; this is a
		// drawing object, which is why it lives beside the labels and arrows rather than in the
		// electronic-structure session that owns WAVECAR orbitals.
		struct SceneOrbital
		{
			// Same stable identity as SceneArrow::id, from the same SceneRegistry.
			SceneObjectId id;
			OrbitalPreset preset = OrbitalPreset::P;
			// Fed straight to OrbitalPresetSettings - see HydrogenicOrbital.hpp for what each one
			// selects and how each is clamped.
			int shell = 2;
			int lobeIndex = 0;
			float effectiveCharge = 1.0f;
			glm::vec3 centerA = glm::vec3(0.0f);
			// Only read by the two-centre presets. For a single-centre one it is left alone rather
			// than hidden, so switching preset back and forth does not lose the bond the user set up.
			glm::vec3 centerB = glm::vec3(1.5f, 0.0f, 0.0f);
			// Optional atom anchoring, indices into structure.atoms: one entry drives centerA, two
			// drive centerA and centerB. Resolved every frame like SceneArrow's endpoint anchors, so an
			// orbital sits on its atom through gizmo drags, nudges and relaxation playback. Anchors
			// that no longer resolve are ignored, never indexed.
			std::vector<std::size_t> anchorAtoms;
			// Euler angles in degrees, applied to single-centre presets only - the two-centre ones
			// take their orientation from centerB - centerA. Degrees rather than a matrix so the
			// properties panel and the YAML both stay readable.
			glm::vec3 rotationEuler = glm::vec3(0.0f);
			// Scene-level sign decoration. BuildOrbitalWavefunction applies it after constructing the
			// physical preset, so the Domain preset builders remain an unmodified description of it.
			bool phaseFlipped = false;
			// Uniform mesh scale about the orbital's centroid, purely for composing a figure. It
			// does NOT change the physics - effectiveCharge is the knob that actually contracts or
			// expands the wavefunction. Kept separate so a drawing that was scaled to look right
			// next to an atom stays honest about which number is which.
			float scale = 1.0f;
			// Per-axis stretch in the orbital's own frame, on top of the uniform scale. x and y
			// span the lobe while z runs along it. This changes only the drawing, not the orbital
			// preset or the physical meaning of effectiveCharge and isoFraction.
			glm::vec3 stretch = glm::vec3(1.0f);
			// Iso value as a fraction of the sampled grid's peak amplitude (SuggestOrbitalIsoValue),
			// not an absolute value - a diffuse 3d and a tight 1s then both come out looking like
			// the textbook picture at the same setting.
			float isoFraction = 0.2f;
			// Samples per axis for the sampling cube. Meshing is CPU-side and runs on the main
			// thread whenever a parameter changes, so this is the frame-hitch knob.
			int resolution = 48;
			glm::vec3 positiveLobeColor = glm::vec3(0.85f, 0.25f, 0.25f);
			glm::vec3 negativeLobeColor = glm::vec3(0.25f, 0.35f, 0.9f);
			float alpha = 0.75f;
			// The outliner's eye column - see Renderer/Scene/SceneVisibility.hpp.
			bool visible = true;
			// ...and its camera column: drawn in an exported render. Independent of `visible`.
			bool renderable = true;
			std::string persistKey; // see PinnedMeasurement::persistKey
		};
		std::vector<SceneOrbital> sceneOrbitals;
		std::vector<SceneObjectId> selectedSceneOrbitals;

		// A flat quad drawn through a set of points - a molecular plane, a slip plane, a mirror
		// plane for the group-theory panel to point at. Like SceneArrow it is a drawing, not a
		// measurement: the points it was fitted through are consumed at creation and not kept, so
		// nothing here is linked to an atom and nothing has to be unlinked later.
		struct ScenePlane
		{
			// Same stable identity as SceneArrow::id, from the same SceneRegistry.
			SceneObjectId id;
			glm::vec3 center = glm::vec3(0.0f);
			// Unit normal. With `tangent` (unit, perpendicular to it) this fixes the quad's frame;
			// the second in-plane axis is the cross product, so there is no third vector to keep
			// consistent.
			glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
			glm::vec3 tangent = glm::vec3(1.0f, 0.0f, 0.0f);
			// Half-width along `tangent` and half-height along normal x tangent, Angstrom.
			glm::vec2 halfExtents = glm::vec2(2.0f);
			// Optional atom anchoring, indices into structure.atoms, same shape and lifetime rules
			// as SceneOrbital::anchorAtoms - but here the whole frame is re-fitted from the atoms
			// rather than a centre copied, so a plane through three atoms stays through them while
			// they move. Needs at least two resolvable entries; anything less is ignored. Empty
			// means a free plane, which is what "Odczep" leaves behind.
			std::vector<std::size_t> anchorAtoms;
			glm::vec3 color = glm::vec3(0.35f, 0.65f, 0.9f);
			float alpha = 0.35f;
			// Drawn on top of the outline of the quad. Off gives a plain translucent sheet.
			bool showBorder = true;
			// The Scene Outliner's two columns - see Renderer/Scene/SceneVisibility.hpp.
			bool visible = true;
			bool renderable = true;
			std::string persistKey; // see PinnedMeasurement::persistKey
		};
		std::vector<ScenePlane> scenePlanes;

		// task/41: the window's paths, and the caches derived from them. A Unique rather than a member
		// by value so that the vector of windows reallocating does not move the caches out from under
		// anything holding a reference into them; null until the first path is created (see
		// SceneSystem::EnsurePathSystem). No shipping UI reaches this before S15.
		Unique<PathSystem> paths;
		// Same multi-select shape as selectedSceneArrows; back() is the gizmo anchor.
		std::vector<SceneObjectId> selectedScenePlanes;
		// task/41 S11a: the selected paths, ids into `paths->Store()` rather than indices into a
		// vector - a path has no vector to index. Same multi-select shape as the four above, and it
		// must be cleared everywhere they are: a stale entry here shows the wrong Properties section.
		std::vector<SceneObjectId> selectedScenePaths;
		// Blender-like edit aid for validating the generated path mesh. Per viewport and deliberately
		// not persisted as object data.
		bool showPathMeshOverlay = true;
		// Edit Mode state for the one path opened from selectedScenePaths; element selection is kept
		// separate so leaving the session never changes Object Mode's path selection.
		PathEditSession pathEdit;
		// Set by renderer.path_edit.handle_type_menu (V); the viewport opens the handle-type popup on
		// its next frame and clears it. A command cannot call ImGui::OpenPopup itself - it runs outside
		// the viewport window's ImGui ID scope.
		bool pathHandleTypeMenuRequested = false;

		// Click-select + drag for sceneArrows (RendererPanel::handleSceneArrowInteraction) - same
		// multi-select/group-drag shape as selectedFreeLabels above, plus which endpoint a single
		// selected arrow's drag actually grabs (irrelevant once more than one is selected - a
		// multi-selection always moves every selected arrow's start AND end together, same rigid
		// group-drag convention as labels).
		std::vector<SceneObjectId> selectedSceneArrows;
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
		// Add > Orbital: whether a picked preset lands on the selected atoms or at the 3D cursor.
		// A sticky flag on the window rather than a level of submenu, which is what made that menu
		// six flyouts deep. Per-window and not persisted - it is a mode for the next click, not a
		// project setting.
		bool orbitalAddAnchorToSelection = true;
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
			// BuildLocalMatchingPlan); this only changes what's drawn, no recompute.
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
		// One entry in the scene-object undo snapshot - both label kinds together, since a single
		// logical edit (e.g. dragging the gizmo) only ever touches one kind but undo/redo needs to
		// restore the OTHER kind's vector too (it didn't change, so just copies through unchanged).
		// sceneArrows joined this same snapshot for the same reason - one shared scene-object scope.
		struct LabelUndoSnapshot
		{
			std::vector<PinnedMeasurement> pinnedMeasurements;
			std::vector<FreeLabel> freeLabels;
			std::vector<SceneArrow> sceneArrows;
			std::vector<SceneOrbital> sceneOrbitals;
			std::vector<ScenePlane> scenePlanes;
			// Paths join the same scope: one logical edit touches one kind, undo restores all of them.
			PathStore paths;
		};
			// Applies to every bond-length pin (new and already-pinned) - toggled in bulk by
			// `A` (see RendererLayer::onLabelsToggleBondAlignmentRequested), not per-pin like
			// `flipped` above.
			bool bondLabelsAlignToDirection = true;
			// Auto-offset (notes.txt pt. 7) - a bond-length pin's rendered position gets an extra
			// perpendicular-to-bond nudge toward the visible structure's centroid, computed live in
			// OpenGlRendererBackend::renderLabels, ADDED to (not replacing) the pin's own manual
			// worldOffset - dragging a pin still works exactly as before, just starting from an
			// already-offset base instead of dead-center on the bond. Per-window like
			// bondLabelsAlignToDirection above, not per-pin - per-pin correction is what worldOffset
			// already is.
			bool bondLabelAutoOffsetEnabled = true;
			float bondLabelAutoOffsetMagnitude = 0.3f; // world units (Angstrom)
			// Align-to-camera (notes.txt pt. 8) - "Align all above threshold" (ObjectPropertiesPanel)
			// flattens every bond-length pin whose current on-screen bond-alignment angle (the same
			// atan2 computation renderLabels uses for alignToBondDirection) exceeds this many degrees
			// from horizontal. Purely a UI-side filter value, no renderer involvement.
			float bondLabelAlignThresholdDeg = 45.0f;
		// Multi-select (Ctrl-click/box/circle-select add to this the same way selectedAtomIndices
		// below works for atoms) - primarily so ObjectPropertiesPanel can bulk-edit style across
		// several pins/free labels at once. Gizmo/keyboard-shortcut code still needs "the" pivot/anchor
		// for drag math - by convention that's selectedPinnedMeasurements.back() (the most recently
		// added/clicked one), same "last clicked is primary" convention selectedAtomIndices doesn't
		// need since atoms don't have their own gizmo picking a single representative.
		std::vector<SceneObjectId> selectedPinnedMeasurements;
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
		// RendererPanel's shared request consumer, which forwards it into the one app-wide Add menu.
		bool addAtomPopupRequested = false;
		std::optional<glm::vec2> addMenuScreenPosition;
		// Box/circle drag-select (Alt+B / Alt+C). Coordinates are viewport-relative pixels, same
		// space as RendererPanel::handleAtomPick's relX/relY.
		SelectionToolMode activeSelectionTool = SelectionToolMode::None;
		bool selectionDragActive = false;
		// Viewport transform gizmo (G/R/S). Keyboard commands set the operation and request a modal
		// transform; the Presentation gizmo owns the per-frame input and renderer-side preview while
		// atom and scene-object commands share one grouped undo boundary.
		GizmoOperation gizmoOperation = GizmoOperation::Translate;
		bool gizmoDragActive = false;
		bool modalTransformStartRequested = false;
		std::optional<ModalTransformSession> modalTransform;
		SceneTransformSelectionSnapshot modalTransformSelection;
		std::optional<LabelUndoSnapshot> modalTransformSceneObjectsBefore;
		// task/41 S11g: the scene objects as they were before the Properties panel's current style
		// drag started, held for as long as the drag lasts. ImGui reports a change on every frame a
		// slider or colour picker is held, so routing each of those frames through the undo-recording
		// edit context is what turns a one-second drag into sixty Ctrl+Z presses. The editor applies
		// the intermediate frames silently and pushes this one snapshot when the widget is released -
		// the same "capture once, commit once" contract `modalTransformSceneObjectsBefore` gives
		// G/R/S. Empty means no drag is in flight.
		std::optional<LabelUndoSnapshot> scenePathStyleEditBefore;
		bool modalTransformStartedFromHandle = false;
		TransformOrientation transformOrientation = TransformOrientation::Global;
		TransformPivotMode transformPivotMode = TransformPivotMode::Median;
		// Which Start/End/midpoint candidate owns the unified transform gizmo for a single arrow.
		// All three render as markers; clicking a non-active one moves the gizmo there, while the
		// active marker still consumes its own visible hit area. Reset to Both whenever
		// sceneArrowGizmoActiveArrowIndex no longer matches the current single-arrow selection.
		SceneArrowDragTarget sceneArrowGizmoActiveTarget = SceneArrowDragTarget::Both;
		std::size_t sceneArrowGizmoActiveArrowIndex = static_cast<std::size_t>(-1);
		enum class NavigationGizmoDragMode
		{
			None,
			Orbit,
			Pan,
			Zoom,
		};
		NavigationGizmoDragMode navigationGizmoDragMode = NavigationGizmoDragMode::None;
		glm::vec2 navigationGizmoLastMouse = glm::vec2(0.0f);
		// Pan/Zoom round buttons: drag on the button = one-shot, click without moving = toggle a
		// latched mode where every LMB drag in the viewport pans/zooms until clicked off or Esc.
		NavigationGizmoDragMode navigationGizmoLatchedMode = NavigationGizmoDragMode::None;
		glm::vec2 navigationGizmoDragStartMouse = glm::vec2(0.0f);
		bool navigationGizmoDragFromButton = false;
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
		// Shared sticky preference for both right-click Add > Orbital and the vertical toolbar's
		// orbital popup. Per-window UI state; it does not need project persistence.
		bool anchorOrbitalToSelection = true;
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

		// Link to StructureRecord for dirty flag checking
		StructureId structureId;

		// Set on the ephemeral preview windows of a structure creation session (see CreationSession).
		// Such a window is deliberately NOT domain-backed - structureId stays empty - and its whole
		// lifecycle belongs to the session, which is why it needs an identity of its own rather than
		// borrowing a StructureId it has no right to. Empty for every normal, domain-backed window.
		// StructureCreationTabsPanel docks these into the session's own 2+1 layout, so RendererPanel
		// leaves their initial docking alone.
		std::string sessionId;
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
