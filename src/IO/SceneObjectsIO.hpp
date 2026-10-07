#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	// Plain on-disk copies of the renderer's annotation objects (task 24). IO owns only this data and
	// its YAML shape; mapping to/from RendererWindowState lives in Renderer/Scene/SceneObjectPersistence.
	// Field defaults mirror RendererWindowState::LabelStyle/ArrowStyle so a missing key loads as the
	// renderer's own default.
	struct PersistedLabelStyle
	{
		glm::vec3 textColor = glm::vec3(0.92f, 0.92f, 0.85f);
		float textAlpha = 1.0f;
		glm::vec3 backgroundColor = glm::vec3(0.0f);
		float backgroundAlpha = 0.0f;
		glm::vec3 outlineColor = glm::vec3(0.0f);
		float outlineWidth = 0.0f;
		float cornerRadius = 0.0f;
		glm::vec2 padding = glm::vec2(0.05f);
		glm::vec3 strokeColor = glm::vec3(0.0f);
		float strokeWidth = 0.0f;
		float scale = 1.0f;
	};

	struct PersistedArrowStyle
	{
		glm::vec3 color = glm::vec3(0.95f, 0.75f, 0.1f);
		float alpha = 1.0f;
		float shaftWidth = 0.06f;
		bool dashed = false;
		float dashLength = 0.25f;
		float gapLength = 0.15f;
		glm::vec3 outlineColor = glm::vec3(0.0f);
		float outlineWidth = 0.0f;
		float headWidth = 0.14f;
		float headLength = 0.22f;
		// Optional two-stop ramp along the shaft. Absent in files written before it existed, which
		// is why it defaults to off - an old arrow keeps its flat colour.
		bool useGradient = false;
		glm::vec3 gradientStart = glm::vec3(0.6f, 0.6f, 0.6f);
		glm::vec3 gradientFinish = glm::vec3(0.9f, 0.9f, 0.9f);
	};

	// No stable atom id exists in the domain, so a reference is index + element + Cartesian position.
	// `position` doubles as the frozen anchor: when the reference no longer binds on load, the object
	// renders from these positions.
	// ponytail: one position field instead of the plan's separate atomRefs/frozenAnchor pair - they
	// are always equal at save time. Stable atom ids in CrystalStructure are the upgrade path.
	struct PersistedAtomRef
	{
		std::size_t index = 0;
		std::string element;
		glm::vec3 position = glm::vec3(0.0f);
	};

	// RendererWindowState::ScenePlane, field for field. Anchors use the same stable-on-disk atom
	// references as orbitals and are re-fitted after loading.
	struct PersistedScenePlane
	{
		std::string persistKey;
		glm::vec3 center = glm::vec3(0.0f);          // required
		glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f); // required
		glm::vec3 tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec2 halfExtents = glm::vec2(2.0f);
		std::vector<PersistedAtomRef> anchorAtoms;
		glm::vec3 color = glm::vec3(0.35f, 0.65f, 0.9f);
		float alpha = 0.35f;
		bool showBorder = true;
		bool visible = true;
	};

	// task/83: RendererWindowState::SceneDensity minus the loaded grid, which is re-read from
	// chgcarPath (and referencePath) on load. component is DensityComponentKey's spelling.
	struct PersistedSceneDensity
	{
		std::string persistKey;
		std::string displayName;
		std::string chgcarPath; // required
		std::string referencePath;
		std::string component = "magnetization";
		float isoValue = 0.0f;
		bool showNegative = true;
		glm::vec3 positiveColor = glm::vec3(0.95f, 0.80f, 0.10f);
		glm::vec3 negativeColor = glm::vec3(0.10f, 0.75f, 0.95f);
		float alpha = 0.7f;
		bool visible = true;
		bool renderable = true;
	};

	struct PersistedPinnedMeasurement
	{
		std::string persistKey; // 32 lowercase hex chars; empty = none (a new one is assigned on load)
		std::vector<PersistedAtomRef> atomRefs; // required: 2 (bond length) or 3 (angle)
		bool linkBroken = false;
		glm::vec3 labelOffset = glm::vec3(0.0f); // RendererWindowState::PinnedMeasurement::worldOffset
		bool alignToBondDirection = true;
		bool flipped = false;
		float rotationOffsetRadians = 0.0f;
		glm::vec3 bondPeriodicOffset = glm::vec3(0.0f);
		PersistedLabelStyle style;
	};

	struct PersistedFreeLabel
	{
		std::string persistKey;
		std::string text = "Label";
		glm::vec3 position = glm::vec3(0.0f); // required
		float rotationRadians = 0.0f;
		std::vector<PersistedAtomRef> anchorAtoms;
		std::optional<int> anchorVacancy;
		glm::vec3 anchorOffset = glm::vec3(0.0f);
		PersistedLabelStyle style;
	};

	enum class PersistedArrowKind { Line, Arrow2D, Arrow3D };
	enum class PersistedArrow2DOrientation { Billboard, FixedPlane };
	enum class PersistedWorldPlane { XY, XZ, YZ };

	struct PersistedSceneArrow
	{
		std::string persistKey;
		PersistedArrowKind kind = PersistedArrowKind::Arrow3D; // required
		PersistedArrow2DOrientation orientation2D = PersistedArrow2DOrientation::Billboard;
		PersistedWorldPlane fixedPlane = PersistedWorldPlane::XY;
		std::vector<glm::vec3> points = {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)}; // at least 2
		std::optional<glm::vec3> controlPoint;
		int curveSegments = 24;
		// IO round-trips the stable v1 tip names; path migration interprets them.
		std::string startTip = "None";
		std::string endTip = "Plain";
		// Zero or one stable atom reference per endpoint. Separate vectors preserve which end is
		// free without changing the dense anchor representation used by orbitals and planes.
		std::vector<PersistedAtomRef> startAnchorAtoms;
		std::vector<PersistedAtomRef> endAnchorAtoms;
		float atomBuffer = 1.15f;
		PersistedArrowStyle style;
	};

	// RendererWindowState::SceneOrbital, field for field. `preset` is the stable string from
	// OrbitalPresetName rather than the enumerator's ordinal, so inserting a preset into the enum
	// - which task 26d does, in the middle - cannot silently turn everyone's saved sp3 into a
	// sigma. An unknown preset string loads as the default `p` and is reported, not guessed at.
	// task/53: SceneOrbital::LcaoComponent. `atom` is the anchor as an atom reference (rebound on
	// load like anchorAtoms); its position doubles as the component's fallback centre.
	struct PersistedOrbitalLcaoComponent
	{
		PersistedAtomRef atom;
		std::string preset = "sp3"; // OrbitalPresetName; an unknown name skips the whole orbital
		int shell = 2;
		int lobeIndex = 0;
		float effectiveCharge = 1.0f;
		glm::vec3 rotationEuler = glm::vec3(0.0f);
		float coefficient = 1.0f;
	};

	struct PersistedSceneOrbital
	{
		std::string persistKey;
		std::string preset = "p"; // required
		int shell = 2;
		int lobeIndex = 0;
		float effectiveCharge = 1.0f;
		glm::vec3 centerA = glm::vec3(0.0f); // required
		glm::vec3 centerB = glm::vec3(1.5f, 0.0f, 0.0f);
		// Empty for a free-floating orbital, one entry anchored to a single atom, two for a
		// two-centre preset. Matched back to atom indices the same way a pinned measurement's
		// atomRefs are, so an anchor survives a reordered structure file.
		std::vector<PersistedAtomRef> anchorAtoms;
		glm::vec3 rotationEuler = glm::vec3(0.0f); // degrees
		bool phaseFlipped = false;
		float scale = 1.0f;
		glm::vec3 stretch = glm::vec3(1.0f);
		float isoFraction = 0.2f;
		int resolution = 48;
		bool smoothShading = true;
		glm::vec3 positiveLobeColor = glm::vec3(0.85f, 0.25f, 0.25f);
		glm::vec3 negativeLobeColor = glm::vec3(0.25f, 0.35f, 0.9f);
		float alpha = 0.75f;
		bool visible = true;
		// task/53, both optional and not written when empty - files without them load unchanged.
		std::vector<PersistedOrbitalLcaoComponent> lcaoComponents;
		std::string displayName;
	};

	// ---- v2: paths (task/41 S9) ------------------------------------------------------------------
	//
	// Element ids are deliberately absent from every struct below. They are regenerated on load
	// (plan v2 C11) precisely so that nothing outside a path can hold a reference to one, which in
	// turn means a file can never carry a stale one.

	enum class PersistedPathSegmentKind { Line, Cubic, Arc };

	// The binding variant flattened for YAML. `kind` is a stable string rather than an ordinal, for
	// the same reason PersistedSceneOrbital::preset is one: inserting a case into the runtime enum
	// must not silently reinterpret everyone's saved files.
	struct PersistedPathBinding
	{
		std::string kind = "Free"; // Free | CopyPosition | CopyVacancy | BondMidpoint | ObjectOrigin
		std::vector<PersistedAtomRef> atoms; // CopyPosition: 1, BondMidpoint: 2, otherwise empty
		glm::vec3 offset = glm::vec3(0.0f);
		float buffer = 0.0f; // endpoint CopyPosition / CopyVacancy only
		// ObjectOrigin targets another scene object by its persistKey, not by SceneObjectId: ids are
		// reallocated on every load and would point at whatever happened to take the number.
		std::string objectPersistKey;
		std::size_t vacancyIndex = 0; // CopyVacancy only; index into the structure's vacancies
	};

	struct PersistedPathNode
	{
		glm::vec3 position = glm::vec3(0.0f); // required
		PersistedPathBinding binding;
	};

	struct PersistedPathTransformBinding
	{
		std::string kind = "Free"; // Free | BondFrame
		std::vector<PersistedAtomRef> atoms; // BondFrame: 2
		float rollRadians = 0.0f;
	};

	struct PersistedPathSegment
	{
		PersistedPathSegmentKind kind = PersistedPathSegmentKind::Line;
		// Cubic only. Absolute world positions, matching PathHandle::position - not offsets.
		glm::vec3 startHandle = glm::vec3(0.0f);
		glm::vec3 endHandle = glm::vec3(0.0f);
		std::string startHandleType = "Auto"; // Free | Aligned | Vector | Auto
		std::string endHandleType = "Auto";
		// Arc only.
		glm::vec3 planeNormal = glm::vec3(0.0f, 0.0f, 1.0f);
		float signedSweepRadians = 0.0f;
	};

	struct PersistedPathGradientStop
	{
		float position = 0.0f;
		glm::vec3 color = glm::vec3(1.0f);
		float alpha = 1.0f;
	};

	// PathStrokeStyle field for field, with every enum written as its enumerator name.
	struct PersistedPathStyle
	{
		std::string profile = "Round"; // Round | Flat | CameraFacing
		glm::vec3 ribbonNormal{0.0f, 1.0f, 0.0f};
		// task/41 S11m. Written as `ribbon_thickness`, matching `ribbon_normal` beside it and
		// `start_decoration_filled` rather than the older camelCase strays like `radialSegments` -
		// the key name is stated here because leaving it to the implementation got it guessed one way
		// and asserted the other. Additive: absent means 0.0, which is the flat sheet every file
		// written before it meant. The format version does not move.
		float ribbonThickness = 0.0f;
		// task/41 S11t. Written as `ribbon_bevel`. Additive, absent means 0.0 - the sharp box - and
		// the format version does not move.
		float ribbonBevel = 0.0f;
		std::string ribbonBevelParts = "Both";
		int ribbonBevelSegments = 1;
		float ribbonBevelShape = 0.5f;
		bool shadeSmooth = false;
		float width = 0.05f;           // full width, as in PathStrokeStyle - not a radius
		std::string join = "Bevel";    // Bevel | Round
		std::string cap = "Butt";      // Butt | Square | Round
		int radialSegments = 12;
		glm::vec3 color = glm::vec3(1.0f);
		float alpha = 1.0f;
		bool dashEnabled = false;
		float dashLength = 0.1f;
		float gapLength = 0.05f;
		float dashPhase = 0.0f;
		bool gradientEnabled = false;
		std::vector<PersistedPathGradientStop> gradientStops;
		// PathDecorationKind names: None | Arrow | Stealth | Latex | Bar | Circle | Square | Diamond
		// | Kite. `OpenArrow` is a v2 name that no longer has an enumerator: it reads back as
		// { Arrow, filled = false } and is never written again. Kept accepted, not kept alive -
		// dropping it would silently turn every saved hollow arrowhead solid.
		//
		// The `filled` flags are additive and the format version does NOT move for them. A v2 file
		// written before task/41 S11i has no `*_decoration_filled` key, and absent means true, which
		// is what those files meant. Only `OpenArrow` meant otherwise, and it carries its own answer.
		std::string startDecoration = "None";
		float startDecorationLengthScale = 1.0f;
		float startDecorationWidthScale = 1.0f;
		bool startDecorationFilled = true;
		std::string endDecoration = "None";
		float endDecorationLengthScale = 1.0f;
		float endDecorationWidthScale = 1.0f;
		bool endDecorationFilled = true;
		std::string depthMode = "DepthTest"; // DepthTest | AlwaysOnTop
	};

	struct PersistedScenePath
	{
		// task/41 transform-2: the path's own position, rotation and scale, and its node positions are
		// local to it. Optional and additive - absent means the identity, and under the identity a
		// local position equals the world position files written before this stored. The format
		// version does not move.
		//
		// Rotation is written as a quaternion in xyzw order, matching how it is stored rather than
		// how the panel shows it: Euler degrees would have to pick one of several equivalent readings
		// on every save.
		glm::vec3 transformPosition{0.0f};
		glm::vec4 transformRotation{0.0f, 0.0f, 0.0f, 1.0f};
		glm::vec3 transformScale{1.0f};
		std::string persistKey;
		std::string name;
		std::vector<PersistedPathNode> nodes;       // required, at least 2
		std::vector<PersistedPathSegment> segments; // required, exactly nodes.size() - 1
		PersistedPathTransformBinding transformBinding;
		bool visible = true;
		bool renderable = true;
		PersistedPathStyle style;
	};

	using PersistedSceneObject = std::variant<
		PersistedPinnedMeasurement, PersistedFreeLabel, PersistedSceneArrow, PersistedSceneOrbital,
		PersistedScenePlane, PersistedScenePath, PersistedSceneDensity>;

	// task/51: one domain VacancySite. Not a scene object - a vacancy is structure data - but the
	// structure itself is stored as a POSCAR, which has no way to say "an atom was here", so the
	// vacancies ride in this sidecar next to the structure's scene objects. Field for field.
	struct PersistedVacancy
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 fractional = glm::vec3(0.0f);
		std::string sourceSpecies; // optional in the file, empty when absent
		std::string label;         // optional in the file, empty when absent
		int index = 0;             // optional in the file, 0 when absent
		std::optional<glm::vec3> color; // optional in the file, VacancySite::color
		bool hidden = false;            // optional in the file, VacancySite::hidden
	};

	// CrystalStructure::defectFrame, field for field, same reason as PersistedVacancy.
	struct PersistedDefectFrame
	{
		glm::vec3 origin = glm::vec3(0.0f);
		glm::vec3 x = glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 y = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 z = glm::vec3(0.0f, 0.0f, 1.0f);
		// How the axes are drawn (RendererWindowState::defectFrameAxis*/showDefectFrame). Optional in
		// the file: absent keys keep the window defaults.
		std::optional<float> axisLength;
		std::optional<float> axisWidth;
		std::optional<bool> negativeAxes;
		std::optional<bool> shown;
	};

	struct PersistedStructureSceneObjects
	{
		std::string structureKey; // SceneObjectsIO::MakeStructureKey
		std::vector<PersistedSceneObject> objects;
		// Optional `vacancies:` list beside `objects:` - absent means none and an empty list is not
		// written, so formatVersion does not change and older files and builds are unaffected (the
		// projectObjects rule). An entry without `position` is skipped with the usual
		// "scene_objects.entry_skipped" warning. A structure entry may now carry vacancies and no
		// objects; it is written all the same.
		std::vector<PersistedVacancy> vacancies;
		// Optional `defectFrame:` map, same additive rule as `vacancies`. An entry missing an axis or
		// with a non-finite value is skipped with "scene_objects.entry_skipped".
		std::optional<PersistedDefectFrame> defectFrame;
	};

	struct SceneObjectsFile
	{
		int formatVersion = 2;
		std::vector<PersistedStructureSceneObjects> structures;
		// The project's one structure-free scene ("Project Scene", see Renderer/ProjectSceneWindow.hpp).
		// Additive and optional: absent in the file means empty, and an empty list is not written, so
		// formatVersion does not change and older files and builds are unaffected.
		std::vector<PersistedSceneObject> projectObjects;
	};

	// scene_objects.yaml next to manifest.yaml. Schema:
	//   formatVersion: 1
	//   structures:
	//     - structureKey: structures/NV/POSCAR
	//       objects:
	//         - kind: PinnedMeasurement | FreeLabel | SceneArrow | SceneOrbital | ScenePlane | SceneDensity
	//           ...per-kind payload (camelCase keys = the struct field names above, vec as [x, y, z],
	//           enums as their enumerator name, style as a nested map; SceneArrow's `kind` field is
	//           written as `arrowKind` so it does not collide with the entry tag)
	//       vacancies:             # optional, task/51
	//         - position: [x, y, z]
	//           fractional: [x, y, z]
	//           sourceSpecies: C       # optional
	//           label: V_C             # optional
	//           index: 12              # optional
	//       defectFrame:           # optional
	//         origin: [x, y, z]
	//         x: [x, y, z]
	//         y: [x, y, z]
	//         z: [x, y, z]
	//         axisLength: 1.6      # optional, display only
	//         axisWidth: 2.5       # optional
	//         negativeAxes: true   # optional
	//         shown: true          # optional
	//   projectObjects:            # optional; same object entries, no structureKey
	//     - kind: ScenePath
	//       ...
	class SceneObjectsIO
	{
	public:
		static constexpr int kFormatVersion = 2;

		// v1 of this file predates paths. It is still read: a v1 arrow is migrated to a path by
		// Renderer/Scene/ScenePathPersistence, which is where all knowledge of what a v1 arrow meant
		// lives. IO only tells the two apart.

		[[nodiscard]] static Path FilePath(const Path &projectDirectory); // projectDirectory / "scene_objects.yaml"

		// Structure key persisted in the file: the structure's source path relative to the project
		// directory with '/' separators when it lies inside it, otherwise the absolute path with '/'.
		[[nodiscard]] static std::string MakeStructureKey(const Path &projectDirectory, const Path &structureSourcePath);

		// Returns false (outError set) when the text is not YAML, its root is not a map, or its
		// formatVersion is greater than kFormatVersion. The version is checked BEFORE anything else is
		// interpreted: a file from a future version may reuse a key with a different meaning, so
		// parsing it on a best-effort basis and keeping what looked familiar would corrupt it on the
		// next save. That rejection carries code "scene_objects.future_format_version".
		// An empty/whitespace text is a valid empty file. An object with an unknown `kind`, a missing
		// required field or a bad value (wrong atomRefs count, unknown enum name, non-numeric vector)
		// is skipped and reported as one Severity::Warning, ErrorCategory::IO StructuredError with code
		// "scene_objects.entry_skipped" - never aborts the rest. A structure entry without
		// structureKey is skipped the same way.
		[[nodiscard]] static bool Parse(
			const std::string &text,
			SceneObjectsFile &outFile,
			std::vector<StructuredError> &outWarnings,
			std::string &outError);

		[[nodiscard]] static std::string Serialize(const SceneObjectsFile &file);

		// Missing file = success with an empty SceneObjectsFile.
		[[nodiscard]] static bool Load(
			const Path &projectDirectory,
			SceneObjectsFile &outFile,
			std::vector<StructuredError> &outWarnings,
			std::string &outError);

		// Copies an existing v1 scene_objects.yaml to "scene_objects.yaml.v1.bak" before the first v2
		// write. True also means "there was no v1 file" and "the backup already existed" - the point
		// is that afterwards, either a backup exists or there was never anything to lose. False means
		// the copy itself failed, and Save then refuses to write: replacing the only copy of a file
		// with a format the user's previous version cannot read is not a recoverable mistake.
		[[nodiscard]] static bool WriteBackupOnce(const Path &projectDirectory, std::string &outError);

		// Writes a sibling temp file then renames it over FilePath(); a failure leaves any existing
		// file untouched and returns false with outError. Calls WriteBackupOnce first and aborts
		// without writing anything if that fails.
		[[nodiscard]] static bool Save(const Path &projectDirectory, const SceneObjectsFile &file, std::string &outError);
	};
} // namespace DefectStudio
