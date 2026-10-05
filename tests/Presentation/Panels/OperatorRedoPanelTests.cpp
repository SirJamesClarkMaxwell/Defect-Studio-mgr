#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Operators/SceneOperatorRegistry.hpp"
#include "Presentation/Panels/OperatorRedoPanel.hpp"
#include "Presentation/Panels/ScenePathCurvedArrow.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		std::size_t PathCount(const RendererWindowState &window)
		{
			return window.paths ? window.paths->Store().Ids().size() : 0u;
		}
	}

	class OperatorRedoPanelTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undoStack);
			ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
			RendererWindowState window;
			window.windowId = "redo-panel";
			window.structure.atoms = {{"C", {-1, 0, 0}}, {"C", {1, 0, 0}}};
			window.selectedAtomIndices = {0, 1};
			renderer.AddWindow(std::move(window));
		}
		void TearDown() override { renderer.OnDetach(); }

		[[nodiscard]] RendererWindowState &Window() { return renderer.GetWindows().front(); }
		[[nodiscard]] const SceneOperator &CurvedArrow()
		{
			const auto *op = registry.Find("scene.curved_arrow");
			EXPECT_NE(op, nullptr);
			return *op;
		}

		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{[] { RendererStartupConfig config; config.loadDefaultScene = false; return config; }()};
		SceneOperatorRegistry registry;
		OperatorRedoPanel panel;
	};

	TEST_F(OperatorRedoPanelTests, FiveReapplicationsLeaveOneUndoEntry)
	{
		ASSERT_TRUE(panel.RunAndOpen(CurvedArrow(), Window()));
		EXPECT_TRUE(panel.IsOpen());
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);

		auto values = panel.Values();
		for (const float sweep : {90.0f, 120.0f, 180.0f, 240.0f, 300.0f})
		{
			values["sweepDegrees"] = sweep;
			ASSERT_TRUE(panel.Reapply(Window(), values));
		}
		// One entry for the whole session - this is the point of the panel.
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
		EXPECT_EQ(PathCount(Window()), 1u);
	}

	TEST_F(OperatorRedoPanelTests, UndoAfterReapplyRestoresThePreOperationScene)
	{
		ASSERT_TRUE(panel.RunAndOpen(CurvedArrow(), Window()));
		auto values = panel.Values();
		values["sweepDegrees"] = 120.0f;
		ASSERT_TRUE(panel.Reapply(Window(), values));
		ASSERT_EQ(PathCount(Window()), 1u);

		ASSERT_TRUE(undoStack->Undo());
		// Not an intermediate parameter value - the scene as it was before the operator ran.
		EXPECT_EQ(PathCount(Window()), 0u);
		ASSERT_TRUE(undoStack->Redo());
		EXPECT_EQ(PathCount(Window()), 1u);
	}

	TEST_F(OperatorRedoPanelTests, AnUnrelatedUndoEntryClosesThePanel)
	{
		ASSERT_TRUE(panel.RunAndOpen(CurvedArrow(), Window()));
		const auto created = PathCount(Window());
		ASSERT_EQ(created, 1u);

		// Any other undoable scene edit while the panel is open. The first run consumed the atom
		// selection, so re-select before adding a second arrow.
		Window().selectedAtomIndices = {0, 1};
		ASSERT_TRUE(AddCurvedArrowThroughSelectedAtoms(Window(), {}, SceneOperationUndo::Push));
		ASSERT_EQ(undoStack->GetUndoDepth(), 2u);

		panel.PollInvalidation(*undoStack, &Window());
		EXPECT_FALSE(panel.IsOpen());
		// The foreign edit must survive: a panel that restored its snapshot here would eat it.
		EXPECT_EQ(PathCount(Window()), 2u);
	}

	TEST_F(OperatorRedoPanelTests, AClosedWindowClosesThePanel)
	{
		ASSERT_TRUE(panel.RunAndOpen(CurvedArrow(), Window()));
		panel.PollInvalidation(*undoStack, nullptr);
		EXPECT_FALSE(panel.IsOpen());
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}

	TEST_F(OperatorRedoPanelTests, AFailingReapplyClosesThePanelAndKeepsTheObjects)
	{
		ASSERT_TRUE(panel.RunAndOpen(CurvedArrow(), Window()));
		ASSERT_EQ(PathCount(Window()), 1u);

		// The scene moved under the panel: the two atoms now sit on top of each other, so the bond
		// the ring turns about no longer exists. Clearing the selection would NOT do it - the panel
		// restores the selection it ran on, which is what keeps it usable after a click elsewhere.
		Window().structure.atoms[1].cartesianPosition = Window().structure.atoms[0].cartesianPosition;
		auto values = panel.Values();
		values["sweepDegrees"] = 120.0f;
		EXPECT_FALSE(panel.Reapply(Window(), values));
		EXPECT_FALSE(panel.IsOpen());
		// The last successful result stays on screen rather than vanishing under the user.
		EXPECT_EQ(PathCount(Window()), 1u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}

	TEST_F(OperatorRedoPanelTests, AFailingFirstRunPushesNothingAndStaysClosed)
	{
		Window().selectedAtomIndices.clear();
		EXPECT_FALSE(panel.RunAndOpen(CurvedArrow(), Window()));
		EXPECT_FALSE(panel.IsOpen());
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
		EXPECT_EQ(PathCount(Window()), 0u);
	}

	TEST_F(OperatorRedoPanelTests, ValuesStartAtTheOperatorDefaults)
	{
		ASSERT_TRUE(panel.RunAndOpen(CurvedArrow(), Window()));
		ASSERT_NE(panel.CurrentOperator(), nullptr);
		EXPECT_EQ(panel.CurrentOperator()->id, "scene.curved_arrow");
		EXPECT_EQ(panel.Values(), CurvedArrow().defaults);
	}

	TEST_F(OperatorRedoPanelTests, ReapplyWithoutAnOpenPanelIsRejected)
	{
		// The ImGui body can outlive a close by a frame; a stale change must not restore a snapshot.
		EXPECT_FALSE(panel.Reapply(Window(), SceneOperatorValues{}));
		EXPECT_EQ(PathCount(Window()), 0u);
	}
}
