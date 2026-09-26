#include "Core/dspch.hpp"

#include <cmath>
#include <optional>

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/Scene/SceneTransform.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		void ExpectVec3Near(const glm::vec3 &actual, const glm::vec3 &expected)
		{
			EXPECT_NEAR(actual.x, expected.x, 1.0e-5f);
			EXPECT_NEAR(actual.y, expected.y, 1.0e-5f);
			EXPECT_NEAR(actual.z, expected.z, 1.0e-5f);
		}
	}

	// Criteria 7 and 8: path G/R/S composes onto the object transform, and Local uses its rotation.
	TEST(SceneTransformPathsTests, ComposesObjectTransformWithoutRewritingGeometryAndRestoresIt)
	{
		RendererWindowState window;
		ScenePath path;
		path.id = window.sceneRegistry.AllocateObjectId();
		path.transform.position = glm::vec3(2.0f, -3.0f, 1.0f);
		path.transform.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
		path.transform.scale = glm::vec3(1.5f, 0.75f, 2.0f);
		PathNode a;
		a.id = AllocateElementId(path);
		a.position = glm::vec3(0.0f);
		PathNode b;
		b.id = AllocateElementId(path);
		b.position = glm::vec3(2.0f, 0.0f, 0.0f);
		path.nodes = {a, b};
		PathSegment segment;
		segment.id = AllocateElementId(path);
		CubicBezierSegmentData cubic;
		cubic.startHandle.id = AllocateElementId(path);
		const glm::vec3 startHandleWorld{0.5f, 1.0f, 0.0f};
		cubic.startHandle.offset = startHandleWorld - a.position;
		cubic.endHandle.id = AllocateElementId(path);
		const glm::vec3 endHandleWorld{1.5f, 1.0f, 0.0f};
		cubic.endHandle.offset = endHandleWorld - b.position;
		segment.data = cubic;
		path.segments = {segment};
		SceneSystem::EnsurePathSystem(window).Store().Insert(path);
		window.selectedScenePaths = {path.id};

		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		ASSERT_EQ(snapshot.paths.size(), 1u);
		EXPECT_EQ(snapshot.paths[0].id, path.id);
		EXPECT_EQ(snapshot.paths[0].transform.position, path.transform.position);
		EXPECT_EQ(snapshot.paths[0].transform.scale, path.transform.scale);
		EXPECT_TRUE(HasSceneObjectTransformTargets(snapshot));
		const std::optional<glm::mat3> localBasis = SceneTransformLocalBasis(snapshot);
		ASSERT_TRUE(localBasis.has_value());
		const glm::mat3 expectedBasis(snapshot.paths[0].transform.rotation);
		for (int column = 0; column < 3; ++column)
			for (int row = 0; row < 3; ++row)
				EXPECT_NEAR((*localBasis)[column][row], expectedBasis[column][row], 1.0e-5f);

		const ScenePath authored = *window.paths->Store().Find(path.id);
		ASSERT_EQ(authored.nodes.size(), 2u);
		ASSERT_EQ(authored.segments.size(), 1u);

		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(1.0f, 2.0f, 0.0f);
		ApplySceneTransformSelection(window, snapshot, delta, ModalTransformOp::Translate,
			TransformPivotMode::Median, glm::vec3(0.0f));
		const ScenePath *moved = window.paths->Store().Find(path.id);
		ASSERT_NE(moved, nullptr);
		EXPECT_EQ(moved->nodes[0].position, authored.nodes[0].position);
		EXPECT_EQ(std::get<CubicBezierSegmentData>(moved->segments[0].data).startHandle.offset,
			std::get<CubicBezierSegmentData>(authored.segments[0].data).startHandle.offset);
		ExpectVec3Near(moved->transform.position, snapshot.paths[0].transform.position + delta.spatial.translation);

		RestoreSceneTransformSelection(window, snapshot);
		const ScenePath *restored = window.paths->Store().Find(path.id);
		ASSERT_NE(restored, nullptr);
		EXPECT_EQ(restored->transform.position, authored.transform.position);
		EXPECT_EQ(restored->transform.rotation, authored.transform.rotation);
		EXPECT_EQ(restored->transform.scale, authored.transform.scale);
		EXPECT_EQ(restored->nodes[0].position, authored.nodes[0].position);

		SceneTransformDelta rotation;
		rotation.spatial.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
		ApplySceneTransformSelection(window, snapshot, rotation, ModalTransformOp::Rotate,
			TransformPivotMode::Median, glm::vec3(0.0f));
		const ScenePath *rotated = window.paths->Store().Find(path.id);
		ASSERT_NE(rotated, nullptr);
		EXPECT_EQ(rotated->nodes[0].position, authored.nodes[0].position);
		EXPECT_EQ(std::get<CubicBezierSegmentData>(rotated->segments[0].data).startHandle.offset,
			std::get<CubicBezierSegmentData>(authored.segments[0].data).startHandle.offset);
		const glm::quat expectedRotation = glm::normalize(rotation.spatial.rotation * snapshot.paths[0].transform.rotation);
		EXPECT_NEAR(std::abs(glm::dot(rotated->transform.rotation, expectedRotation)), 1.0f, 1.0e-5f);

		RestoreSceneTransformSelection(window, snapshot);
		SceneTransformDelta scale;
		scale.spatial.linear = glm::mat3(1.0f);
		scale.spatial.linear[0][0] = 1.25f;
		scale.spatial.linear[1][1] = 0.5f;
		scale.spatial.linear[2][2] = 2.5f;
		ApplySceneTransformSelection(window, snapshot, scale, ModalTransformOp::Scale,
			TransformPivotMode::Median, glm::vec3(0.0f));
		const ScenePath *scaled = window.paths->Store().Find(path.id);
		ASSERT_NE(scaled, nullptr);
		const glm::mat3 basis(snapshot.paths[0].transform.rotation);
		const glm::mat3 localScale = glm::transpose(basis) * scale.spatial.linear * basis;
		const glm::vec3 expectedScale = snapshot.paths[0].transform.scale * glm::vec3(
			localScale[0][0], localScale[1][1], localScale[2][2]);
		ExpectVec3Near(scaled->transform.scale, expectedScale);
		EXPECT_EQ(scaled->nodes[0].position, authored.nodes[0].position);
		EXPECT_EQ(std::get<CubicBezierSegmentData>(scaled->segments[0].data).startHandle.offset,
			std::get<CubicBezierSegmentData>(authored.segments[0].data).startHandle.offset);

		RestoreSceneTransformSelection(window, snapshot);
		const ScenePath *cancelled = window.paths->Store().Find(path.id);
		ASSERT_NE(cancelled, nullptr);
		EXPECT_EQ(cancelled->transform.position, authored.transform.position);
		EXPECT_EQ(cancelled->transform.rotation, authored.transform.rotation);
		EXPECT_EQ(cancelled->transform.scale, authored.transform.scale);
	}
}
