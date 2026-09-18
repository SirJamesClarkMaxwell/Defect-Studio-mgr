#include <gtest/gtest.h>

#include <utility>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererStartupConfig EmptyRendererConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}

		[[nodiscard]] RendererWindowState MakeSelectedArrowWindow()
		{
			RendererWindowState window;
			window.windowId = "scene-arrows";

			RendererWindowState::SceneArrow first;
			first.id = window.sceneRegistry.AllocateObjectId();
			first.start = glm::vec3(1.0f, 2.0f, 3.0f);
			first.end = glm::vec3(4.0f, 5.0f, 6.0f);
			first.startAnchorAtom = 2;
			first.endAnchorAtom = 5;

			RendererWindowState::SceneArrow second;
			second.id = window.sceneRegistry.AllocateObjectId();
			second.start = glm::vec3(-1.0f, -2.0f, -3.0f);
			second.end = glm::vec3(-4.0f, -5.0f, -6.0f);
			second.startAnchorAtom = 7;
			second.endAnchorAtom = 11;

			window.sceneArrows = {first, second};
			window.selectedSceneArrows = {first.id, second.id};
			return window;
		}
	} // namespace

	class SceneArrowReverseCommandTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undoStack);
			RegisterViewportSceneObjectCommands(registry, renderer);
		}

		void TearDown() override
		{
			renderer.OnDetach();
		}

		[[nodiscard]] Result<CommandOutcome> ExecuteReverse()
		{
			return registry.Execute(CommandID{"renderer.scene_arrow.reverse"});
		}

		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{EmptyRendererConfig()};
		CommandRegistry registry;
	};

	TEST_F(SceneArrowReverseCommandTests, ReversesEverySelectedArrowAndOneUndoRestoresTheBatch)
	{
		renderer.AddWindow(MakeSelectedArrowWindow());
		RendererWindowState &window = renderer.GetWindows().front();
		const std::vector<RendererWindowState::SceneArrow> before = window.sceneArrows;

		ASSERT_TRUE(ExecuteReverse());

		ASSERT_EQ(window.sceneArrows.size(), 2u);
		EXPECT_EQ(window.sceneArrows[0].start, before[0].end);
		EXPECT_EQ(window.sceneArrows[0].end, before[0].start);
		EXPECT_EQ(window.sceneArrows[0].startAnchorAtom, before[0].endAnchorAtom);
		EXPECT_EQ(window.sceneArrows[0].endAnchorAtom, before[0].startAnchorAtom);
		EXPECT_EQ(window.sceneArrows[1].start, before[1].end);
		EXPECT_EQ(window.sceneArrows[1].end, before[1].start);
		EXPECT_EQ(window.sceneArrows[1].startAnchorAtom, before[1].endAnchorAtom);
		EXPECT_EQ(window.sceneArrows[1].endAnchorAtom, before[1].startAnchorAtom);
		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);

		ASSERT_TRUE(undoStack->Undo());
		ASSERT_EQ(window.sceneArrows.size(), before.size());
		for (std::size_t index = 0; index < before.size(); ++index)
		{
			EXPECT_EQ(window.sceneArrows[index].start, before[index].start);
			EXPECT_EQ(window.sceneArrows[index].end, before[index].end);
			EXPECT_EQ(window.sceneArrows[index].startAnchorAtom, before[index].startAnchorAtom);
			EXPECT_EQ(window.sceneArrows[index].endAnchorAtom, before[index].endAnchorAtom);
		}
	}

	TEST_F(SceneArrowReverseCommandTests, EmptyArrowSelectionDoesNothingAndPushesNoUndo)
	{
		RendererWindowState windowState = MakeSelectedArrowWindow();
		windowState.selectedSceneArrows.clear();
		renderer.AddWindow(std::move(windowState));
		RendererWindowState &window = renderer.GetWindows().front();
		const std::vector<RendererWindowState::SceneArrow> before = window.sceneArrows;

		ASSERT_TRUE(ExecuteReverse());

		ASSERT_EQ(window.sceneArrows.size(), before.size());
		for (std::size_t index = 0; index < before.size(); ++index)
		{
			EXPECT_EQ(window.sceneArrows[index].start, before[index].start);
			EXPECT_EQ(window.sceneArrows[index].end, before[index].end);
			EXPECT_EQ(window.sceneArrows[index].startAnchorAtom, before[index].startAnchorAtom);
			EXPECT_EQ(window.sceneArrows[index].endAnchorAtom, before[index].endAnchorAtom);
		}
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
		EXPECT_FALSE(undoStack->CanUndo());
	}

	TEST(SceneArrowAtomMatchTests, PositionUsesTheTwoAtomPositionsInSelectionOrder)
	{
		RendererWindowState::SceneArrow arrow;
		const RendererAtomData first{"C", glm::vec3(1.0f, 2.0f, 3.0f)};
		const RendererAtomData second{"O", glm::vec3(-4.0f, 5.0f, 6.0f)};

		MatchSceneArrowPositionToAtoms(arrow, first, second);

		EXPECT_EQ(arrow.start, first.cartesianPosition);
		EXPECT_EQ(arrow.end, second.cartesianPosition);
	}

	TEST(SceneArrowAtomMatchTests, TheBufferLeavesAGapOfOneRadiusAtEachEnd)
	{
		RendererWindowState::SceneArrow unbuffered;
		RendererWindowState::SceneArrow buffered;
		const RendererAtomData first{"C", glm::vec3(0.0f), glm::vec3(0.0f), 0.5f};
		const RendererAtomData second{"O", glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0.25f};

		MatchSceneArrowPositionToAtoms(unbuffered, first, second, 0.0f);
		MatchSceneArrowPositionToAtoms(buffered, first, second, 1.0f);

		EXPECT_GT(glm::distance(buffered.start, first.cartesianPosition),
			glm::distance(unbuffered.start, first.cartesianPosition));
		EXPECT_GT(glm::distance(buffered.end, second.cartesianPosition),
			glm::distance(unbuffered.end, second.cartesianPosition));
		EXPECT_LT(buffered.start.x, buffered.end.x);
	}

	TEST(SceneArrowAtomMatchTests, DefaultBufferClearsTheDrawnAtomSpheres)
	{
		RendererWindowState::SceneArrow arrow;
		const RendererAtomData first{"C", glm::vec3(0.0f), glm::vec3(0.0f), 0.5f};
		const RendererAtomData second{"O", glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0.25f};

		MatchSceneArrowPositionToAtoms(arrow, first, second, GetSceneArrowAtomBuffer());

		EXPECT_NEAR(arrow.start.x, 0.575f, 1e-5f);
		EXPECT_NEAR(arrow.end.x, 9.7125f, 1e-5f);
		EXPECT_GT(glm::length(arrow.start - first.cartesianPosition), first.radius);
		EXPECT_GT(glm::length(arrow.end - second.cartesianPosition), second.radius);
	}

	TEST(SceneArrowAtomMatchTests, ABufferBiggerThanTheGapDoesNotInvertTheArrow)
	{
		RendererWindowState::SceneArrow arrow;
		// Overlapping spheres: 2.0 + 2.0 of trim over a 1.0 separation would put start past end.
		const RendererAtomData first{"C", glm::vec3(0.0f), glm::vec3(0.0f), 2.0f};
		const RendererAtomData second{"C", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f), 2.0f};

		MatchSceneArrowPositionToAtoms(arrow, first, second, 1.0f);

		EXPECT_LT(arrow.start.x, arrow.end.x);
		EXPECT_NEAR(arrow.end.x - arrow.start.x, 0.1f, 1e-5f);
	}

	TEST(SceneArrowAtomMatchTests, OneAtomColorCreatesAFlatElementColor)
	{
		RendererWindowState::SceneArrow arrow;
		arrow.style.useGradient = true;
		const RendererAtomData atom{"N", glm::vec3(0.0f), glm::vec3(0.15f, 0.25f, 0.95f)};

		MatchSceneArrowColorToAtom(arrow, atom);

		EXPECT_FALSE(arrow.style.useGradient);
		EXPECT_EQ(arrow.style.color, atom.color);
	}

	TEST(SceneArrowAtomMatchTests, TwoAtomColorsCreateAnOrderedGradient)
	{
		RendererWindowState::SceneArrow arrow;
		const RendererAtomData first{"C", glm::vec3(0.0f), glm::vec3(0.2f, 0.2f, 0.2f)};
		const RendererAtomData second{"O", glm::vec3(0.0f), glm::vec3(0.9f, 0.1f, 0.1f)};

		MatchSceneArrowColorToAtoms(arrow, first, second);

		EXPECT_TRUE(arrow.style.useGradient);
		EXPECT_EQ(arrow.style.gradient.start, first.color);
		EXPECT_EQ(arrow.style.gradient.finish, second.color);
	}

	TEST(SceneArrowAtomMatchTests, ReverseSwapsEndpointsWithoutRewritingGradientStops)
	{
		RendererWindowState::SceneArrow arrow;
		arrow.start = glm::vec3(1.0f, 2.0f, 3.0f);
		arrow.end = glm::vec3(4.0f, 5.0f, 6.0f);
		arrow.startAnchorAtom = 2;
		arrow.endAnchorAtom = 5;
		arrow.style.useGradient = true;
		arrow.style.gradient.start = glm::vec3(1.0f, 0.0f, 0.0f);
		arrow.style.gradient.finish = glm::vec3(0.0f, 0.0f, 1.0f);

		ReverseSceneArrow(arrow);

		EXPECT_EQ(arrow.start, glm::vec3(4.0f, 5.0f, 6.0f));
		EXPECT_EQ(arrow.end, glm::vec3(1.0f, 2.0f, 3.0f));
		EXPECT_EQ(arrow.startAnchorAtom, std::optional<std::size_t>(5));
		EXPECT_EQ(arrow.endAnchorAtom, std::optional<std::size_t>(2));
		EXPECT_EQ(arrow.style.gradient.start, glm::vec3(1.0f, 0.0f, 0.0f));
		EXPECT_EQ(arrow.style.gradient.finish, glm::vec3(0.0f, 0.0f, 1.0f));
	}

	TEST(SceneArrowAtomMatchTests, GateExplainsBothInvalidSelections)
	{
		const SceneArrowAtomMatchDescription none = DescribeSceneArrowAtomMatch(0);
		EXPECT_FALSE(none.canMatchPosition);
		EXPECT_FALSE(none.canMatchColor);
		EXPECT_EQ(none.positionTooltip, "Zaznacz dokladnie dwa atomy, aby dopasowac pozycje.");
		EXPECT_EQ(none.colorTooltip, "Zaznacz jeden lub dwa atomy, aby dopasowac kolor.");

		const SceneArrowAtomMatchDescription one = DescribeSceneArrowAtomMatch(1);
		EXPECT_FALSE(one.canMatchPosition);
		EXPECT_TRUE(one.canMatchColor);
		EXPECT_TRUE(one.colorTooltip.empty());

		const SceneArrowAtomMatchDescription two = DescribeSceneArrowAtomMatch(2);
		EXPECT_TRUE(two.canMatchPosition);
		EXPECT_TRUE(two.canMatchColor);
		EXPECT_TRUE(two.positionTooltip.empty());

		const SceneArrowAtomMatchDescription three = DescribeSceneArrowAtomMatch(3);
		EXPECT_FALSE(three.canMatchPosition);
		EXPECT_FALSE(three.canMatchColor);
	}

	TEST(SceneArrowClipboardTests, DuplicateOffsetsAndDetachesAnAnchoredArrow)
	{
		RendererWindowState window;
		RendererWindowState::SceneArrow arrow;
		arrow.id = window.sceneRegistry.AllocateObjectId();
		arrow.start = glm::vec3(1.0f, 2.0f, 3.0f);
		arrow.end = glm::vec3(4.0f, 5.0f, 6.0f);
		arrow.startAnchorAtom = 0;
		arrow.endAnchorAtom = 1;
		window.sceneArrows.push_back(arrow);
		window.selectedSceneArrows = {arrow.id};

		DuplicateSelectedSceneArrows(window);

		ASSERT_EQ(window.sceneArrows.size(), 2u);
		EXPECT_EQ(window.sceneArrows[0].startAnchorAtom, std::optional<std::size_t>(0));
		EXPECT_EQ(window.sceneArrows[0].endAnchorAtom, std::optional<std::size_t>(1));
		EXPECT_EQ(window.sceneArrows[1].start, arrow.start + glm::vec3(0.5f, 0.0f, 0.0f));
		EXPECT_EQ(window.sceneArrows[1].end, arrow.end + glm::vec3(0.5f, 0.0f, 0.0f));
		EXPECT_FALSE(window.sceneArrows[1].startAnchorAtom.has_value());
		EXPECT_FALSE(window.sceneArrows[1].endAnchorAtom.has_value());
	}

	TEST(SceneArrowClipboardTests, PasteOffsetsAndDetachesAnAnchoredArrow)
	{
		RendererWindowState window;
		RendererWindowState::SceneArrow arrow;
		arrow.start = glm::vec3(1.0f, 2.0f, 3.0f);
		arrow.end = glm::vec3(4.0f, 5.0f, 6.0f);
		arrow.startAnchorAtom = 0;
		arrow.endAnchorAtom = 1;
		GetSceneArrowClipboard() = {arrow};

		PasteSceneArrowsFromClipboard(window);

		ASSERT_EQ(window.sceneArrows.size(), 1u);
		EXPECT_EQ(window.sceneArrows[0].start, arrow.start + glm::vec3(0.5f, 0.0f, 0.0f));
		EXPECT_EQ(window.sceneArrows[0].end, arrow.end + glm::vec3(0.5f, 0.0f, 0.0f));
		EXPECT_FALSE(window.sceneArrows[0].startAnchorAtom.has_value());
		EXPECT_FALSE(window.sceneArrows[0].endAnchorAtom.has_value());
		GetSceneArrowClipboard().clear();
	}
} // namespace DefectStudio::Tests
