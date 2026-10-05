#include "Core/dspch.hpp"

#include <algorithm>
#include <numbers>
#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Operators/SceneOperatorRegistry.hpp"
#include "Presentation/Panels/OperatorRedoPanel.hpp"
#include "Presentation/Panels/ScenePathCurvedArrow.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

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

namespace DefectStudio::Tests
{
	namespace
	{
		SceneOperator ObserveRelevance(const SceneOperator &source, const RendererWindowState &window,
			std::vector<std::string> &hidden)
		{
			SceneOperator op = source;
			op.isParameterRelevant = [rule = source.isParameterRelevant, atoms = window.selectedAtomIndices,
				vacancies = window.selectedVacancies, firstKey = source.schema.front().key, &hidden]
				(const std::string &key, const SceneOperatorValues &values, const RendererWindowState &input) {
				EXPECT_EQ(input.selectedAtomIndices, atoms);
				EXPECT_EQ(input.selectedVacancies, vacancies);
				if (key == firstKey) hidden.clear();
				const bool relevant = rule(key, values, input);
				if (!relevant) hidden.push_back(key);
				return relevant;
			};
			return op;
		}
	}

	TEST_F(OperatorRedoPanelTests, CycleEveryRelevantParameterChangesAllArrows)
	{
		auto &window = Window();
		window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {-1, 1.7320508f, 0}}, {"C", {-1, -1.7320508f, 0}}};
		window.selectedAtomIndices = {0, 1, 2};
		std::vector<std::string> hidden;
		const auto op = ObserveRelevance(CurvedArrow(), window, hidden);
		ASSERT_TRUE(panel.RunAndOpen(op, window));
		const std::vector<std::string> expectedHidden{
			"axisMode", "radiusRule", "radiusFactor", "sweepDegrees", "rotationDegrees"};
		EXPECT_EQ(hidden, expectedHidden);
		ASSERT_EQ(PathCount(window), 3u);
		std::vector<ScenePath> before;
		for (const auto id : window.paths->Store().Ids()) before.push_back(*window.paths->Store().Find(id));
		const SceneOperatorValues edits{
			{"curvature", 1.0f}, {"decoration", 0}, {"color", glm::vec3(0, 1, 0)}, {"strokeWidth", 0.2f}};
		std::size_t checked = 0;
		for (const auto &parameter : op.schema)
		{
			if (std::find(expectedHidden.begin(), expectedHidden.end(), parameter.key) != expectedHidden.end())
				continue;
			SCOPED_TRACE(parameter.key);
			ASSERT_NE(edits.find(parameter.key), edits.end());
			auto values = op.defaults;
			values[parameter.key] = edits.at(parameter.key);
			ASSERT_TRUE(panel.Reapply(window, values));
			EXPECT_TRUE(panel.IsOpen());
			EXPECT_EQ(hidden, expectedHidden);
			ASSERT_EQ(PathCount(window), 3u);
			const auto ids = window.paths->Store().Ids();
			for (std::size_t i = 0; i < ids.size(); ++i)
			{
				const auto &path = *window.paths->Store().Find(ids[i]);
				if (parameter.key == "curvature")
				{
					const auto &arc = std::get<CircularArcSegmentData>(path.segments.front().data);
					EXPECT_NEAR(arc.signedSweepRadians, 2.0f * std::numbers::pi_v<float> / 3.0f, 1.0e-5f);
					const auto context = SceneSystem::MakePathBindingContext(window);
					const auto oldSample = EvaluateSegment(before[i], ResolveNodePositions(before[i], context), 0, 0.5);
					const auto newSample = EvaluateSegment(path, ResolveNodePositions(path, context), 0, 0.5);
					ASSERT_TRUE(oldSample);
					ASSERT_TRUE(newSample);
					EXPECT_GT(glm::distance(oldSample->position, newSample->position), 1.0e-3);
				}
				else if (parameter.key == "decoration")
				{
					EXPECT_EQ(path.style.endDecoration.kind, PathDecorationKind::None);
					EXPECT_NE(path.style.endDecoration.kind, before[i].style.endDecoration.kind);
				}
				else if (parameter.key == "color")
				{
					EXPECT_EQ(path.style.color, glm::vec3(0, 1, 0));
					EXPECT_NE(path.style.color, before[i].style.color);
				}
				else if (parameter.key == "strokeWidth")
				{
					EXPECT_FLOAT_EQ(path.style.width, 0.2f);
					EXPECT_NE(path.style.width, before[i].style.width);
				}
			}
			++checked;
		}
		EXPECT_EQ(checked, edits.size());
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}

	TEST_F(OperatorRedoPanelTests, AxisSwitchRefreshesBondAndTwoEndParameters)
	{
		auto &window = Window();
		window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {-1, 1.7320508f, 0}}};
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->z = {0, 0, 1};
		std::vector<std::string> hidden;
		const auto op = ObserveRelevance(CurvedArrow(), window, hidden);
		ASSERT_TRUE(panel.RunAndOpen(op, window));
		EXPECT_EQ(hidden, std::vector<std::string>{"curvature"});
		auto values = panel.Values();
		values["axisMode"] = static_cast<int>(CurvedArrowAxisMode::DefectZ);
		ASSERT_TRUE(panel.Reapply(window, values));
		const std::vector<std::string> nonBondHidden{
			"radiusRule", "radiusFactor", "sweepDegrees", "rotationDegrees"};
		EXPECT_EQ(hidden, nonBondHidden);
		values["curvature"] = 1.0f;
		ASSERT_TRUE(panel.Reapply(window, values));
		ASSERT_EQ(PathCount(window), 1u);
		const auto &path = *window.paths->Store().Find(window.paths->Store().Ids().front());
		EXPECT_NEAR(std::get<CircularArcSegmentData>(path.segments.front().data).signedSweepRadians,
			2.0f * std::numbers::pi_v<float> / 3.0f, 1.0e-5f);
		values["axisMode"] = static_cast<int>(CurvedArrowAxisMode::Bond);
		ASSERT_TRUE(panel.Reapply(window, values));
		EXPECT_EQ(hidden, std::vector<std::string>{"curvature"});
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}

	TEST_F(OperatorRedoPanelTests, OperatorWithoutRelevanceRuleStillRunsAndReapplies)
	{
		auto op = CurvedArrow();
		op.isParameterRelevant = {};
		ASSERT_TRUE(panel.RunAndOpen(op, Window()));
		auto values = panel.Values();
		values["sweepDegrees"] = 90.0f;
		ASSERT_TRUE(panel.Reapply(Window(), values));
		EXPECT_TRUE(panel.IsOpen());
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}
}
