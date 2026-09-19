#pragma once

#include <cstddef>
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
		// IO round-trips stable names only. Renderer/Scene/SceneObjectPersistence owns conversion to
		// RendererWindowState::ArrowTip and all knowledge of what these names look like.
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
		glm::vec3 positiveLobeColor = glm::vec3(0.85f, 0.25f, 0.25f);
		glm::vec3 negativeLobeColor = glm::vec3(0.25f, 0.35f, 0.9f);
		float alpha = 0.75f;
		bool visible = true;
	};

	using PersistedSceneObject = std::variant<
		PersistedPinnedMeasurement, PersistedFreeLabel, PersistedSceneArrow, PersistedSceneOrbital,
		PersistedScenePlane>;

	struct PersistedStructureSceneObjects
	{
		std::string structureKey; // SceneObjectsIO::MakeStructureKey
		std::vector<PersistedSceneObject> objects;
	};

	struct SceneObjectsFile
	{
		int formatVersion = 1;
		std::vector<PersistedStructureSceneObjects> structures;
	};

	// scene_objects.yaml next to manifest.yaml. Schema:
	//   formatVersion: 1
	//   structures:
	//     - structureKey: structures/NV/POSCAR
	//       objects:
	//         - kind: PinnedMeasurement | FreeLabel | SceneArrow | SceneOrbital | ScenePlane
	//           ...per-kind payload (camelCase keys = the struct field names above, vec as [x, y, z],
	//           enums as their enumerator name, style as a nested map; SceneArrow's `kind` field is
	//           written as `arrowKind` so it does not collide with the entry tag)
	class SceneObjectsIO
	{
	public:
		static constexpr int kFormatVersion = 1;

		[[nodiscard]] static Path FilePath(const Path &projectDirectory); // projectDirectory / "scene_objects.yaml"

		// Structure key persisted in the file: the structure's source path relative to the project
		// directory with '/' separators when it lies inside it, otherwise the absolute path with '/'.
		[[nodiscard]] static std::string MakeStructureKey(const Path &projectDirectory, const Path &structureSourcePath);

		// Returns false (outError set) only when the text is not YAML or its root is not a map.
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

		// Writes a sibling temp file then renames it over FilePath(); a failure leaves any existing
		// file untouched and returns false with outError.
		[[nodiscard]] static bool Save(const Path &projectDirectory, const SceneObjectsFile &file, std::string &outError);
	};
} // namespace DefectStudio
