#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Presentation/PanelDockRegions.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// A 1000x800 dockspace with the viewport occupying the middle band, the shape the toggles
		// actually run against: sidebars down both edges, a console strip along the bottom.
		constexpr DockRectangle kCentral{{200.0f, 40.0f}, {800.0f, 600.0f}};

		[[nodiscard]] DockRectangle Rect(float minX, float minY, float maxX, float maxY)
		{
			return DockRectangle{{minX, minY}, {maxX, maxY}};
		}
	} // namespace

	TEST(PanelDockRegionsTests, ClassifiesEachSideOfTheCentralNode)
	{
		EXPECT_EQ(ClassifyDockRegion(Rect(0.0f, 40.0f, 200.0f, 600.0f), kCentral), DockRegion::Left);
		EXPECT_EQ(ClassifyDockRegion(Rect(800.0f, 40.0f, 1000.0f, 600.0f), kCentral), DockRegion::Right);
		EXPECT_EQ(ClassifyDockRegion(Rect(200.0f, 600.0f, 800.0f, 800.0f), kCentral), DockRegion::Bottom);
		EXPECT_EQ(ClassifyDockRegion(Rect(200.0f, 0.0f, 800.0f, 40.0f), kCentral), DockRegion::Top);
	}

	TEST(PanelDockRegionsTests, TheCentralNodeItselfAndAnythingOverlappingItIsCentral)
	{
		EXPECT_EQ(ClassifyDockRegion(kCentral, kCentral), DockRegion::Central);
		// A renderer window sharing the central node's area, inset.
		EXPECT_EQ(ClassifyDockRegion(Rect(300.0f, 100.0f, 700.0f, 500.0f), kCentral), DockRegion::Central);
	}

	TEST(PanelDockRegionsTests, ACornerDockGoesToTheAxisItClearsByMore)
	{
		// Clears central on the left by 200 and below it by 50 - horizontal wins.
		EXPECT_EQ(ClassifyDockRegion(Rect(-200.0f, 650.0f, 0.0f, 800.0f), kCentral), DockRegion::Left);
		// Clears below by 300 and to the right by 20 - vertical wins.
		EXPECT_EQ(ClassifyDockRegion(Rect(820.0f, 900.0f, 1000.0f, 1000.0f), kCentral), DockRegion::Bottom);
	}

	TEST(PanelDockRegionsTests, NoCentralNodeMeansEverythingFloats)
	{
		const DockRectangle empty{};
		EXPECT_EQ(ClassifyDockRegion(Rect(0.0f, 0.0f, 200.0f, 600.0f), empty), DockRegion::Floating);
	}

	TEST(PanelDockRegionsTests, TheTrackerRemembersARegionAfterThePanelStopsReportingOne)
	{
		DockRegionTracker tracker;
		tracker.Observe("Project Tree", DockRegion::Left);

		// The panel is hidden now, so nothing observes it this frame - and ImGui has merged away
		// the dock node it used to live in. It still belongs to the left toggle.
		EXPECT_EQ(tracker.RegionOf("Project Tree"), DockRegion::Left);
	}

	TEST(PanelDockRegionsTests, TheTrackerFollowsAPanelDraggedToAnotherEdge)
	{
		DockRegionTracker tracker;
		tracker.Observe("Object Properties", DockRegion::Left);
		tracker.Observe("Object Properties", DockRegion::Right);

		EXPECT_EQ(tracker.RegionOf("Object Properties"), DockRegion::Right);
	}

	TEST(PanelDockRegionsTests, AnUnseenPanelBelongsToNoRegion)
	{
		const DockRegionTracker tracker;

		EXPECT_EQ(tracker.RegionOf("Terminal"), DockRegion::Floating);
	}

	TEST(PanelDockRegionsTests, TogglingHidesEveryVisiblePanelInTheRegion)
	{
		DockRegionToggle toggle;
		EXPECT_FALSE(toggle.IsHidden());

		const DockRegionToggle::Decision decision = toggle.Toggle({"Project Tree", "Scene Outliner"});

		EXPECT_FALSE(decision.makeVisible);
		EXPECT_EQ(decision.titles, (std::vector<std::string>{"Project Tree", "Scene Outliner"}));
		EXPECT_TRUE(toggle.IsHidden());
	}

	TEST(PanelDockRegionsTests, TogglingBackRestoresExactlyWhatWasHidden)
	{
		DockRegionToggle toggle;
		(void)toggle.Toggle({"Project Tree", "Scene Outliner"});

		// The region is empty now - every panel in it was hidden by the first toggle.
		const DockRegionToggle::Decision decision = toggle.Toggle({});

		EXPECT_TRUE(decision.makeVisible);
		EXPECT_EQ(decision.titles, (std::vector<std::string>{"Project Tree", "Scene Outliner"}));
		EXPECT_FALSE(toggle.IsHidden());
	}

	TEST(PanelDockRegionsTests, RestoringNeverOpensAPanelTheUserHadClosed)
	{
		DockRegionToggle toggle;
		// Text Editor is docked left but closed, so it is not among the visible titles and must not
		// come back when the region is restored.
		(void)toggle.Toggle({"Project Tree"});

		const DockRegionToggle::Decision decision = toggle.Toggle({});

		EXPECT_EQ(decision.titles, (std::vector<std::string>{"Project Tree"}));
	}

	TEST(PanelDockRegionsTests, TogglingAnEmptyRegionWithNothingRememberedDoesNothing)
	{
		DockRegionToggle toggle;

		const DockRegionToggle::Decision decision = toggle.Toggle({});

		EXPECT_TRUE(decision.titles.empty());
		EXPECT_FALSE(toggle.IsHidden());
	}

	TEST(PanelDockRegionsTests, ForgettingDropsTheRestoreSet)
	{
		DockRegionToggle toggle;
		(void)toggle.Toggle({"Project Tree"});
		toggle.Forget();

		EXPECT_FALSE(toggle.IsHidden());
		EXPECT_TRUE(toggle.Toggle({}).titles.empty());
	}
} // namespace DefectStudio::Tests
