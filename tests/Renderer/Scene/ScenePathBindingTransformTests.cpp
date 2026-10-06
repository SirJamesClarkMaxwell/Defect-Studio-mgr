#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Presentation/Panels/ScenePathBindingOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio::Tests
{
	TEST(ScenePathBindingTransformTests, BoundNodesTransformInWorldSpaceWithoutAccumulatingAndRestore)
	{
		for (const PathBinding binding : {
			PathBinding{PathBinding::CopyPosition{0, {1.0f, 2.0f, 0.0f}}},
			PathBinding{PathBinding::BondMidpoint{0, 1, {1.0f, 2.0f, 0.0f}}}})
		{
			RendererWindowState window;
			RendererAtomData atom;
			atom.cartesianPosition = {4.0f, 5.0f, 6.0f};
			window.structure.atoms = {atom, atom};
			ScenePath path;
			path.transform.position = {10.0f, -3.0f, 2.0f};
			path.transform.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
			path.transform.scale = {2.0f, 3.0f, 4.0f};
			path.nodes = {{AllocateElementId(path), {-1.0f, 0.0f, 0.0f}, binding},
				{AllocateElementId(path), {1.0f, 0.0f, 0.0f}, {}}};
			path.segments = {{AllocateElementId(path), LineSegmentData{}}};
			const SceneObjectId id = SceneSystem::AppendScenePath(window, path);
			window.pathEdit.Enter(id);
			window.pathEdit.SetSelection({path.nodes.front().id});
			const glm::vec3 startWorld{5.0f, 7.0f, 6.0f};
			const glm::vec3 pivot{1.0f, -1.0f, 2.0f};
			const auto snapshot = CaptureSceneTransformSelection(window);
			ASSERT_EQ(snapshot.pathElements.size(), 1u);
			EXPECT_TRUE(snapshot.pathElements.front().bound);
			EXPECT_EQ(snapshot.pathElements.front().bindingOffset, glm::vec3(1.0f, 2.0f, 0.0f));

			for (const ModalTransformOp op : {ModalTransformOp::Translate, ModalTransformOp::Rotate, ModalTransformOp::Scale})
			{
				SceneTransformDelta delta;
				glm::vec3 expected;
				if (op == ModalTransformOp::Translate)
				{
					delta.spatial.translation = {2.0f, -1.0f, 3.0f};
					expected = startWorld + delta.spatial.translation;
				}
				else if (op == ModalTransformOp::Rotate)
				{
					delta.spatial.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
					expected = {-7.0f, 3.0f, 6.0f};
				}
				else
				{
					delta.spatial.linear = glm::mat3(2.0f);
					expected = {9.0f, 15.0f, 10.0f};
				}
				for (int frame = 0; frame < 2; ++frame)
				{
					ApplySceneTransformSelection(window, snapshot, delta, op, TransformPivotMode::Cursor3D, pivot);
					const ScenePath &stored = *window.paths->Store().Find(id);
					EXPECT_EQ(stored.nodes.front().position, path.nodes.front().position);
					const auto resolved = ResolveNodePositions(stored, SceneSystem::MakePathBindingContext(window));
					EXPECT_LT(glm::length(resolved.positions.front() - expected), 1e-4f);
				}
				RestoreSceneTransformSelection(window, snapshot);
				const auto resolved = ResolveNodePositions(*window.paths->Store().Find(id), SceneSystem::MakePathBindingContext(window));
				EXPECT_LT(glm::length(resolved.positions.front() - startWorld), 1e-4f);
			}

			window.paths->Store().MutateGeometry(id, [](ScenePath &stored) { stored.transform.scale.x = 0.0f; });
			EXPECT_FALSE(DetachActiveScenePathNodeKeepingPosition(window));
			EXPECT_TRUE(ResolveActiveScenePathNodeBinding(window));
			EXPECT_EQ(window.paths->Store().Find(id)->nodes.front().position, path.nodes.front().position);
		}
	}
} // namespace DefectStudio::Tests
