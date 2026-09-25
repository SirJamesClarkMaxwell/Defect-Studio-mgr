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
		EXPECT_EQ(built.Value().nodes[0].position, glm::vec3(3, 3, 3));
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
		// Component-wise: the migration reaches these values through P + 2/3 (Q - P), which lands one
		// ULP away from the literal thirds an exact comparison would demand.
		EXPECT_FLOAT_EQ(cubic.startHandle.position.x, 1.0f / 3.0f);
		EXPECT_FLOAT_EQ(cubic.startHandle.position.y, 2.0f / 3.0f);
		EXPECT_FLOAT_EQ(cubic.startHandle.position.z, 0.0f);
		EXPECT_FLOAT_EQ(cubic.endHandle.position.x, 2.0f / 3.0f);
		EXPECT_FLOAT_EQ(cubic.endHandle.position.y, 2.0f / 3.0f);
		EXPECT_FLOAT_EQ(cubic.endHandle.position.z, 0.0f);
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
