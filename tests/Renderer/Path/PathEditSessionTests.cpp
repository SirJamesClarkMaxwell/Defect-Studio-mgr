#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Renderer/Path/PathEditSession.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath MakePath(const SceneObjectId id)
		{
			ScenePath path;
			path.id = id;

			PathNode first;
			first.id = AllocateElementId(path);
			PathNode second;
			second.id = AllocateElementId(path);
			path.nodes = {first, second};

			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			return path;
		}
	} // namespace

	TEST(PathEditSessionTests, DefaultSessionIsInactiveAndUnselected)
	{
		const PathEditSession session;

		EXPECT_FALSE(session.IsActive());
		EXPECT_TRUE(session.Selection().empty());
		EXPECT_FALSE(session.ActiveElement().IsValid());
	}

	TEST(PathEditSessionTests, EnterActivatesAndReportsTheOpenedPath)
	{
		PathEditSession session;
		const SceneObjectId path{1};

		session.Enter(path);

		EXPECT_TRUE(session.IsActive());
		EXPECT_EQ(session.Path(), path);
	}

	TEST(PathEditSessionTests, LeaveDeactivatesAndClearsTheSelection)
	{
		const ScenePath path = MakePath(SceneObjectId{1});
		PathEditSession session;
		session.Enter(path.id);
		session.SetSelection({path.nodes.front().id});

		session.Leave();

		EXPECT_FALSE(session.IsActive());
		EXPECT_TRUE(session.Selection().empty());
		EXPECT_FALSE(session.ActiveElement().IsValid());
	}

	TEST(PathEditSessionTests, DifferentPathClearsSelectionButReenteringTheSamePathKeepsIt)
	{
		const ScenePath firstPath = MakePath(SceneObjectId{1});
		const ScenePath secondPath = MakePath(SceneObjectId{2});
		PathEditSession session;
		session.Enter(firstPath.id);
		session.SetSelection({firstPath.nodes.front().id});

		session.Enter(secondPath.id);
		EXPECT_TRUE(session.Selection().empty());

		session.SetSelection({secondPath.nodes.back().id});
		session.Enter(secondPath.id);

		EXPECT_EQ(session.Selection(), (std::vector<PathElementId>{secondPath.nodes.back().id}));
	}

	TEST(PathEditSessionTests, SetSelectionIsIgnoredWhileInactive)
	{
		const ScenePath path = MakePath(SceneObjectId{1});
		PathEditSession session;

		session.SetSelection({path.nodes.front().id});

		EXPECT_TRUE(session.Selection().empty());
	}

	TEST(PathEditSessionTests, ActiveElementIsTheLastSelectedElement)
	{
		const ScenePath path = MakePath(SceneObjectId{1});
		PathEditSession session;
		session.Enter(path.id);
		const std::vector<PathElementId> selection = {path.nodes.front().id, path.segments.front().id, path.nodes.back().id};

		session.SetSelection(selection);

		ASSERT_EQ(session.Selection(), selection);
		EXPECT_EQ(session.ActiveElement(), selection.back());
		EXPECT_NE(session.ActiveElement(), selection.front());
		EXPECT_NE(session.ActiveElement(), selection[1]);
	}

	TEST(PathEditSessionTests, IsSelectedReportsMembership)
	{
		const ScenePath path = MakePath(SceneObjectId{1});
		PathEditSession session;
		session.Enter(path.id);
		session.SetSelection({path.nodes.front().id, path.segments.front().id});

		EXPECT_TRUE(session.IsSelected(path.nodes.front().id));
		EXPECT_TRUE(session.IsSelected(path.segments.front().id));
		EXPECT_FALSE(session.IsSelected(path.nodes.back().id));
	}

	TEST(PathEditSessionTests, ClearSelectionKeepsTheSessionActiveAndUnsetsTheActiveElement)
	{
		const ScenePath path = MakePath(SceneObjectId{1});
		PathEditSession session;
		session.Enter(path.id);
		session.SetSelection({path.nodes.front().id});

		session.ClearSelection();

		EXPECT_TRUE(session.IsActive());
		EXPECT_TRUE(session.Selection().empty());
		EXPECT_FALSE(session.ActiveElement().IsValid());
	}

	TEST(PathEditSessionTests, ElementModeRoundTripsEveryMode)
	{
		PathEditSession session;

		for (const PathElementMode mode : {PathElementMode::NodeHandle, PathElementMode::Segment, PathElementMode::WholePath})
		{
			session.SetElementMode(mode);
			EXPECT_EQ(session.ElementMode(), mode);
		}
	}

	// A segment id and a node id are both a PathElementId and neither says which it is, so a
	// selection surviving a mode switch would be read as the new mode's kind of thing.
	TEST(PathEditSessionTests, ChangingElementModeClearsTheSelection)
	{
		const ScenePath path = MakePath(SceneObjectId{1});
		PathEditSession session;
		session.Enter(path.id);
		session.SetSelection({path.nodes.front().id, path.nodes.back().id});
		ASSERT_FALSE(session.Selection().empty());

		session.SetElementMode(PathElementMode::Segment);

		EXPECT_TRUE(session.Selection().empty());
		EXPECT_EQ(session.ElementMode(), PathElementMode::Segment);
		EXPECT_TRUE(session.IsActive());
	}

	TEST(PathEditSessionTests, SettingTheModeItIsAlreadyInKeepsTheSelection)
	{
		const ScenePath path = MakePath(SceneObjectId{1});
		PathEditSession session;
		session.Enter(path.id);
		const std::vector<PathElementId> selection = {path.nodes.front().id, path.nodes.back().id};
		session.SetSelection(selection);

		session.SetElementMode(session.ElementMode());

		EXPECT_EQ(session.Selection(), selection);
	}

	TEST(PathEditSessionTests, PruneSelectionDropsMissingIdsAndPreservesSurvivingOrder)
	{
		ScenePath path = MakePath(SceneObjectId{1});
		const PathElementId missing = AllocateElementId(path);
		const std::vector<PathElementId> selection = {
			path.nodes.back().id, missing, path.segments.front().id, path.nodes.front().id};
		PathEditSession session;
		session.Enter(path.id);
		session.SetSelection(selection);

		session.PruneSelection(path);

		const std::vector<PathElementId> survivors = {
			path.nodes.back().id, path.segments.front().id, path.nodes.front().id};
		EXPECT_EQ(session.Selection(), survivors);
		EXPECT_EQ(session.ActiveElement(), survivors.back());
	}

	TEST(PathEditSessionTests, PruneSelectionAgainstAnotherPathLeavesSelectionUntouched)
	{
		const ScenePath sessionPath = MakePath(SceneObjectId{1});
		const ScenePath otherPath = MakePath(SceneObjectId{2});
		PathEditSession session;
		session.Enter(sessionPath.id);
		const std::vector<PathElementId> selection = {sessionPath.nodes.front().id, sessionPath.segments.front().id};
		session.SetSelection(selection);

		session.PruneSelection(otherPath);

		EXPECT_EQ(session.Selection(), selection);
	}

	TEST(PathEditSessionTests, PruneSelectionOnInactiveSessionLeavesStateUnchanged)
	{
		const ScenePath path = MakePath(SceneObjectId{1});
		PathEditSession session;
		const std::vector<PathElementId> selectionBefore = session.Selection();
		const PathElementId activeBefore = session.ActiveElement();

		session.PruneSelection(path);

		EXPECT_FALSE(session.IsActive());
		EXPECT_EQ(session.Selection(), selectionBefore);
		EXPECT_EQ(session.ActiveElement(), activeBefore);
	}
} // namespace DefectStudio::Tests
