#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Core/Utils/Memory.hpp"
#include "Renderer/OpenGl/MsdfFont.hpp"
#include "Renderer/OpenGl/OpenGlFrameBuffer.hpp"
#include "Renderer/OpenGl/OpenGlShaderLibrary.hpp"
#include "Renderer/RendererMeshData.hpp"
#include "Renderer/Scene/IsosurfaceMesher.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	// Two independent slots so spin-up and spin-down wavefunctions can render simultaneously -
	// slot 0 is conventionally "up"/primary, slot 1 "down"/secondary.
	constexpr int kIsosurfaceSlotCount = 2;

	struct OpenGlMeshHandles
	{
		unsigned int vao = 0;
		unsigned int vbo = 0;
		unsigned int ebo = 0;
		unsigned int instanceVbo = 0;
		int indexCount = 0;
	};

	struct OpenGlAtomInstance
	{
		glm::vec4 positionRadius = glm::vec4(0.0f);
		glm::vec4 color = glm::vec4(1.0f);
	};

	struct OpenGlBondInstance
	{
		glm::mat4 model = glm::mat4(1.0f);
		glm::vec4 colorA = glm::vec4(1.0f);
		glm::vec4 colorB = glm::vec4(1.0f);
	};

	// One glyph quad. aWorldCenter repeats across every glyph of the same label (the string's
	// anchor point, e.g. a bond midpoint); aLocalOffsetSize places this particular glyph relative
	// to it in label-local em-space, which the vertex shader then billboards via the camera's own
	// right/up axes.
	struct OpenGlLabelInstance
	{
		glm::vec3 worldCenter = glm::vec3(0.0f);
		glm::vec4 localOffsetSize = glm::vec4(0.0f);
		glm::vec4 atlasUvMinMax = glm::vec4(0.0f);
		glm::vec4 color = glm::vec4(1.0f);
		// In-plane rotation (radians) applied within the billboard's own cameraRight/cameraUp basis
		// before placing the glyph - see PinnedMeasurement::alignToBondDirection.
		float rotationRadians = 0.0f;
		// LabelStyle::outlineColor/outlineWidth/cornerRadius - the background quad's own border
		// stroke and corner rounding (label_background.frag's rounded-rect SDF). Unused - always 0 -
		// on glyph instances (the "labels" program's shader doesn't read them), but shared here since
		// both instance kinds go through the same VAO/instance layout; see AppendLabelBackgroundInstance.
		glm::vec3 outlineColor = glm::vec3(0.0f);
		float outlineWidth = 0.0f;
		float cornerRadius = 0.0f;
		// LabelStyle::strokeColor/strokeWidth - the glyph's own MSDF stroke (labels.frag), independent
		// of the background border above. Unused - always 0 - on background instances (label_background
		// .frag doesn't read them), same sharing rationale as outlineColor/outlineWidth/cornerRadius.
		glm::vec3 strokeColor = glm::vec3(0.0f);
		float strokeWidth = 0.0f;
	};

	// One SceneArrow Arrow2D quad. right/up are fully resolved world-space basis vectors, computed
	// CPU-side in renderSceneArrows/ComputeArrowQuadBasis (camera-facing-plane projection of the
	// arrow direction for Billboard, fixed world-plane axes for FixedPlane) - so unlike
	// OpenGlLabelInstance's billboard math, arrow_quad.vert needs no camera uniform of its own.
	// halfSize/outlineWidth/headHalfWidth/headLength all arrive here already converted from
	// SceneArrow::ArrowStyle's screen-space pixel units to this arrow's own local world-space scale
	// (renderSceneArrows does that conversion via a projection probe, same idea as
	// RendererPanel::handleFreeLabelInteraction's pixel<->world drag conversion) - the shader itself
	// stays entirely unit-agnostic, same as before.
	struct OpenGlArrowQuadInstance
	{
		glm::vec3 worldCenter = glm::vec3(0.0f);
		glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec2 halfSize = glm::vec2(0.0f); // x = half arrow length, y = half shaft width
		glm::vec4 color = glm::vec4(1.0f);
		glm::vec3 outlineColor = glm::vec3(0.0f);
		float outlineWidth = 0.0f;
		float headHalfWidth = 0.0f; // 0 collapses the SDF union's head triangle to a point (no head)
		float headLength = 0.0f;
	};

	// One SceneArrow Arrow3D's welded shaft+head mesh (see BuildWeldedArrowMesh) - unlike bonds'
	// shared unit m_CylinderMesh/m_ConeMesh, each arrow's mesh has its own proportions (shaftRadius/
	// headRadius/headLength/length all vary per-arrow), so it can't be instanced from one shared
	// buffer - every arrow gets its own small VAO/VBO/EBO, indexed by its position in
	// RendererWindowState::sceneArrows. Rebuilt only when the 4 params below actually change (a
	// position/orientation-only drag reuses the same geometry through the draw transform) - the
	// negative defaults guarantee the very first frame for a slot always rebuilds.
	struct OpenGlSceneArrowMeshCache
	{
		float shaftRadius = -1.0f;
		float headRadius = -1.0f;
		float headLength = -1.0f;
		float length = -1.0f;
		float bulgeStrength = -1.0f;
		OpenGlMeshHandles mesh;
	};

	struct OpenGlViewportResources
	{
		OpenGlFrameBuffer frameBuffer;
		Time::SteadyTimePoint lastRenderTime{};
		bool atomsDirty = true;
		bool bondsDirty = true;
		bool gridDirty = true;
		bool cellEdgesDirty = true;
		bool labelsDirty = true;
		std::size_t lastAtomCount = 0;
		std::size_t lastBondCount = 0;
		std::size_t lastSelectedCount = 0;
		std::size_t lastSelectionHash = 0;
		std::size_t lastSelectedBondCount = 0;
		std::size_t lastBondSelectionHash = 0;
		std::size_t lastBondVisibilityHash = 0;
		std::size_t lastVisibilityHash = 0;
		std::size_t lastPositionHash = 0;
		// Catches atom-appearance edits that touch neither count/selection/visibility/position - e.g.
		// "Change atom type" (RendererAtomEditCommands::ChangeSelectedAtomTypeCommand) only rewrites
		// AtomStyleTable-derived color/radius and bond gradient endpoints, same atom count/positions.
		// Without this, atoms/bonds keep showing their last-uploaded (pre-change) color until some
		// unrelated dirty condition above happens to also fire - same class of bug as lastPositionHash
		// above, just for appearance instead of geometry.
		std::size_t lastColorHash = 0;
		// Bond radius multiplier is a global render setting, not per-structure data - the cylinder
		// trim math in renderBonds bakes it into cachedBondInstances, so a change needs its own
		// dirty check here (see the shrinkA/shrinkB comment at that call site for why).
		float lastBondRadiusMultiplier = 1.0f;
		std::string lastSourcePath;
		std::vector<OpenGlAtomInstance> cachedAtomInstances;
		std::vector<OpenGlBondInstance> cachedBondInstances;
		std::vector<OpenGlLabelInstance> cachedLabelInstances;
		// LabelStyle::backgroundAlpha > 0 quads, one per label - drawn via the "label_background"
		// program before cachedLabelInstances' glyph pass so glyphs composite on top (see renderLabels).
		// Same labelsDirty lifecycle as cachedLabelInstances - rebuilt together, always empty in
		// practice today since "every bond" mode has no per-bond style to opt into a background.
		std::vector<OpenGlLabelInstance> cachedLabelBackgroundInstances;
		std::vector<glm::vec3> cachedGridVertices;
		std::vector<glm::vec3> cachedCellEdgeVertices;
		// One entry per Arrow3D SceneArrow, indexed by its position in sceneArrows - see
		// OpenGlSceneArrowMeshCache. Shrunk (with GL cleanup) when sceneArrows.size() drops.
		std::vector<OpenGlSceneArrowMeshCache> sceneArrow3DMeshCache;

		// Per-window orbital isosurface GPU buffers. Was 2 backend-global slots shared by every
		// window; regenerating one window's orbital mesh (e.g. dragging its iso-value slider)
		// silently overwrote what every OTHER window with an orbital overlay was drawing, since
		// they all bound the same VAO. Lazily created (ensureIsosurfaceBuffers) on first
		// RegenerateIsosurfaceGpu() call for this window - not every window shows an orbital
		// overlay, so eager creation (2M vertices * 32B * 2 slots ~= 128MB) would be wasteful.
		// Freed only at backend Shutdown, same lifecycle as every other resource in this struct -
		// RendererLayer::RemoveWindow doesn't tear OpenGlViewportResources down on window close at
		// all today (pre-existing gap for the whole struct, not something this fix alone should
		// paper over with new cleanup machinery nothing else here has yet).
		std::array<unsigned int, kIsosurfaceSlotCount> isosurfaceVao{};
		std::array<unsigned int, kIsosurfaceSlotCount> isosurfaceVertexSsbo{};
		std::array<unsigned int, kIsosurfaceSlotCount> isosurfaceCounterSsbo{};
	};

	class RendererViewCamera;

	class OpenGlRendererBackend
	{
	public:
		OpenGlRendererBackend() = default;
		~OpenGlRendererBackend();

		Result<void> Initialize(const Path &shaderDirectory, const RendererPrimitiveMeshAssets &primitiveMeshes);
		void Shutdown();
		void ReloadShadersIfNeeded();
		void CollectProfilingData();
		void MarkGridDirty();
		[[nodiscard]] unsigned int RenderWindow(
			const std::string &windowKey,
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			const RendererGlobalRenderSettings &globalSettings,
			int viewportWidth,
			int viewportHeight,
			bool showAtoms,
			bool showBonds,
			bool showCellBox,
			bool showGrid,
			bool showLabels = false,
			const std::vector<RendererWindowState::PinnedMeasurement> &pinnedMeasurements = {},
			const std::vector<std::size_t> &selectedPinnedMeasurements = {},
			const std::vector<RendererWindowState::FreeLabel> &freeLabels = {},
			const std::vector<std::size_t> &selectedFreeLabels = {},
			const std::vector<RendererWindowState::SceneArrow> &sceneArrows = {},
			const std::vector<std::size_t> &selectedSceneArrows = {},
			const std::vector<std::size_t> &selectedAtomIndices = {},
			const std::vector<std::size_t> &selectedBondIndices = {},
			// TODO(T08.6.3): temporary debug overlays to validate the isosurface pipeline
			// end-to-end (CPU reference, then its GPU compute-shader port) before the real
			// orbital panel exists. Not part of the real per-window structure render path.
			const std::vector<IsosurfaceVertex> *debugIsosurfaceMesh = nullptr,
			const RendererWindowState::OrbitalOverlayChannel *orbitalChannelUp = nullptr,
			const RendererWindowState::OrbitalOverlayChannel *orbitalChannelDown = nullptr,
			const RendererWindowState::DisplacementComparisonState *displacementComparison = nullptr,
			// Non-destructive whole-structure offset (RendererWindowState::viewOffset, export-preview-
			// only as of Etap F Phase 1) - forwarded as a render-time uniform (u_SceneOffset) to every
			// geometry pass (atoms/bonds/cell box/grid/labels/isosurface), never baked into any CPU-
			// side position data. See each shader's own u_SceneOffset comment for the per-pass detail.
			const glm::vec3 &sceneOffset = glm::vec3(0.0f),
			// notes.txt pt. 7 - see RendererWindowState::bondLabelAutoOffsetEnabled/Magnitude.
			bool bondLabelAutoOffsetEnabled = true,
			float bondLabelAutoOffsetMagnitude = 0.3f,
			// notes.txt pt. 8 - see renderLabels' own comment on this same parameter.
			float bondLabelAlignThresholdDeg = 45.0f);

		// Runs the marching-tetrahedra compute shader (isosurface_march.comp - GPU port of
		// GenerateIsosurfaceMesh) over `grid` and returns the resulting vertex count (0 on
		// failure/empty), ready to render via the orbitalChannel{Up,Down} slots above. The
		// geometry stays GPU-resident (the compute shader's output SSBO doubles as the vertex
		// buffer) - only a 4-byte counter is read back, not the vertex data itself. `slot` selects
		// which of `windowKey`'s two independent channel buffers to write (0=up, 1=down) - buffers
		// are per-window (see OpenGlViewportResources), `windowKey` must already have an active
		// viewport (i.e. RenderWindow has run for it at least once); returns 0 otherwise.
		[[nodiscard]] int RegenerateIsosurfaceGpu(
			const std::string &windowKey, const OrbitalGridData &grid, float isoValue, int slot = 0);
		// Reads back the last-rendered frame for windowKey (must have been rendered via
		// RenderWindow this session) and writes it to a PNG. Returns false + fills error on
		// missing viewport or write failure. crop* are fractions (0..1) of width/height trimmed
		// from each edge before writing - a real pixel crop (changes output aspect ratio), not the
		// pan/zoom reframing that keeps the requested resolution's aspect intact.
		[[nodiscard]] bool CaptureWindowToPng(
			const std::string &windowKey,
			const Path &outputPath,
			std::string &error,
			float cropLeft = 0.0f,
			float cropRight = 0.0f,
			float cropTop = 0.0f,
			float cropBottom = 0.0f) const;

	private:
		Result<void> createStaticGeometry(const RendererPrimitiveMeshAssets &primitiveMeshes);
		void releaseStaticGeometry();
		Result<void> createSphereMesh(const RendererStaticMeshData &meshData);
		Result<void> createCylinderMesh(const RendererStaticMeshData &meshData);
		Result<void> createConeMesh(const RendererStaticMeshData &meshData);
		void createLabelQuadMesh();
		void createArrowQuadMesh();
		void createScreenGrid();
		void createIsosurfaceGeometry();
		// Lazily allocates `resources`'s per-window isosurface GPU buffers on first use - no-op if
		// already created (checks isosurfaceVao[0]).
		void ensureIsosurfaceBuffers(OpenGlViewportResources &resources);
		void configureOpenGlState() const;
		void renderAtoms(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const RendererGlobalRenderSettings &globalSettings,
			const std::vector<std::size_t> &selectedIndices = {},
			const glm::vec3 &sceneOffset = glm::vec3(0.0f));
		void renderBonds(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const RendererGlobalRenderSettings &globalSettings,
			const std::vector<std::size_t> &selectedIndices = {},
			const glm::vec3 &sceneOffset = glm::vec3(0.0f));
		// Atoms-displacement comparison arrows (RendererWindowState::displacementComparison) - one
		// batched instanced draw for all visible shafts (shared m_CylinderMesh, like renderBonds)
		// plus one for all visible cone heads (shared m_ConeMesh, already instance-layout-compatible
		// via createConeMesh but otherwise unused for instancing today - see that function). Unlike
		// sceneArrows this can be hundreds-to-thousands of auto-generated arrows, so no per-arrow
		// welded mesh (BuildWeldedArrowMesh) - two shared meshes, CPU-filtered by
		// displacementComparison->displayThresholdAngstrom each call (no dirty-cache, same choice
		// renderSceneArrows already makes for its own per-call instance lists). Also draws a ghost
		// marker (shared m_SphereMesh/"atoms" program) per interstitial-like unmatched comparison
		// atom. nullptr = no comparison active for this window.
		void renderDisplacementArrows(
			const RendererStructureData &structure,
			const RendererWindowState::DisplacementComparisonState *displacementComparison,
			const RendererViewCamera &camera,
			const RendererGlobalRenderSettings &globalSettings,
			const glm::vec3 &sceneOffset = glm::vec3(0.0f));
		// Figure-annotation arrows (RendererWindowState::sceneArrows). Line draws its shaft through
		// the shared bond cylinder mesh/shader like before; Arrow3D draws through its own per-arrow
		// welded shaft+head mesh (BuildWeldedArrowMesh, cached in resources.sceneArrow3DMeshCache -
		// rebuilt only when that arrow's shaftWidth/headWidth/headLength/length actually change, not
		// every frame); Arrow2D draws through the arrow_quad SDF shader. No dirty-cache for the first
		// two lists below (shaftInstances/quadInstances) - rebuilt every call like pinnedInstances in
		// renderLabels, cheap for the handful of arrows a figure needs.
		// Called twice per frame with opposite `renderArrow2D` values (see RenderWindow) so Line/
		// Arrow3D (world-space, depth-tested, drawn with bonds) and Arrow2D (screen-space sized,
		// depth-disabled, drawn late with labels - docs/scene_arrow_rework_plan_corrected.md
		// Section 10) each land in the GL state their own semantics need, without either pass paying
		// for the other kind's work. viewportPixelSize is the actual framebuffer resolution
		// (resources.frameBuffer.Width/Height) - needed only by the Arrow2D pass to convert
		// ArrowStyle's pixel-space geometry into this arrow's local world-space scale via a
		// projection probe.
		void renderSceneArrows(
			const std::vector<RendererWindowState::SceneArrow> &arrows,
			const std::vector<std::size_t> &selectedArrows,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const RendererGlobalRenderSettings &globalSettings,
			bool renderArrow2D,
			const glm::vec2 &viewportPixelSize,
			const glm::vec3 &sceneOffset = glm::vec3(0.0f));
		void renderLabels(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			bool showAllLabels,
			const std::vector<RendererWindowState::PinnedMeasurement> &pinnedMeasurements,
			const std::vector<std::size_t> &selectedPinnedMeasurements,
			const std::vector<RendererWindowState::FreeLabel> &freeLabels = {},
			const std::vector<std::size_t> &selectedFreeLabels = {},
			const glm::vec3 &sceneOffset = glm::vec3(0.0f),
			// notes.txt pt. 7 - see RendererWindowState::bondLabelAutoOffsetEnabled/Magnitude.
			bool bondLabelAutoOffsetEnabled = true,
			float bondLabelAutoOffsetMagnitude = 0.3f,
			// notes.txt pt. 8 - live threshold (RendererWindowState::bondLabelAlignThresholdDeg):
			// every bond-length pin whose current in-plane bond-alignment angle exceeds this many
			// degrees from horizontal renders flat instead, recomputed fresh every frame - no
			// persistent mutation, so it reacts to the slider immediately and reverts on its own.
			float bondLabelAlignThresholdDeg = 45.0f);
		void renderCellBox(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const glm::vec3 &sceneOffset = glm::vec3(0.0f));
		void renderGrid(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const RendererGlobalRenderSettings &globalSettings,
			const glm::vec3 &sceneOffset = glm::vec3(0.0f));
		void renderIsosurfaceOverlay(
			const std::vector<IsosurfaceVertex> &vertices,
			const RendererViewCamera &camera,
			const RendererGlobalRenderSettings &globalSettings);
		void renderIsosurfaceGpuOverlay(
			unsigned int vao,
			int vertexCount,
			const RendererViewCamera &camera,
			const RendererGlobalRenderSettings &globalSettings,
			const glm::vec3 &positiveLobeColor,
			const glm::vec3 &negativeLobeColor,
			float lobeAlpha,
			const glm::vec3 &sceneOffset);
		// T09 extension point: GPU-side bond transform via compute shader.
		// SSBO i shader są inicjalizowane, ale dispatch nie jest wywoływany.
		// Aktywować gdy T09 wprowadzi automatyczną regenerację bondów przy przesuwaniu atomów.
		void dispatchBondCompute(const RendererStructureData &structure);
		[[nodiscard]] OpenGlViewportResources &viewportResources(const std::string &windowKey, int width, int height);
		[[nodiscard]] glm::mat4 buildBondTransform(const glm::vec3 &start, const glm::vec3 &finish, float radius) const;
		// Same translate+rotate (local +Z -> direction) as buildBondTransform, but no scale at all -
		// for BuildWeldedArrowMesh's output, whose vertices already have absolute world-unit radii
		// and length baked in, so scaling Z by `length` again (as buildBondTransform's radius overload
		// would) or radius by anything but 1 would double the arrow's real size.
		[[nodiscard]] glm::mat4 buildArrowRevolutionTransform(const glm::vec3 &start, const glm::vec3 &end) const;

	private:
		bool m_Initialized = false;
		Path m_ShaderDirectory;
		OpenGlShaderLibrary m_ShaderLibrary;
		OpenGlMeshHandles m_SphereMesh;
		OpenGlMeshHandles m_CylinderMesh;
		OpenGlMeshHandles m_ConeMesh;
		OpenGlMeshHandles m_LabelQuadMesh;
		OpenGlMeshHandles m_ArrowQuadMesh;
		// Lazily constructed on first renderLabels() call with showLabels=true - atlas generation
		// (FreeType + msdfgen) costs real time, no reason to pay it for windows/sessions that never
		// toggle labels on. Bundled font (see resolveLabelFontPath) same as the app's own UI font.
		Unique<MsdfFont> m_LabelFont;
		unsigned int m_LineVao = 0;
		unsigned int m_LineVbo = 0;
		unsigned int m_IsosurfaceVao = 0;
		unsigned int m_IsosurfaceVbo = 0;
		// Grid input SSBO stays backend-global (not per-window): consumed synchronously within a
		// single RegenerateIsosurfaceGpu call and fully overwritten before that call's compute
		// dispatch reads it, never held/read across calls - safe to share since only one such call
		// is ever in flight (main-thread only, GL calls aren't reentrant here). The per-window
		// output vertex buffer/counter/VAO (what actually gets drawn) live in
		// OpenGlViewportResources instead, see its isosurface* fields.
		unsigned int m_IsosurfaceGridSsbo = 0;
		unsigned int m_ComputeInputSsbo = 0;
		unsigned int m_ComputeOutputSsbo = 0;
		std::unordered_map<std::string, OpenGlViewportResources> m_Viewports;
	};
} // namespace DefectStudio
