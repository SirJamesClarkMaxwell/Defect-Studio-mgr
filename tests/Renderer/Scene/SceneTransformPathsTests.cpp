#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/Scene/SceneTransform.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneTransformPathsTests, CapturesAndMovesEveryNodeAndCubicHandle)
	{
		RendererWindowState window;
		ScenePath path;
		path.id = window.sceneRegistry.AllocateObjectId();
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
		cubic.startHandle.position = glm::vec3(0.5f, 1.0f, 0.0f);
		cubic.endHandle.id = AllocateElementId(path);
		cubic.endHandle.position = glm::vec3(1.5f, 1.0f, 0.0f);
		segment.data = cubic;
		path.segments = {segment};
		SceneSystem::EnsurePathSystem(window).Store().Insert(path);
		window.selectedScenePaths = {path.id};

		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		ASSERT_EQ(snapshot.paths.size(), 1u);
		EXPECT_EQ(snapshot.paths[0].nodes.size(), 2u);
		EXPECT_EQ(snapshot.paths[0].handles.size(), 2u);
		EXPECT_TRUE(HasSceneObjectTransformTargets(snapshot));

		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(1.0f, 2.0f, 0.0f);
		ApplySceneTransformSelection(window, snapshot, delta, ModalTransformOp::Translate,
			TransformPivotMode::Median, glm::vec3(0.0f));
		const ScenePath *moved = window.paths->Store().Find(path.id);
		ASSERT_NE(moved, nullptr);
		EXPECT_EQ(moved->nodes[0].position, glm::vec3(1.0f, 2.0f, 0.0f));
		EXPECT_EQ(std::get<CubicBezierSegmentData>(moved->segments[0].data).startHandle.position,
			glm::vec3(1.5f, 3.0f, 0.0f));

		RestoreSceneTransformSelection(window, snapshot);
		SceneTransformDelta rotation;
		rotation.spatial.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
		ApplySceneTransformSelection(window, snapshot, rotation, ModalTransformOp::Rotate,
			TransformPivotMode::Median, glm::vec3(0.0f));
		const ScenePath *rotated = window.paths->Store().Find(path.id);
		ASSERT_NE(rotated, nullptr);
		EXPECT_FLOAT_EQ(glm::length(rotated->nodes[0].position), glm::length(snapshot.paths[0].nodePositions[0]));

		RestoreSceneTransformSelection(window, snapshot);
		EXPECT_EQ(window.paths->Store().Find(path.id)->nodes[0].position, glm::vec3(0.0f));
	}
}
