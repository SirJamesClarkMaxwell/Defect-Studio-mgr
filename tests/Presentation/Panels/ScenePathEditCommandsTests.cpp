#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/ScenePathEditCommands.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Three nodes on the x axis joined by two Line segments: -1, 1, 3.
		ScenePath MakeLinePath(std::uint64_t id)
		{
			ScenePath path;
			path.id = SceneObjectId{id};
			for (const float x : {-1.0f, 1.0f, 3.0f})
				path.nodes.push_back(PathNode{AllocateElementId(path), glm::vec3(x, 0.0f, 0.0f), {}});
			for (int segment = 0; segment < 2; ++segment)
				path.segments.push_back(PathSegment{AllocateElementId(path), LineSegmentData{}});
			return path;
		}

		RendererStartupConfig EmptyRendererConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}
	} // namespace

	class ScenePathEditCommandsTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undoStack);
			RegisterScenePathEditCommands(registry, renderer);
		}

		void TearDown() override
		{
			renderer.OnDetach();
		}

		// One unfocused window - the commands fall back to it, as renderer.scene_path.reverse does.
		RendererWindowState &AddWindowWithPath(bool openInEditMode)
		{
			RendererWindowState window;
			window.windowId = "path-edit-commands";
			pathId = SceneSystem::AppendScenePath(window, MakeLinePath(1));
			window.selectedScenePaths = {pathId};
			if (openInEditMode)
				window.pathEdit.Enter(pathId);
			renderer.AddWindow(std::move(window));
			return renderer.GetWindows().front();
		}

		[[nodiscard]] const ScenePath &StoredPath(const RendererWindowState &window) const
		{
			const ScenePath *path = window.paths->Store().Find(pathId);
			EXPECT_NE(path, nullptr);
			return *path;
		}

		Result<CommandOutcome> Run(const char *id)
		{
			return registry.Execute(CommandID{id});
		}

		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{EmptyRendererConfig()};
		CommandRegistry registry;
		SceneObjectId pathId;
	};

	TEST_F(ScenePathEditCommandsTests, RegistersEveryPathEditCommand)
	{
		for (const char *id : {
				 "renderer.path_edit.toggle", "renderer.path_edit.leave", "renderer.path_edit.mode_nodes",
				 "renderer.path_edit.mode_segments", "renderer.path_edit.mode_whole", "renderer.path_edit.extend",
				 "renderer.path_edit.insert", "renderer.path_edit.delete_nodes", "renderer.path_edit.reverse",
				 "renderer.path_edit.handle_type_menu", "renderer.path_edit.handle_free",
				 "renderer.path_edit.handle_aligned", "renderer.path_edit.handle_vector",
				 "renderer.path_edit.handle_auto"})
			EXPECT_TRUE(registry.HasCommand(CommandID{id})) << id;
	}

	TEST_F(ScenePathEditCommandsTests, ToggleEntersOnTheSingleSelectedPathAndLeavesAgain)
	{
		RendererWindowState &window = AddWindowWithPath(false);

		ASSERT_TRUE(Run("renderer.path_edit.toggle"));
		EXPECT_TRUE(window.pathEdit.IsActive());
		EXPECT_EQ(window.pathEdit.Path(), pathId);

		ASSERT_TRUE(Run("renderer.path_edit.toggle"));
		EXPECT_FALSE(window.pathEdit.IsActive());
		EXPECT_EQ(window.selectedScenePaths, std::vector<SceneObjectId>({pathId}));
	}

	TEST_F(ScenePathEditCommandsTests, ToggleWithoutExactlyOneSelectedPathIsANoOp)
	{
		RendererWindowState &window = AddWindowWithPath(false);
		window.selectedScenePaths.clear();

		ASSERT_TRUE(Run("renderer.path_edit.toggle"));
		EXPECT_FALSE(window.pathEdit.IsActive());
	}

	TEST_F(ScenePathEditCommandsTests, LeaveClosesTheSession)
	{
		RendererWindowState &window = AddWindowWithPath(true);

		ASSERT_TRUE(Run("renderer.path_edit.leave"));
		EXPECT_FALSE(window.pathEdit.IsActive());
	}

	TEST_F(ScenePathEditCommandsTests, ModeCommandsSetTheElementMode)
	{
		RendererWindowState &window = AddWindowWithPath(true);

		ASSERT_TRUE(Run("renderer.path_edit.mode_segments"));
		EXPECT_EQ(window.pathEdit.ElementMode(), PathElementMode::Segment);
		ASSERT_TRUE(Run("renderer.path_edit.mode_whole"));
		EXPECT_EQ(window.pathEdit.ElementMode(), PathElementMode::WholePath);
		ASSERT_TRUE(Run("renderer.path_edit.mode_nodes"));
		EXPECT_EQ(window.pathEdit.ElementMode(), PathElementMode::NodeHandle);
	}

	TEST_F(ScenePathEditCommandsTests, ExtendAddsANodeAtTheSelectedEndpointWithOneUndo)
	{
		RendererWindowState &window = AddWindowWithPath(true);
		window.pathEdit.SetSelection({StoredPath(window).nodes.back().id});

		ASSERT_TRUE(Run("renderer.path_edit.extend"));

		EXPECT_EQ(StoredPath(window).nodes.size(), 4u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}

	TEST_F(ScenePathEditCommandsTests, ExtendWithoutAnEndpointSelectedReturnsThePathError)
	{
		RendererWindowState &window = AddWindowWithPath(true);
		window.pathEdit.ClearSelection();

		const Result<CommandOutcome> result = Run("renderer.path_edit.extend");

		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "path.edit_endpoint_required");
		EXPECT_EQ(StoredPath(window).nodes.size(), 3u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathEditCommandsTests, InsertSplitsTheSelectedSegment)
	{
		RendererWindowState &window = AddWindowWithPath(true);
		window.pathEdit.SetElementMode(PathElementMode::Segment);
		window.pathEdit.SetSelection({StoredPath(window).segments.front().id});

		ASSERT_TRUE(Run("renderer.path_edit.insert"));

		EXPECT_EQ(StoredPath(window).nodes.size(), 4u);
		EXPECT_EQ(StoredPath(window).nodes[1].position, glm::vec3(0.0f));
	}

	TEST_F(ScenePathEditCommandsTests, DeleteNodesRemovesTheSelectedNodeNotThePathObject)
	{
		RendererWindowState &window = AddWindowWithPath(true);
		window.pathEdit.SetSelection({StoredPath(window).nodes[1].id});

		ASSERT_TRUE(Run("renderer.path_edit.delete_nodes"));

		ASSERT_NE(window.paths->Store().Find(pathId), nullptr);
		EXPECT_EQ(StoredPath(window).nodes.size(), 2u);
		EXPECT_EQ(window.selectedScenePaths, std::vector<SceneObjectId>({pathId}));
	}

	TEST_F(ScenePathEditCommandsTests, ReverseSwapsTheNodeOrder)
	{
		RendererWindowState &window = AddWindowWithPath(true);

		ASSERT_TRUE(Run("renderer.path_edit.reverse"));

		EXPECT_EQ(StoredPath(window).nodes.front().position, glm::vec3(3.0f, 0.0f, 0.0f));
		EXPECT_EQ(StoredPath(window).nodes.back().position, glm::vec3(-1.0f, 0.0f, 0.0f));
	}

	TEST_F(ScenePathEditCommandsTests, HandleTypeMenuOnlyRequestsThePopup)
	{
		RendererWindowState &window = AddWindowWithPath(true);
		const ScenePath before = StoredPath(window);

		ASSERT_TRUE(Run("renderer.path_edit.handle_type_menu"));

		EXPECT_TRUE(window.pathHandleTypeMenuRequested);
		EXPECT_EQ(StoredPath(window).nodes.size(), before.nodes.size());
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathEditCommandsTests, EditCommandsOutsideEditModeChangeNothing)
	{
		RendererWindowState &window = AddWindowWithPath(false);

		for (const char *id : {
				 "renderer.path_edit.leave", "renderer.path_edit.mode_segments", "renderer.path_edit.extend",
				 "renderer.path_edit.insert", "renderer.path_edit.delete_nodes", "renderer.path_edit.reverse",
				 "renderer.path_edit.handle_type_menu", "renderer.path_edit.handle_auto"})
			EXPECT_TRUE(Run(id)) << id;

		EXPECT_FALSE(window.pathEdit.IsActive());
		EXPECT_FALSE(window.pathHandleTypeMenuRequested);
		EXPECT_EQ(StoredPath(window).nodes.size(), 3u);
		EXPECT_EQ(StoredPath(window).nodes.front().position, glm::vec3(-1.0f, 0.0f, 0.0f));
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathEditCommandsTests, ContextFollowsTheEditSessionOfTheOnlyWindow)
	{
		RendererWindowState &window = AddWindowWithPath(false);
		ContextManager contexts;

		UpdateScenePathEditContext(renderer, contexts);
		EXPECT_FALSE(contexts.IsActive(kPathEditActiveContext));

		window.pathEdit.Enter(pathId);
		UpdateScenePathEditContext(renderer, contexts);
		EXPECT_TRUE(contexts.IsActive(kPathEditActiveContext));

		window.pathEdit.Leave();
		UpdateScenePathEditContext(renderer, contexts);
		EXPECT_FALSE(contexts.IsActive(kPathEditActiveContext));
	}
} // namespace DefectStudio::Tests
