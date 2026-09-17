#include <gtest/gtest.h>

#include "IO/SceneObjectsIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] SceneObjectsFile RoundTrip(const SceneObjectsFile &file, std::vector<StructuredError> &warnings)
		{
			SceneObjectsFile loaded;
			std::string error;
			EXPECT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), loaded, warnings, error)) << error;
			return loaded;
		}

		[[nodiscard]] SceneObjectsFile SingleObject(PersistedSceneObject object)
		{
			SceneObjectsFile file;
			file.structures.push_back({"structures/NV/POSCAR", {std::move(object)}});
			return file;
		}
	} // namespace

	TEST(SceneObjectsIOTests, PinnedMeasurementRoundTrips)
	{
		PersistedPinnedMeasurement pin;
		pin.persistKey = "0123456789abcdef0123456789abcdef";
		pin.atomRefs = {{3, "C", glm::vec3(0.5f, 1.25f, -2.0f)}, {7, "N", glm::vec3(1.0f, 0.0f, 0.0f)}, {9, "C", glm::vec3(0.0f, 2.0f, 0.0f)}};
		pin.linkBroken = true;
		pin.labelOffset = glm::vec3(0.1f, 0.2f, 0.3f);
		pin.alignToBondDirection = false;
		pin.flipped = true;
		pin.rotationOffsetRadians = 0.75f;
		pin.bondPeriodicOffset = glm::vec3(1.0f, 0.0f, -1.0f);
		pin.style.textColor = glm::vec3(0.1f, 0.2f, 0.3f);
		pin.style.backgroundAlpha = 0.5f;
		pin.style.padding = glm::vec2(0.2f, 0.4f);
		pin.style.strokeWidth = 2.5f;
		pin.style.scale = 1.5f;

		std::vector<StructuredError> warnings;
		const SceneObjectsFile loaded = RoundTrip(SingleObject(pin), warnings);
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.formatVersion, SceneObjectsIO::kFormatVersion);
		ASSERT_EQ(loaded.structures.size(), 1u);
		EXPECT_EQ(loaded.structures[0].structureKey, "structures/NV/POSCAR");
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto *got = std::get_if<PersistedPinnedMeasurement>(&loaded.structures[0].objects[0]);
		ASSERT_NE(got, nullptr);
		EXPECT_EQ(got->persistKey, pin.persistKey);
		ASSERT_EQ(got->atomRefs.size(), 3u);
		EXPECT_EQ(got->atomRefs[1].index, 7u);
		EXPECT_EQ(got->atomRefs[1].element, "N");
		EXPECT_EQ(got->atomRefs[0].position, pin.atomRefs[0].position);
		EXPECT_TRUE(got->linkBroken);
		EXPECT_EQ(got->labelOffset, pin.labelOffset);
		EXPECT_FALSE(got->alignToBondDirection);
		EXPECT_TRUE(got->flipped);
		EXPECT_FLOAT_EQ(got->rotationOffsetRadians, 0.75f);
		EXPECT_EQ(got->bondPeriodicOffset, pin.bondPeriodicOffset);
		EXPECT_EQ(got->style.textColor, pin.style.textColor);
		EXPECT_FLOAT_EQ(got->style.backgroundAlpha, 0.5f);
		EXPECT_EQ(got->style.padding, pin.style.padding);
		EXPECT_FLOAT_EQ(got->style.strokeWidth, 2.5f);
		EXPECT_FLOAT_EQ(got->style.scale, 1.5f);
	}

	TEST(SceneObjectsIOTests, FreeLabelRoundTrips)
	{
		PersistedFreeLabel label;
		label.persistKey = "ffffffffffffffffffffffffffffffff";
		label.text = "V_C: \"vacancy\" #1";
		label.position = glm::vec3(-1.5f, 2.0f, 3.25f);
		label.rotationRadians = 1.0f;
		label.style.outlineColor = glm::vec3(1.0f, 0.0f, 0.0f);
		label.style.outlineWidth = 0.02f;

		std::vector<StructuredError> warnings;
		const SceneObjectsFile loaded = RoundTrip(SingleObject(label), warnings);
		EXPECT_TRUE(warnings.empty());
		const auto *got = std::get_if<PersistedFreeLabel>(&loaded.structures.at(0).objects.at(0));
		ASSERT_NE(got, nullptr);
		EXPECT_EQ(got->persistKey, label.persistKey);
		EXPECT_EQ(got->text, label.text);
		EXPECT_EQ(got->position, label.position);
		EXPECT_FLOAT_EQ(got->rotationRadians, 1.0f);
		EXPECT_EQ(got->style.outlineColor, label.style.outlineColor);
		EXPECT_FLOAT_EQ(got->style.outlineWidth, 0.02f);
	}

	TEST(SceneObjectsIOTests, SceneArrowRoundTrips)
	{
		PersistedSceneArrow arrow;
		arrow.persistKey = "00000000000000000000000000000001";
		arrow.kind = PersistedArrowKind::Arrow2D;
		arrow.orientation2D = PersistedArrow2DOrientation::FixedPlane;
		arrow.fixedPlane = PersistedWorldPlane::YZ;
		arrow.start = glm::vec3(1.0f, 2.0f, 3.0f);
		arrow.end = glm::vec3(4.0f, 5.0f, 6.0f);
		arrow.style.color = glm::vec3(0.2f, 0.4f, 0.6f);
		arrow.style.headLength = 0.5f;

		std::vector<StructuredError> warnings;
		const SceneObjectsFile loaded = RoundTrip(SingleObject(arrow), warnings);
		EXPECT_TRUE(warnings.empty());
		const auto *got = std::get_if<PersistedSceneArrow>(&loaded.structures.at(0).objects.at(0));
		ASSERT_NE(got, nullptr);
		EXPECT_EQ(got->kind, PersistedArrowKind::Arrow2D);
		EXPECT_EQ(got->orientation2D, PersistedArrow2DOrientation::FixedPlane);
		EXPECT_EQ(got->fixedPlane, PersistedWorldPlane::YZ);
		EXPECT_EQ(got->start, arrow.start);
		EXPECT_EQ(got->end, arrow.end);
		EXPECT_EQ(got->style.color, arrow.style.color);
		EXPECT_FLOAT_EQ(got->style.headLength, 0.5f);
	}

	TEST(SceneObjectsIOTests, SceneOrbitalRoundTrips)
	{
		PersistedSceneOrbital orbital;
		orbital.persistKey = "fedcba9876543210fedcba9876543210";
		orbital.preset = "sp3-sigma*";
		orbital.shell = 3;
		orbital.lobeIndex = 2;
		orbital.effectiveCharge = 3.25f;
		orbital.centerA = glm::vec3(1.0f, -2.0f, 0.5f);
		orbital.centerB = glm::vec3(2.5f, -2.0f, 0.5f);
		orbital.anchorAtoms = {{4, "C", glm::vec3(1.0f, -2.0f, 0.5f)}, {8, "N", glm::vec3(2.5f, -2.0f, 0.5f)}};
		orbital.rotationEuler = glm::vec3(30.0f, 0.0f, -45.0f);
		orbital.scale = 1.75f;
		orbital.isoFraction = 0.35f;
		orbital.resolution = 64;
		orbital.positiveLobeColor = glm::vec3(0.9f, 0.1f, 0.2f);
		orbital.negativeLobeColor = glm::vec3(0.2f, 0.3f, 0.95f);
		orbital.alpha = 0.6f;
		orbital.visible = false;

		std::vector<StructuredError> warnings;
		const SceneObjectsFile loaded = RoundTrip(SingleObject(orbital), warnings);
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto *got = std::get_if<PersistedSceneOrbital>(&loaded.structures[0].objects[0]);
		ASSERT_NE(got, nullptr);
		EXPECT_EQ(got->persistKey, orbital.persistKey);
		EXPECT_EQ(got->preset, "sp3-sigma*");
		EXPECT_EQ(got->shell, 3);
		EXPECT_EQ(got->lobeIndex, 2);
		EXPECT_FLOAT_EQ(got->effectiveCharge, 3.25f);
		EXPECT_EQ(got->centerA, orbital.centerA);
		EXPECT_EQ(got->centerB, orbital.centerB);
		ASSERT_EQ(got->anchorAtoms.size(), 2u);
		EXPECT_EQ(got->anchorAtoms[0].index, 4u);
		EXPECT_EQ(got->anchorAtoms[1].element, "N");
		EXPECT_EQ(got->rotationEuler, orbital.rotationEuler);
		EXPECT_FLOAT_EQ(got->scale, 1.75f);
		EXPECT_FLOAT_EQ(got->isoFraction, 0.35f);
		EXPECT_EQ(got->resolution, 64);
		EXPECT_EQ(got->positiveLobeColor, orbital.positiveLobeColor);
		EXPECT_EQ(got->negativeLobeColor, orbital.negativeLobeColor);
		EXPECT_FLOAT_EQ(got->alpha, 0.6f);
		EXPECT_FALSE(got->visible);
	}

	TEST(SceneObjectsIOTests, AnUnanchoredOrbitalKeepsItsOwnCentres)
	{
		PersistedSceneOrbital orbital;
		orbital.preset = "f";
		orbital.centerA = glm::vec3(-3.0f, 0.25f, 7.5f);

		std::vector<StructuredError> warnings;
		const SceneObjectsFile loaded = RoundTrip(SingleObject(orbital), warnings);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto *got = std::get_if<PersistedSceneOrbital>(&loaded.structures[0].objects[0]);
		ASSERT_NE(got, nullptr);
		EXPECT_TRUE(got->anchorAtoms.empty());
		EXPECT_EQ(got->centerA, orbital.centerA);
		EXPECT_EQ(got->preset, "f");
		// Everything the writer left at its default comes back at that default rather than zeroed.
		EXPECT_TRUE(got->visible);
		EXPECT_FLOAT_EQ(got->scale, 1.0f);
		EXPECT_EQ(got->resolution, 48);
	}

	TEST(SceneObjectsIOTests, UnknownKindAndInvalidEntriesAreSkippedWithWarningsAndDroppedOnResave)
	{
		const std::string text = R"(formatVersion: 1
structures:
  - structureKey: a/POSCAR
    objects:
      - kind: Hologram
        glow: 3
      - kind: FreeLabel
        text: kept
        position: [1, 2, 3]
      - kind: PinnedMeasurement
        atomRefs:
          - {index: 0, element: C, position: [0, 0, 0]}
      - kind: SceneArrow
        kind2: nope
        start: [0, 0, 0]
      - kind: SceneArrow
        arrowKind: Arrow9D
        start: [0, 0, 0]
        end: [1, 0, 0]
  - objects:
      - kind: FreeLabel
        position: [0, 0, 0]
)";
		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(text, loaded, warnings, error)) << error;
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		EXPECT_EQ(std::get<PersistedFreeLabel>(loaded.structures[0].objects[0]).text, "kept");
		ASSERT_GE(warnings.size(), 4u);
		for (const StructuredError &warning : warnings)
		{
			EXPECT_EQ(warning.severity, Severity::Warning);
			EXPECT_EQ(warning.code, "scene_objects.entry_skipped");
		}

		const std::string resaved = SceneObjectsIO::Serialize(loaded);
		EXPECT_EQ(resaved.find("Hologram"), std::string::npos);
		EXPECT_EQ(resaved.find("Arrow9D"), std::string::npos);
	}

	TEST(SceneObjectsIOTests, MissingOptionalFieldsTakeDefaults)
	{
		std::vector<StructuredError> warnings;
		std::string error;
		SceneObjectsFile minimal;
		ASSERT_TRUE(SceneObjectsIO::Parse(
			"formatVersion: 1\nstructures:\n  - structureKey: k\n    objects:\n      - {kind: FreeLabel, position: [0,0,0]}\n",
			minimal, warnings, error)) << error;
		const auto &label = std::get<PersistedFreeLabel>(minimal.structures.at(0).objects.at(0));
		const PersistedFreeLabel defaults;
		EXPECT_EQ(label.text, defaults.text);
		EXPECT_TRUE(label.persistKey.empty());
		EXPECT_EQ(label.style.textColor, defaults.style.textColor);
		EXPECT_FLOAT_EQ(label.style.scale, defaults.style.scale);
	}

	TEST(SceneObjectsIOTests, EmptyTextIsAnEmptyFileAndNonMapRootFails)
	{
		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		EXPECT_TRUE(SceneObjectsIO::Parse("", loaded, warnings, error));
		EXPECT_TRUE(loaded.structures.empty());
		EXPECT_FALSE(SceneObjectsIO::Parse("- just\n- a list\n", loaded, warnings, error));
		EXPECT_FALSE(error.empty());
	}

	TEST(SceneObjectsIOTests, StructureKeyIsProjectRelativeWithForwardSlashes)
	{
		const Path project = Path::FromResolved(FileSystem::TempDirectoryPath()) / "ds_scene_key_project";
		EXPECT_EQ(SceneObjectsIO::MakeStructureKey(project, project / "structures" / "NV" / "POSCAR"), "structures/NV/POSCAR");
		const std::string outside = SceneObjectsIO::MakeStructureKey(project, Path::FromResolved(FileSystem::TempDirectoryPath()) / "elsewhere" / "POSCAR");
		EXPECT_EQ(outside.find('\\'), std::string::npos);
		EXPECT_NE(outside.find("elsewhere/POSCAR"), std::string::npos);
		EXPECT_NE(outside.rfind("..", 0), 0u); // outside the project stays absolute, never "../"
	}
} // namespace DefectStudio::Tests
