#pragma once

#include <cstddef>
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
		glm::vec3 outlineColor = glm::vec3(0.0f);
		float outlineWidth = 0.0f;
		float headWidth = 0.14f;
		float headLength = 0.22f;
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
		glm::vec3 start = glm::vec3(0.0f); // required
		glm::vec3 end = glm::vec3(0.0f, 0.0f, 1.0f); // required
		PersistedArrowStyle style;
	};

	using PersistedSceneObject = std::variant<PersistedPinnedMeasurement, PersistedFreeLabel, PersistedSceneArrow>;

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
	//         - kind: PinnedMeasurement | FreeLabel | SceneArrow
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
