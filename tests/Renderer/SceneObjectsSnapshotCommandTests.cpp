#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"

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
		int restoredCount = 0;
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(CreateSceneObjectsSnapshotCommand(
			ResolverFor(window), "w1", CaptureSceneObjectsSnapshot(window),
			[&restoredCount](RendererWindowState &) { ++restoredCount; })));

		window.freeLabels[0].text = "after";
		window.sceneArrows.push_back({});
		window.selectedFreeLabels.push_back(window.freeLabels[0].id);
		window.freeLabelDragging = true;

		ASSERT_TRUE(stack.Undo().HasValue());
		ASSERT_EQ(window.freeLabels.size(), 1u);
		EXPECT_EQ(window.freeLabels[0].text, "before");
		EXPECT_TRUE(window.sceneArrows.empty());
		EXPECT_TRUE(window.selectedFreeLabels.empty());
		EXPECT_FALSE(window.freeLabelDragging);

		ASSERT_TRUE(stack.Redo().HasValue());
		EXPECT_EQ(window.freeLabels[0].text, "after");
		EXPECT_EQ(window.sceneArrows.size(), 1u);
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
} // namespace DefectStudio::Tests
