#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Commands/SceneVisibilitySnapshotCommand.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/ViewModifier.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererWindowState MakeWindow(bool firstAtomVisible = true)
		{
			RendererWindowState window;
			window.windowId = "w1";
			window.structure.atoms = {
				RendererAtomData{"C", glm::vec3(0.0f), glm::vec3(1.0f), 0.4f, firstAtomVisible},
				RendererAtomData{"O", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0.4f, true}};
			RendererBondData bond;
			bond.firstAtomIndex = 0;
			bond.secondAtomIndex = 1;
			bond.visible = firstAtomVisible;
			window.structure.bonds.push_back(bond);
			SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
			window.sceneRegistry.AtomEntityAt(0).GetComponent<SelectionComponent>().selected = true;
			window.sceneRegistry.BondEntityAt(0).GetComponent<SelectionComponent>().selected = true;
			SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
			return window;
		}

		[[nodiscard]] RendererStartupConfig EmptyRendererConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}
	} // namespace

	class SceneVisibilitySnapshotCommandTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undoStack);
		}

		void TearDown() override
		{
			renderer.OnDetach();
		}

		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{EmptyRendererConfig()};
	};

	TEST_F(SceneVisibilitySnapshotCommandTests, HideUndoShowsAtomsAndRedoHidesThemAgain)
	{
		renderer.AddWindow(MakeWindow());
		RendererWindowState &window = renderer.GetWindows().front();

		const HiddenSceneState before = CaptureHiddenSceneState(window.structure);
		HideSelectionModifier{}.Apply(window.sceneRegistry, window);
		PushSceneVisibilityUndoSnapshot(window, before, "Hide selection");

		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);
		EXPECT_FALSE(window.structure.atoms[0].visible);
		EXPECT_FALSE(window.structure.bonds[0].visible);
		ASSERT_TRUE(undoStack->Undo().HasValue());
		EXPECT_TRUE(window.structure.atoms[0].visible);
		EXPECT_TRUE(window.structure.bonds[0].visible);
		EXPECT_EQ(window.selectedAtomIndices, (std::vector<std::size_t>{0}));
		EXPECT_EQ(window.selectedBondIndices, (std::vector<std::size_t>{0}));

		ASSERT_TRUE(undoStack->Redo().HasValue());
		EXPECT_FALSE(window.structure.atoms[0].visible);
		EXPECT_FALSE(window.structure.bonds[0].visible);
		EXPECT_EQ(window.selectedAtomIndices, (std::vector<std::size_t>{0}));
		EXPECT_EQ(window.selectedBondIndices, (std::vector<std::size_t>{0}));
	}

	TEST_F(SceneVisibilitySnapshotCommandTests, ShowAllUndoRestoresHiddenSetAndRedoShowsEverything)
	{
		renderer.AddWindow(MakeWindow(false));
		RendererWindowState &window = renderer.GetWindows().front();

		const HiddenSceneState before = CaptureHiddenSceneState(window.structure);
		ShowAllModifier{}.Apply(window.sceneRegistry, window);
		PushSceneVisibilityUndoSnapshot(window, before, "Show all");

		EXPECT_TRUE(window.structure.atoms[0].visible);
		EXPECT_TRUE(window.structure.bonds[0].visible);
		ASSERT_TRUE(undoStack->Undo().HasValue());
		EXPECT_FALSE(window.structure.atoms[0].visible);
		EXPECT_FALSE(window.structure.bonds[0].visible);
		EXPECT_EQ(window.selectedAtomIndices, (std::vector<std::size_t>{0}));
		EXPECT_EQ(window.selectedBondIndices, (std::vector<std::size_t>{0}));

		ASSERT_TRUE(undoStack->Redo().HasValue());
		EXPECT_TRUE(window.structure.atoms[0].visible);
		EXPECT_TRUE(window.structure.bonds[0].visible);
		EXPECT_EQ(window.selectedAtomIndices, (std::vector<std::size_t>{0}));
		EXPECT_EQ(window.selectedBondIndices, (std::vector<std::size_t>{0}));
	}

	TEST_F(SceneVisibilitySnapshotCommandTests, NoOpHideDoesNotPushUndoRecord)
	{
		renderer.AddWindow(MakeWindow(false));
		RendererWindowState &window = renderer.GetWindows().front();

		const HiddenSceneState before = CaptureHiddenSceneState(window.structure);
		HideSelectionModifier{}.Apply(window.sceneRegistry, window);
		PushSceneVisibilityUndoSnapshot(window, before, "Hide selection");

		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
		EXPECT_FALSE(undoStack->CanUndo());
	}

	TEST_F(SceneVisibilitySnapshotCommandTests, MissingWindowFailsWithoutMovingTheStack)
	{
		RendererWindowState window = MakeWindow();
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(CreateSceneVisibilitySnapshotCommand(
			[](const std::string &) -> std::optional<std::reference_wrapper<RendererWindowState>> {
				return std::nullopt;
			},
			"w1",
			CaptureHiddenSceneState(window.structure))));

		const Result<void> result = stack.Undo();
		ASSERT_FALSE(result.HasValue());
		EXPECT_EQ(result.Error().code, "scene_visibility.undo_target_unavailable");
		EXPECT_EQ(stack.GetUndoDepth(), 1u);
	}

	TEST_F(SceneVisibilitySnapshotCommandTests, ViewUndoRestoresOnlyCameraAfterGlobalVisibilityChange)
	{
		renderer.AddWindow(MakeWindow());
		RendererWindowState &window = renderer.GetWindows().front();
		window.camera = CreateUnique<RendererViewCamera>();
		const float originalRoll = window.camera->Roll();

		renderer.BeginViewInteraction(window.windowId, "keyboard.roll_step");
		window.camera->Roll(0.25f);
		renderer.CommitViewInteraction(window.windowId);
		ASSERT_EQ(window.viewUndoHistory.size(), 1u);

		const HiddenSceneState before = CaptureHiddenSceneState(window.structure);
		HideSelectionModifier{}.Apply(window.sceneRegistry, window);
		PushSceneVisibilityUndoSnapshot(window, before, "Hide selection");
		ASSERT_FALSE(window.structure.atoms[0].visible);

		window.sceneRegistry.AtomEntityAt(0).GetComponent<SelectionComponent>().selected = false;
		window.sceneRegistry.AtomEntityAt(1).GetComponent<SelectionComponent>().selected = true;
		SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);

		renderer.UndoViewChange(window.windowId);
		renderer.UpdateCameraTransitions(1.0f);

		EXPECT_FALSE(window.structure.atoms[0].visible);
		EXPECT_EQ(window.selectedAtomIndices, (std::vector<std::size_t>{1}));
		EXPECT_NEAR(window.camera->Roll(), originalRoll, 0.0001f);
	}
} // namespace DefectStudio::Tests
