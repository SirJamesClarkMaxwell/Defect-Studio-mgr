#include <gtest/gtest.h>

#include <limits>
#include <unordered_set>

#include "IO/SceneObjectsIO.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/ScenePathPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "IO/SceneObjectsV1Fixtures.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStructureData Structure()
		{
			RendererStructureData structure;
			structure.atoms.push_back({"C", {0, 0, 0}});
			structure.atoms.push_back({"C", {1, 0, 0}});
			return structure;
		}

		PersistedScenePath MinimalPath()
		{
			PersistedScenePath path;
			path.nodes = {{{0, 0, 0}}, {{1, 0, 0}}};
			path.segments = {{}};
			return path;
		}

		PersistedSceneArrow Arrow(const char *fixture)
		{
			SceneObjectsFile file;
			std::vector<StructuredError> warnings;
			std::string error;
			EXPECT_TRUE(SceneObjectsIO::Parse(fixture, file, warnings, error)) << error;
			return std::get<PersistedSceneArrow>(file.structures[0].objects[0]);
		}
	}

	TEST(ScenePathPersistenceTests, BuildAllocatesUniqueElementIdsAndExtractsStably)
	{
		PersistedScenePath saved;
		saved.persistKey = "path";
		saved.nodes = {{{0, 0, 0}}, {{1, 0, 0}}};
		saved.segments = {{}};
		saved.nodes[0].binding.kind = "CopyPosition";
		saved.nodes[0].binding.atoms = {{0, "C", {0, 0, 0}}};
		saved.nodes[0].binding.buffer = 1.2f;
		std::vector<StructuredError> warnings;
		const Result<ScenePath> built = BuildScenePath(saved, Structure(), warnings);
		ASSERT_TRUE(built);
		const ScenePath &path = built.Value();
		std::unordered_set<std::uint64_t> ids;
		for (const auto &node : path.nodes) ASSERT_TRUE(ids.insert(node.id.value).second);
		for (const auto &segment : path.segments)
		{
			ASSERT_TRUE(ids.insert(segment.id.value).second);
			if (const auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data))
			{
				ASSERT_TRUE(ids.insert(cubic->startHandle.id.value).second);
				ASSERT_TRUE(ids.insert(cubic->endHandle.id.value).second);
			}
		}
		EXPECT_EQ(path.nextElementId, ids.size() + 1);
		const PersistedScenePath extracted = ExtractPersistedScenePath(path, Structure());
		EXPECT_EQ(extracted.nodes[0].binding.kind, "CopyPosition");
		EXPECT_EQ(extracted.nodes[0].binding.atoms[0].element, "C");
	}

	TEST(ScenePathPersistenceTests, RibbonThicknessRoundTripsThroughScenePathPersistence)
	{
		PersistedScenePath saved = MinimalPath();
		saved.style.profile = "Flat";
		saved.style.ribbonThickness = 0.37f;
		std::vector<StructuredError> warnings;
		const Result<ScenePath> built = BuildScenePath(saved, Structure(), warnings);
		ASSERT_TRUE(built);
		EXPECT_FLOAT_EQ(built.Value().style.ribbonThickness, saved.style.ribbonThickness);

		const PersistedScenePath extracted = ExtractPersistedScenePath(built.Value(), Structure());
		EXPECT_FLOAT_EQ(extracted.style.ribbonThickness, saved.style.ribbonThickness);
	}

	TEST(ScenePathPersistenceTests, InvalidRibbonNormalFallsBackToDefault)
	{
		PersistedScenePath saved;
		saved.nodes = {{{0, 0, 0}}, {{1, 0, 0}}};
		saved.segments = {{}};
		for (const glm::vec3 invalid : {glm::vec3(0.0f), glm::vec3(std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f)})
		{
			saved.style.ribbonNormal = invalid;
			std::vector<StructuredError> warnings;
			const Result<ScenePath> built = BuildScenePath(saved, Structure(), warnings);
			ASSERT_TRUE(built);
			EXPECT_EQ(built.Value().style.ribbonNormal, glm::vec3(0.0f, 1.0f, 0.0f));
		}
	}

	TEST(ScenePathPersistenceTests, UnresolvedAndObjectOriginBindingsBecomeFreeWithWarnings)
	{
		PersistedScenePath saved;
		saved.nodes = {{{3, 3, 3}}, {{4, 3, 3}}};
		saved.segments = {{}};
		saved.nodes[0].binding.kind = "CopyPosition";
		saved.nodes[0].binding.atoms = {{9, "O", {9, 9, 9}}};
		saved.nodes[1].binding.kind = "ObjectOrigin";
		std::vector<StructuredError> warnings;
		const Result<ScenePath> built = BuildScenePath(saved, Structure(), warnings);
		ASSERT_TRUE(built);
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(built.Value().nodes[0].binding.value));
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(built.Value().nodes[1].binding.value));
		const ResolvedNodes resolved = ResolveNodePositions(built.Value(), BindingContext{});
		ASSERT_EQ(resolved.positions.size(), built.Value().nodes.size());
		ASSERT_FALSE(resolved.positions.empty());
		EXPECT_NEAR(glm::distance(resolved.positions[0], glm::vec3(3, 3, 3)), 0.0f, 1.0e-5f);
		ASSERT_EQ(warnings.size(), 2u);
		EXPECT_EQ(warnings[0].code, "scene_objects.path_binding_unresolved");
	}

	TEST(ScenePathPersistenceTests, MigrationElevatesQuadraticAndMaps2DDepthAndWidth)
	{
		PersistedSceneArrow arrow = Arrow(SceneObjectsV1Fixtures::Quadratic);
		arrow.style.shaftWidth = 0.2f;
		std::vector<StructuredError> warnings;
		const Result<ScenePathMigration> migrated = MigrateArrowToPath(arrow);
		ASSERT_TRUE(migrated);
		ASSERT_TRUE(std::holds_alternative<CubicBezierSegmentData>(migrated.Value().path.segments[0].data));
		const auto &cubic = std::get<CubicBezierSegmentData>(migrated.Value().path.segments[0].data);
		ASSERT_TRUE(arrow.controlPoint.has_value());
		const glm::vec3 expectedStart = arrow.points[0] + (2.0f / 3.0f) * (*arrow.controlPoint - arrow.points[0]);
		const glm::vec3 expectedEnd = arrow.points[1] + (2.0f / 3.0f) * (*arrow.controlPoint - arrow.points[1]);
		EXPECT_NEAR(glm::distance(migrated.Value().path.nodes[0].position + cubic.startHandle.offset, expectedStart), 0.0f, 1.0e-6f);
		EXPECT_NEAR(glm::distance(migrated.Value().path.nodes[1].position + cubic.endHandle.offset, expectedEnd), 0.0f, 1.0e-6f);
		EXPECT_FLOAT_EQ(migrated.Value().path.style.width, 0.4f);

		arrow = Arrow(SceneObjectsV1Fixtures::Arrow2DBillboard);
		const Result<ScenePathMigration> billboard = MigrateArrowToPath(arrow);
		ASSERT_TRUE(billboard);
		EXPECT_EQ(billboard.Value().path.style.profile, StrokeProfile::CameraFacing);
		EXPECT_EQ(billboard.Value().path.style.depthMode, PathDepthMode::AlwaysOnTop);
		arrow = Arrow(SceneObjectsV1Fixtures::Arrow2DFixedPlane);
		const Result<ScenePathMigration> fixed = MigrateArrowToPath(arrow);
		ASSERT_TRUE(fixed);
		EXPECT_EQ(fixed.Value().path.style.profile, StrokeProfile::Flat);
		EXPECT_EQ(fixed.Value().warnings.size(), 1u);
	}

	TEST(ScenePathPersistenceTests, MigrationMapsTipsGradientAnchorsAndLosses)
	{
		PersistedSceneArrow arrow = Arrow(SceneObjectsV1Fixtures::TipsGradientOutline);
		arrow.startAnchorAtoms = {{0, "C", {0, 0, 0}}};
		arrow.endAnchorAtoms = {{1, "C", {1, 0, 0}}};
		arrow.curveSegments = 12;
		const Result<ScenePathMigration> migrated = MigrateArrowToPath(arrow);
		ASSERT_TRUE(migrated);
		const ScenePath &path = migrated.Value().path;
		EXPECT_EQ(path.style.startDecoration.kind, PathDecorationKind::Circle);
		EXPECT_EQ(path.style.endDecoration.kind, PathDecorationKind::Stealth);
		EXPECT_TRUE(path.style.gradient.enabled);
		EXPECT_EQ(path.style.gradient.stops.size(), 2u);
		EXPECT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(path.nodes.front().binding.value));
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(path.nodes.back().binding.value).buffer, arrow.atomBuffer);
		EXPECT_EQ(migrated.Value().warnings.size(), 2u);
	}

	TEST(ScenePathPersistenceTests, OpenArrowLoadsHollowAndIsWrittenAsArrow)
	{
		PersistedScenePath saved = MinimalPath();
		saved.style.startDecoration = "OpenArrow";
		saved.style.endDecoration = "OpenArrow";
		std::vector<StructuredError> warnings;
		const Result<ScenePath> built = BuildScenePath(saved, Structure(), warnings);
		ASSERT_TRUE(built);
		const ScenePath &path = built.Value();
		EXPECT_EQ(path.style.startDecoration.kind, PathDecorationKind::Arrow);
		EXPECT_EQ(path.style.endDecoration.kind, PathDecorationKind::Arrow);
		EXPECT_FALSE(path.style.startDecoration.filled);
		EXPECT_FALSE(path.style.endDecoration.filled);

		const PersistedScenePath extracted = ExtractPersistedScenePath(path, Structure());
		EXPECT_EQ(extracted.style.startDecoration, "Arrow");
		EXPECT_EQ(extracted.style.endDecoration, "Arrow");
		EXPECT_FALSE(extracted.style.startDecorationFilled);
		EXPECT_FALSE(extracted.style.endDecorationFilled);
		SceneObjectsFile file;
		file.structures.push_back({"k", {extracted}});
		const std::string serialized = SceneObjectsIO::Serialize(file);
		EXPECT_EQ(serialized.find("OpenArrow"), std::string::npos);
		EXPECT_NE(serialized.find("start_decoration_filled"), std::string::npos);
		EXPECT_NE(serialized.find("Arrow"), std::string::npos);
	}

	TEST(ScenePathPersistenceTests, EveryCurrentDecorationNameRoundTrips)
	{
		const char *names[] = {"None", "Arrow", "Stealth", "Latex", "Bar", "Circle", "Square", "Diamond", "Kite"};
		for (const char *name : names)
		{
			PersistedScenePath saved = MinimalPath();
			saved.style.startDecoration = name;
			saved.style.endDecoration = name;
			std::vector<StructuredError> warnings;
			const Result<ScenePath> built = BuildScenePath(saved, Structure(), warnings);
			ASSERT_TRUE(built) << name;
			const PersistedScenePath extracted = ExtractPersistedScenePath(built.Value(), Structure());
			EXPECT_EQ(extracted.style.startDecoration, name);
			EXPECT_EQ(extracted.style.endDecoration, name);
		}
	}

	TEST(ScenePathPersistenceTests, LegacyOpenTipMigratesToHollowArrow)
	{
		PersistedSceneArrow arrow;
		arrow.points = {{0, 0, 0}, {1, 0, 0}};
		arrow.startTip = "Open";
		arrow.endTip = "Open";
		const Result<ScenePathMigration> migrated = MigrateArrowToPath(arrow);
		ASSERT_TRUE(migrated);
		const ScenePath &path = migrated.Value().path;
		EXPECT_EQ(path.style.startDecoration.kind, PathDecorationKind::Arrow);
		EXPECT_EQ(path.style.endDecoration.kind, PathDecorationKind::Arrow);
		EXPECT_FALSE(path.style.startDecoration.filled);
		EXPECT_FALSE(path.style.endDecoration.filled);
	}

	TEST(ScenePathPersistenceTests, ApplyAndExtractUseThePathStore)
	{
		RendererWindowState window;
		window.structure = Structure();
		PersistedScenePath saved;
		saved.persistKey = "path";
		saved.nodes = {{{0, 0, 0}}, {{1, 0, 0}}};
		saved.segments = {{}};
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {saved}, warnings);
		ASSERT_TRUE(warnings.empty());
		ASSERT_NE(window.paths, nullptr);
		ASSERT_EQ(window.paths->Store().Size(), 1u);
		const auto objects = ExtractPersistedSceneObjects(window);
		ASSERT_EQ(objects.size(), 1u);
		EXPECT_EQ(std::get<PersistedScenePath>(objects[0]).persistKey, "path");
		EXPECT_TRUE(ValidatePath(*window.paths->Store().At(0)).empty());
	}

	// Two keyless paths in one window must not end up sharing an identity: MergeWindowSceneObjects
	// keys on persistKey, so a shared key silently drops every path but the last one on save.
	TEST(ScenePathPersistenceTests, KeylessPathsGetDistinctPersistKeys)
	{
		RendererWindowState window;
		window.structure = Structure();
		PathSystem &paths = SceneSystem::EnsurePathSystem(window);
		for (int index = 0; index < 2; ++index)
		{
			ScenePath path;
			path.id = window.sceneRegistry.AllocateObjectId();
			path.nodes = {{AllocateElementId(path), {0, 0, 0}, {}}, {AllocateElementId(path), {1, 0, 0}, {}}};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments.push_back(std::move(segment));
			ASSERT_TRUE(paths.Store().Insert(std::move(path)));
		}
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		const auto objects = ExtractPersistedSceneObjects(window);
		ASSERT_EQ(objects.size(), 2u);
		const std::string first = std::get<PersistedScenePath>(objects[0]).persistKey;
		const std::string second = std::get<PersistedScenePath>(objects[1]).persistKey;
		EXPECT_FALSE(first.empty());
		EXPECT_FALSE(second.empty());
		EXPECT_NE(first, second);
	}

	TEST(ScenePathPersistenceTests, InvalidMigrationHasNoPartialPath)
	{
		PersistedSceneArrow arrow;
		arrow.points = {{0, 0, 0}};
		const Result<ScenePathMigration> result = MigrateArrowToPath(arrow);
		EXPECT_FALSE(result);
		arrow.points = {{0, 0, 0}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}};
		EXPECT_FALSE(MigrateArrowToPath(arrow));
		// The control point is a coordinate too, and it is the one that reaches the path as a handle
		// rather than as a node.
		arrow.points = {{0, 0, 0}, {1, 0, 0}};
		arrow.controlPoint = glm::vec3(0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f);
		EXPECT_FALSE(MigrateArrowToPath(arrow));
	}
}
