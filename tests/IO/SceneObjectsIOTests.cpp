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

	TEST(SceneObjectsIOTests, Pre32bArrowEndpointsMigrateToPointsAndKindSpecificTips)
	{
		const std::string legacy = R"(formatVersion: 1
structures:
  - structureKey: structures/NV/POSCAR
    objects:
      - kind: SceneArrow
        arrowKind: Line
        start: [1, 2, 3]
        end: [4, 5, 6]
      - kind: SceneArrow
        arrowKind: Arrow2D
        start: [-1, 0, 2]
        end: [3, 4, 5]
      - kind: SceneArrow
        arrowKind: Arrow3D
        start: [0, 1, 0]
        end: [0, 1, 7]
)";

		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(legacy, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 3u);

		const auto *line = std::get_if<PersistedSceneArrow>(&loaded.structures[0].objects[0]);
		const auto *arrow2D = std::get_if<PersistedSceneArrow>(&loaded.structures[0].objects[1]);
		const auto *arrow3D = std::get_if<PersistedSceneArrow>(&loaded.structures[0].objects[2]);
		ASSERT_NE(line, nullptr);
		ASSERT_NE(arrow2D, nullptr);
		ASSERT_NE(arrow3D, nullptr);
		ASSERT_EQ(line->points.size(), 2u);
		EXPECT_EQ(line->points[0], glm::vec3(1.0f, 2.0f, 3.0f));
		EXPECT_EQ(line->points[1], glm::vec3(4.0f, 5.0f, 6.0f));
		EXPECT_EQ(line->startTip, "None");
		EXPECT_EQ(line->endTip, "None");
		ASSERT_EQ(arrow2D->points.size(), 2u);
		EXPECT_EQ(arrow2D->points[0], glm::vec3(-1.0f, 0.0f, 2.0f));
		EXPECT_EQ(arrow2D->points[1], glm::vec3(3.0f, 4.0f, 5.0f));
		EXPECT_EQ(arrow2D->startTip, "None");
		EXPECT_EQ(arrow2D->endTip, "Plain");
		ASSERT_EQ(arrow3D->points.size(), 2u);
		EXPECT_EQ(arrow3D->points[0], glm::vec3(0.0f, 1.0f, 0.0f));
		EXPECT_EQ(arrow3D->points[1], glm::vec3(0.0f, 1.0f, 7.0f));
		EXPECT_EQ(arrow3D->startTip, "None");
		EXPECT_EQ(arrow3D->endTip, "Plain");
	}

	TEST(SceneObjectsIOTests, FourPointArrowPathRoundTripsEveryFieldAndTipStrings)
	{
		PersistedSceneArrow arrow;
		arrow.persistKey = "00000000000000000000000000000001";
		arrow.kind = PersistedArrowKind::Arrow3D;
		arrow.orientation2D = PersistedArrow2DOrientation::FixedPlane;
		arrow.fixedPlane = PersistedWorldPlane::YZ;
		arrow.points = {
			glm::vec3(1.0f, 2.0f, 3.0f),
			glm::vec3(4.0f, 5.0f, 6.0f),
			glm::vec3(7.0f, 8.0f, 9.0f),
			glm::vec3(10.0f, 11.0f, 12.0f)};
		arrow.controlPoint = glm::vec3(2.0f, 7.0f, 4.0f);
		arrow.curveSegments = 17;
		arrow.startTip = "Barbed";
		arrow.endTip = "Circle";
		arrow.startAnchorAtoms = {{3, "C", glm::vec3(1.0f, 2.0f, 3.0f)}};
		arrow.endAnchorAtoms = {{7, "O", glm::vec3(10.0f, 11.0f, 12.0f)}};
		arrow.atomBuffer = 0.65f;
		arrow.style.color = glm::vec3(0.2f, 0.4f, 0.6f);
		arrow.style.headLength = 0.5f;
		arrow.style.dashed = true;
		arrow.style.dashLength = 0.4f;
		arrow.style.gapLength = 0.2f;

		std::vector<StructuredError> warnings;
		const SceneObjectsFile source = SingleObject(arrow);
		const std::string serialized = SceneObjectsIO::Serialize(source);
		EXPECT_NE(serialized.find("start_tip: Barbed"), std::string::npos);
		EXPECT_NE(serialized.find("end_tip: Circle"), std::string::npos);
		SceneObjectsFile loaded;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(serialized, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		const auto *got = std::get_if<PersistedSceneArrow>(&loaded.structures.at(0).objects.at(0));
		ASSERT_NE(got, nullptr);
		EXPECT_EQ(got->kind, PersistedArrowKind::Arrow3D);
		EXPECT_EQ(got->orientation2D, PersistedArrow2DOrientation::FixedPlane);
		EXPECT_EQ(got->fixedPlane, PersistedWorldPlane::YZ);
		EXPECT_EQ(got->points, arrow.points);
		ASSERT_TRUE(got->controlPoint.has_value());
		EXPECT_EQ(*got->controlPoint, *arrow.controlPoint);
		EXPECT_EQ(got->curveSegments, 17);
		EXPECT_EQ(got->startTip, "Barbed");
		EXPECT_EQ(got->endTip, "Circle");
		ASSERT_EQ(got->startAnchorAtoms.size(), 1u);
		ASSERT_EQ(got->endAnchorAtoms.size(), 1u);
		EXPECT_EQ(got->startAnchorAtoms[0].index, 3u);
		EXPECT_EQ(got->endAnchorAtoms[0].element, "O");
		EXPECT_FLOAT_EQ(got->atomBuffer, 0.65f);
		EXPECT_EQ(got->style.color, arrow.style.color);
		EXPECT_FLOAT_EQ(got->style.headLength, 0.5f);
		EXPECT_TRUE(got->style.dashed);
		EXPECT_FLOAT_EQ(got->style.dashLength, 0.4f);
		EXPECT_FLOAT_EQ(got->style.gapLength, 0.2f);
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
		orbital.phaseFlipped = true;
		orbital.scale = 1.75f;
		orbital.stretch = glm::vec3(0.75f, 1.25f, 2.5f);
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
		EXPECT_TRUE(got->phaseFlipped);
		EXPECT_FLOAT_EQ(got->scale, 1.75f);
		EXPECT_EQ(got->stretch, orbital.stretch);
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
		EXPECT_FALSE(got->phaseFlipped);
		EXPECT_FLOAT_EQ(got->scale, 1.0f);
		EXPECT_EQ(got->resolution, 48);
	}

	TEST(SceneObjectsIOTests, OrbitalWithoutStretchLoadsWithUnitStretch)
	{
		std::vector<StructuredError> warnings;
		std::string error;
		SceneObjectsFile loaded;
		ASSERT_TRUE(SceneObjectsIO::Parse(
			"formatVersion: 1\nstructures:\n  - structureKey: k\n    objects:\n"
			"      - {kind: SceneOrbital, preset: p, centerA: [0, 0, 0], scale: 1.5}\n",
			loaded, warnings, error)) << error;
		ASSERT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto &orbital = std::get<PersistedSceneOrbital>(loaded.structures[0].objects[0]);
		EXPECT_EQ(orbital.stretch, glm::vec3(1.0f));
		EXPECT_FLOAT_EQ(orbital.scale, 1.5f);
	}

	TEST(SceneObjectsIOTests, ScenePlaneRoundTrips)
	{
		PersistedScenePlane plane;
		plane.persistKey = "00112233445566778899aabbccddeeff";
		plane.center = glm::vec3(1.5f, -0.5f, 3.0f);
		plane.normal = glm::vec3(0.0f, 1.0f, 0.0f);
		plane.tangent = glm::vec3(0.0f, 0.0f, 1.0f);
		plane.halfExtents = glm::vec2(4.0f, 2.5f);
		plane.anchorAtoms = {{2, "C", glm::vec3(1.0f, 2.0f, 3.0f)},
			{5, "N", glm::vec3(4.0f, 5.0f, 6.0f)}, {9, "O", glm::vec3(7.0f, 8.0f, 9.0f)}};
		plane.color = glm::vec3(0.2f, 0.8f, 0.4f);
		plane.alpha = 0.5f;
		plane.showBorder = false;
		plane.visible = false;

		std::vector<StructuredError> warnings;
		const SceneObjectsFile loaded = RoundTrip(SingleObject(plane), warnings);
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto *got = std::get_if<PersistedScenePlane>(&loaded.structures[0].objects[0]);
		ASSERT_NE(got, nullptr);
		EXPECT_EQ(got->persistKey, plane.persistKey);
		EXPECT_EQ(got->center, plane.center);
		EXPECT_EQ(got->normal, plane.normal);
		EXPECT_EQ(got->tangent, plane.tangent);
		EXPECT_EQ(got->halfExtents, plane.halfExtents);
		ASSERT_EQ(got->anchorAtoms.size(), 3u);
		EXPECT_EQ(got->anchorAtoms[0].index, 2u);
		EXPECT_EQ(got->anchorAtoms[1].element, "N");
		EXPECT_EQ(got->anchorAtoms[2].position, glm::vec3(7.0f, 8.0f, 9.0f));
		EXPECT_EQ(got->color, plane.color);
		EXPECT_FLOAT_EQ(got->alpha, 0.5f);
		EXPECT_FALSE(got->showBorder);
		EXPECT_FALSE(got->visible);
	}

	TEST(SceneObjectsIOTests, AnArrowGradientSurvivesAndAnOldArrowStaysFlat)
	{
		PersistedSceneArrow arrow;
		arrow.points = {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 2.0f)};
		arrow.style.useGradient = true;
		arrow.style.gradientStart = glm::vec3(1.0f, 0.0f, 0.0f);
		arrow.style.gradientFinish = glm::vec3(0.0f, 0.0f, 1.0f);

		std::vector<StructuredError> warnings;
		const SceneObjectsFile loaded = RoundTrip(SingleObject(arrow), warnings);
		const auto *got = std::get_if<PersistedSceneArrow>(&loaded.structures[0].objects[0]);
		ASSERT_NE(got, nullptr);
		EXPECT_TRUE(got->style.useGradient);
		EXPECT_EQ(got->style.gradientStart, arrow.style.gradientStart);
		EXPECT_EQ(got->style.gradientFinish, arrow.style.gradientFinish);

		// A file written before the gradient existed has no such keys; the arrow must come back
		// flat rather than with a gradient nobody asked for.
		SceneObjectsFile parsed;
		std::string error;
		std::vector<StructuredError> oldWarnings;
		const std::string legacy =
			"formatVersion: 1\n"
			"structures:\n"
			"  - structureKey: structures/NV/POSCAR\n"
			"    objects:\n"
			"      - kind: SceneArrow\n"
			"        arrowKind: Arrow3D\n"
			"        start: [0, 0, 0]\n"
			"        end: [0, 0, 1]\n";
		ASSERT_TRUE(SceneObjectsIO::Parse(legacy, parsed, oldWarnings, error)) << error;
		ASSERT_EQ(parsed.structures[0].objects.size(), 1u);
		const auto *legacyArrow = std::get_if<PersistedSceneArrow>(&parsed.structures[0].objects[0]);
		ASSERT_NE(legacyArrow, nullptr);
		EXPECT_FALSE(legacyArrow->style.useGradient);
		EXPECT_FALSE(legacyArrow->style.dashed);
		EXPECT_FLOAT_EQ(legacyArrow->style.dashLength, 0.25f);
		EXPECT_FLOAT_EQ(legacyArrow->style.gapLength, 0.15f);
		EXPECT_TRUE(legacyArrow->startAnchorAtoms.empty());
		EXPECT_TRUE(legacyArrow->endAnchorAtoms.empty());
		EXPECT_FLOAT_EQ(legacyArrow->atomBuffer, 1.15f);
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
