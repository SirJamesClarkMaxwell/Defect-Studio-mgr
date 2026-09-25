#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererWindowState MakeWindowWithLabel(const std::string &text)
		{
			RendererWindowState window;
			window.windowId = "w1";
			RendererWindowState::FreeLabel label;
			label.text = text;
			window.freeLabels.push_back(label);
			return window;
		}

		[[nodiscard]] SceneObjectsWindowResolver ResolverFor(RendererWindowState &window)
		{
			return [&window](const std::string &id) -> RendererWindowState * {
				return id == window.windowId ? &window : nullptr;
			};
		}
	} // namespace

	TEST(SceneObjectsSnapshotCommandTests, ExecuteIsNoOpAndCommandIsUndoable)
	{
		RendererWindowState window = MakeWindowWithLabel("before");
		Unique<ICommand> command = CreateSceneObjectsSnapshotCommand(ResolverFor(window), "w1", CaptureSceneObjectsSnapshot(window));
		window.freeLabels[0].text = "after";

		CommandContext context;
		EXPECT_TRUE(command->Execute(context).HasValue());
		EXPECT_EQ(window.freeLabels[0].text, "after");
		EXPECT_TRUE(command->IsUndoable());
	}

	TEST(SceneObjectsSnapshotCommandTests, UndoRestoresBeforeAndRedoRestoresAfter)
	{
		RendererWindowState window = MakeWindowWithLabel("before");
		RendererWindowState::ScenePlane plane;
		plane.id = SceneObjectId{7};
		window.scenePlanes.push_back(plane);
		int restoredCount = 0;
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(
			ResolverFor(window), "w1", CaptureSceneObjectsSnapshot(window),
			[&restoredCount](RendererWindowState &) { ++restoredCount; })));

		window.freeLabels[0].text = "after";
		window.sceneArrows.push_back({});
		window.scenePlanes[0].alpha = 0.8f;
		window.selectedFreeLabels.push_back(window.freeLabels[0].id);
		window.selectedScenePlanes.push_back(plane.id);
		window.freeLabelDragging = true;

		ASSERT_TRUE(stack.Undo().HasValue());
		ASSERT_EQ(window.freeLabels.size(), 1u);
		EXPECT_EQ(window.freeLabels[0].text, "before");
		EXPECT_TRUE(window.sceneArrows.empty());
		ASSERT_EQ(window.scenePlanes.size(), 1u);
		EXPECT_FLOAT_EQ(window.scenePlanes[0].alpha, 0.35f);
		EXPECT_TRUE(window.selectedFreeLabels.empty());
		EXPECT_TRUE(window.selectedScenePlanes.empty());
		EXPECT_FALSE(window.freeLabelDragging);

		ASSERT_TRUE(stack.Redo().HasValue());
		EXPECT_EQ(window.freeLabels[0].text, "after");
		EXPECT_EQ(window.sceneArrows.size(), 1u);
		ASSERT_EQ(window.scenePlanes.size(), 1u);
		EXPECT_FLOAT_EQ(window.scenePlanes[0].alpha, 0.8f);
		EXPECT_EQ(restoredCount, 2);
	}

	TEST(SceneObjectsSnapshotCommandTests, ConsecutiveEditsUndoAndRedoInOrder)
	{
		RendererWindowState window = MakeWindowWithLabel("before");
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(ResolverFor(window), "w1", CaptureSceneObjectsSnapshot(window))));
		window.freeLabels[0].text = "one";
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(ResolverFor(window), "w1", CaptureSceneObjectsSnapshot(window))));
		window.freeLabels[0].text = "two";

		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_EQ(window.freeLabels[0].text, "one");
		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_EQ(window.freeLabels[0].text, "before");
		ASSERT_TRUE(stack.Redo().HasValue());
		EXPECT_EQ(window.freeLabels[0].text, "one");
		ASSERT_TRUE(stack.Redo().HasValue());
		EXPECT_EQ(window.freeLabels[0].text, "two");
	}

	TEST(SceneObjectsSnapshotCommandTests, MissingWindowFailsWithoutMovingTheStack)
	{
		RendererWindowState window = MakeWindowWithLabel("before");
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(
			[](const std::string &) -> RendererWindowState * { return nullptr; }, "w1", CaptureSceneObjectsSnapshot(window))));

		const Result<void> result = stack.Undo();
		ASSERT_FALSE(result.HasValue());
		EXPECT_EQ(result.Error().code, "scene_objects.undo_target_unavailable");
		EXPECT_TRUE(stack.CanUndo());
		EXPECT_EQ(stack.GetUndoDepth(), 1u);
	}

	TEST(SceneObjectsSnapshotCommandTests, RestoreReturnsCapturedPathsAndClearsTheirCache)
	{
		RendererWindowState window = MakeWindowWithLabel("before");
		ScenePath path;
		path.id = SceneObjectId{1};
		path.name = "captured";
		ASSERT_TRUE(SceneSystem::EnsurePathSystem(window).Store().Insert(path));
		ScenePath unchanged;
		unchanged.id = SceneObjectId{2};
		unchanged.name = "unchanged";
		ASSERT_TRUE(window.paths->Store().Insert(unchanged));
		const SceneObjectsSnapshot snapshot = CaptureSceneObjectsSnapshot(window);
		ASSERT_TRUE(window.paths->Store().MutateGeometry(path.id, [](ScenePath &mutated) { mutated.name = "mutated"; }));
		ScenePath added;
		added.id = SceneObjectId{3};
		ASSERT_TRUE(window.paths->Store().Insert(added));
		window.paths->Caches().Store(path.id, {{1, 1}, 1, 1}, {});
		RestoreSceneObjectsSnapshot(window, snapshot);
		ASSERT_NE(window.paths, nullptr);
		ASSERT_EQ(window.paths->Store().Size(), 2u);
		EXPECT_EQ(window.paths->Store().Find(path.id)->name, "captured");
		EXPECT_EQ(window.paths->Store().Find(unchanged.id)->name, "unchanged");
		EXPECT_EQ(window.paths->Store().Find(added.id), nullptr);
		EXPECT_EQ(window.paths->Caches().Size(), 0u);
	}

	TEST(SceneObjectsSnapshotCommandTests, RestoreFromNullPathsCreatesAnEmptyStore)
	{
		RendererWindowState window = MakeWindowWithLabel("before");
		ASSERT_EQ(window.paths, nullptr);
		const SceneObjectsSnapshot snapshot = CaptureSceneObjectsSnapshot(window);
		RestoreSceneObjectsSnapshot(window, snapshot);
		ASSERT_NE(window.paths, nullptr);
		EXPECT_TRUE(window.paths->Store().Empty());
	}
} // namespace DefectStudio::Tests
