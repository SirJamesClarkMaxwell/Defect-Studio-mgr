#include <gtest/gtest.h>

#include "IO/SceneObjectsIO.hpp"
#include "IO/TextFileIO.hpp"
#include "SceneObjectsV1Fixtures.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		SceneObjectsFile Parse(const char *text)
		{
			SceneObjectsFile file;
			std::vector<StructuredError> warnings;
			std::string error;
			EXPECT_TRUE(SceneObjectsIO::Parse(text, file, warnings, error)) << error;
			return file;
		}
	}

	TEST(SceneObjectsPathIOTests, VersionTwoPathRoundTripsAllSegmentKindsAndStyle)
	{
		PersistedScenePath path;
		path.persistKey = "path";
		path.nodes = {{{0, 0, 0}}, {{1, 0, 0}}, {{2, 0, 0}}};
		path.segments.resize(2);
		path.segments[0].kind = PersistedPathSegmentKind::Cubic;
		path.segments[0].startHandle = {0.2f, 0.5f, 0.0f};
		path.segments[0].endHandle = {0.8f, 0.5f, 0.0f};
		path.segments[0].startHandleType = "Free";
		path.segments[0].endHandleType = "Aligned";
		path.segments[1].kind = PersistedPathSegmentKind::Arc;
		path.segments[1].planeNormal = {0, 0, 1};
		path.segments[1].signedSweepRadians = 1.2f;
		path.style.profile = "CameraFacing";
		path.style.ribbonNormal = {0.2f, 0.3f, 0.4f};
		path.style.ribbonThickness = 0.37f;
		path.style.width = 0.25f;
		path.style.join = "Round";
		path.style.cap = "Round";
		path.style.radialSegments = 24;
		path.style.color = {0.1f, 0.2f, 0.3f};
		path.style.alpha = 0.7f;
		path.style.dashEnabled = true;
		path.style.dashLength = 0.4f;
		path.style.gapLength = 0.2f;
		path.style.dashPhase = 0.1f;
		path.style.gradientEnabled = true;
		path.style.gradientStops = {{0, {1, 0, 0}, 0.5f}, {0.5f, {0, 1, 0}, 0.75f}, {1, {0, 0, 1}, 1.0f}};
		path.style.startDecoration = "Circle";
		path.style.startDecorationLengthScale = 2.0f;
		path.style.startDecorationWidthScale = 3.0f;
		path.style.startDecorationFilled = false;
		path.style.endDecoration = "Diamond";
		path.style.endDecorationLengthScale = 4.0f;
		path.style.endDecorationWidthScale = 5.0f;
		path.style.endDecorationFilled = true;
		path.style.depthMode = "AlwaysOnTop";
		SceneObjectsFile file;
		file.structures.push_back({"k", {path}});
		const SceneObjectsFile loaded = Parse(SceneObjectsIO::Serialize(file).c_str());
		ASSERT_EQ(loaded.formatVersion, 2);
		const auto &got = std::get<PersistedScenePath>(loaded.structures[0].objects[0]);
		EXPECT_EQ(got.nodes[1].position, path.nodes[1].position);
		EXPECT_EQ(got.segments[0].startHandleType, "Free");
		EXPECT_EQ(got.segments[1].planeNormal, path.segments[1].planeNormal);
		EXPECT_FLOAT_EQ(got.style.gradientStops[1].alpha, 0.75f);
		EXPECT_EQ(got.style.endDecoration, "Diamond");
		EXPECT_FALSE(got.style.startDecorationFilled);
		EXPECT_TRUE(got.style.endDecorationFilled);
		EXPECT_EQ(got.style.depthMode, "AlwaysOnTop");
		EXPECT_EQ(got.style.ribbonNormal, path.style.ribbonNormal);
		EXPECT_FLOAT_EQ(got.style.ribbonThickness, path.style.ribbonThickness);
	}

	TEST(SceneObjectsPathIOTests, MissingRibbonNormalKeepsV2AndUsesDefault)
	{
		PersistedScenePath path;
		path.nodes = {{{0, 0, 0}}, {{1, 0, 0}}};
		path.segments = {{}};
		SceneObjectsFile source;
		source.structures.push_back({"k", {path}});
		std::string text = SceneObjectsIO::Serialize(source);
		// Erase the WHOLE line, indentation included. Cutting just the key leaves its leading
		// whitespace behind for the next line to glue itself onto, which yaml-cpp reports as an
		// illegal map value - a broken fixture masquerading as a broken serializer.
		const std::size_t key = text.find("ribbon_normal:");
		ASSERT_NE(key, std::string::npos);
		const std::size_t lineStart = text.rfind('\n', key);
		ASSERT_NE(lineStart, std::string::npos);
		const std::size_t lineEnd = text.find('\n', key);
		ASSERT_NE(lineEnd, std::string::npos);
		text.erase(lineStart, lineEnd - lineStart);
		const SceneObjectsFile loaded = Parse(text.c_str());
		ASSERT_EQ(loaded.formatVersion, 2);
		// Asserted, not expected: a parse failure otherwise walks off the end of these vectors and
		// takes the whole suite process down with an access violation instead of failing one test.
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto &got = std::get<PersistedScenePath>(loaded.structures[0].objects[0]);
		EXPECT_EQ(got.style.ribbonNormal, glm::vec3(0.0f, 1.0f, 0.0f));
	}

	TEST(SceneObjectsPathIOTests, MissingRibbonThicknessKeepsV2AndUsesZeroDefault)
	{
		PersistedScenePath path;
		path.nodes = {{{0, 0, 0}}, {{1, 0, 0}}};
		path.segments = {{}};
		path.style.ribbonThickness = 0.37f;
		SceneObjectsFile source;
		source.structures.push_back({"k", {path}});
		std::string text = SceneObjectsIO::Serialize(source);
		// Erase the whole line, including indentation. Removing only the key leaves malformed YAML
		// and tests the fixture surgery instead of the additive default.
		const std::size_t key = text.find("ribbon_thickness:");
		ASSERT_NE(key, std::string::npos);
		const std::size_t lineStart = text.rfind('\n', key);
		ASSERT_NE(lineStart, std::string::npos);
		const std::size_t lineEnd = text.find('\n', key);
		ASSERT_NE(lineEnd, std::string::npos);
		text.erase(lineStart, lineEnd - lineStart);

		const SceneObjectsFile loaded = Parse(text.c_str());
		ASSERT_EQ(loaded.formatVersion, 2);
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto &got = std::get<PersistedScenePath>(loaded.structures[0].objects[0]);
		EXPECT_FLOAT_EQ(got.style.ribbonThickness, 0.0f);
	}

	TEST(SceneObjectsPathIOTests, MissingDecorationFilledKeepsV2AndUsesTrueDefault)
	{
		PersistedScenePath path;
		path.nodes = {{{0, 0, 0}}, {{1, 0, 0}}};
		path.segments = {{}};
		path.style.startDecoration = "Arrow";
		path.style.startDecorationFilled = false;
		path.style.endDecoration = "Circle";
		path.style.endDecorationFilled = false;
		SceneObjectsFile source;
		source.structures.push_back({"k", {path}});
		std::string text = SceneObjectsIO::Serialize(source);
		// Remove the whole key line, including indentation. Removing only the key would leave
		// whitespace that yaml-cpp glues to the following field and turns this into a malformed fixture.
		const auto eraseWholeLine = [&](const char *field) {
			const std::size_t key = text.find(field);
			if (key == std::string::npos)
				return false;
			const std::size_t lineStart = text.rfind('\n', key);
			if (lineStart == std::string::npos)
				return false;
			const std::size_t lineEnd = text.find('\n', key);
			if (lineEnd == std::string::npos)
				return false;
			text.erase(lineStart, lineEnd - lineStart);
			return true;
		};
		ASSERT_TRUE(eraseWholeLine("start_decoration_filled:"));
		ASSERT_TRUE(eraseWholeLine("end_decoration_filled:"));

		const SceneObjectsFile loaded = Parse(text.c_str());
		ASSERT_EQ(loaded.formatVersion, 2);
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto &got = std::get<PersistedScenePath>(loaded.structures[0].objects[0]);
		EXPECT_EQ(got.style.startDecoration, "Arrow");
		EXPECT_TRUE(got.style.startDecorationFilled);
		EXPECT_EQ(got.style.endDecoration, "Circle");
		EXPECT_TRUE(got.style.endDecorationFilled);
	}

	TEST(SceneObjectsPathIOTests, FutureVersionIsRejectedBeforeInterpretingObjects)
	{
		SceneObjectsFile file;
		file.structures.push_back({"sentinel", {PersistedFreeLabel{}}});
		std::vector<StructuredError> warnings;
		std::string error;
		EXPECT_FALSE(SceneObjectsIO::Parse("formatVersion: 3\nstructures: nope\n", file, warnings, error));
		EXPECT_TRUE(file.structures.empty());
		EXPECT_FALSE(error.empty());
		ASSERT_EQ(warnings.size(), 1u);
		EXPECT_EQ(warnings[0].code, "scene_objects.future_format_version");
	}

	TEST(SceneObjectsPathIOTests, InvalidPathEntriesAreSkippedButV1ObjectsStillParse)
	{
		const std::string text = "formatVersion: 2\nstructures:\n  - structureKey: k\n    objects:\n      - kind: ScenePath\n        segments: []\n      - kind: FreeLabel\n        position: [1, 2, 3]\n";
		SceneObjectsFile file = Parse(text.c_str());
		ASSERT_EQ(file.structures[0].objects.size(), 1u);
		EXPECT_TRUE(std::holds_alternative<PersistedFreeLabel>(file.structures[0].objects[0]));
	}

	TEST(SceneObjectsPathIOTests, V1FixturesParseAsArrows)
	{
		for (const char *fixture : {SceneObjectsV1Fixtures::Line, SceneObjectsV1Fixtures::Arrow2DBillboard,
			SceneObjectsV1Fixtures::Arrow2DFixedPlane, SceneObjectsV1Fixtures::Arrow3D,
			SceneObjectsV1Fixtures::Quadratic, SceneObjectsV1Fixtures::TipsGradientOutline,
			SceneObjectsV1Fixtures::Anchored})
		{
			const SceneObjectsFile file = Parse(fixture);
			ASSERT_EQ(file.structures.size(), 1u);
			ASSERT_EQ(file.structures[0].objects.size(), 1u);
			EXPECT_TRUE(std::holds_alternative<PersistedSceneArrow>(file.structures[0].objects[0]));
		}
	}

	TEST(SceneObjectsPathIOTests, V1FixtureCoversEveryLegacyTipName)
	{
		const SceneObjectsFile file = Parse(SceneObjectsV1Fixtures::AllTips);
		ASSERT_EQ(file.structures[0].objects.size(), 6u);
		for (const auto &object : file.structures[0].objects)
			EXPECT_TRUE(std::holds_alternative<PersistedSceneArrow>(object));
	}

	TEST(SceneObjectsPathIOTests, V1BackupIsCreatedOnceAndSaveUsesItBeforeReplacement)
	{
		const Path directory = Path::FromResolved(FileSystem::TempDirectoryPath()) / "ds_scene_path_backup";
		std::error_code ignored;
		FileSystem::RemoveAll(directory.Native(), ignored);
		const Path target = SceneObjectsIO::FilePath(directory);
		const std::string original = "formatVersion: 1\nstructures: []\n";
		std::string error;
		ASSERT_TRUE(TextFileIO::Save(target, original, error)) << error;
		ASSERT_TRUE(SceneObjectsIO::WriteBackupOnce(directory, error)) << error;
		std::string backup;
		ASSERT_TRUE(TextFileIO::Load(Path::FromResolved(target.Native().string() + ".v1.bak"), backup, error));
		EXPECT_EQ(backup, original);
		ASSERT_TRUE(TextFileIO::Save(Path::FromResolved(target.Native().string() + ".v1.bak"), "keep", error));
		ASSERT_TRUE(SceneObjectsIO::WriteBackupOnce(directory, error));
		ASSERT_TRUE(TextFileIO::Load(Path::FromResolved(target.Native().string() + ".v1.bak"), backup, error));
		EXPECT_EQ(backup, "keep");
		FileSystem::RemoveAll(directory.Native(), ignored);
	}
}
