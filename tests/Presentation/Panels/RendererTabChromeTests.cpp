#include <gtest/gtest.h>

#include "Presentation/Panels/RendererTabChrome.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath MakePath(RendererWindowState &window)
		{
			ScenePath path;
			path.id = window.sceneRegistry.AllocateObjectId();
			PathNode first;
			first.id = AllocateElementId(path);
			first.position = glm::vec3(0.0f);
			PathNode second;
			second.id = AllocateElementId(path);
			second.position = glm::vec3(1.0f, 0.0f, 0.0f);
			path.nodes = {first, second};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			return path;
		}
	} // namespace

	TEST(ParseRendererWindowIdTests, ReturnsIdAfterRendererWindowMarker)
	{
		EXPECT_EQ(ParseRendererWindowId("Display title###RendererWindow_window-1"), "window-1");
	}

	TEST(ParseRendererWindowIdTests, IgnoresDirtyTitleMarker)
	{
		EXPECT_EQ(ParseRendererWindowId("Display title*###RendererWindow_window-1"), "window-1");
	}

	TEST(ParseRendererWindowIdTests, UsesLastMarkerWhenTitleContainsMarker)
	{
		EXPECT_EQ(ParseRendererWindowId("Display###title###RendererWindow_window-1"), "window-1");
	}

	TEST(ParseRendererWindowIdTests, ReturnsEmptyForNameWithoutMarker)
	{
		EXPECT_TRUE(ParseRendererWindowId("Display title").empty());
	}

	TEST(ParseRendererWindowIdTests, ReturnsEmptyForOtherIdentityPrefix)
	{
		EXPECT_TRUE(ParseRendererWindowId("Display title###OtherWindow_window-1").empty());
	}

	TEST(ParseRendererWindowIdTests, ReturnsEmptyWhenRendererWindowIdIsMissing)
	{
		EXPECT_TRUE(ParseRendererWindowId("Display title###RendererWindow_").empty());
	}

	TEST(ParseRendererWindowIdTests, ReturnsEmptyForEmptyInput)
	{
		EXPECT_TRUE(ParseRendererWindowId("").empty());
	}

	TEST(RendererTabHoldsContentTests, DefaultWindowHoldsNothing)
	{
		const RendererWindowState window;

		EXPECT_FALSE(RendererTabHoldsContent(window));
	}

	TEST(RendererTabHoldsContentTests, DomainStructureIdMeansWindowHoldsContent)
	{
		RendererWindowState window;
		window.structure.domainStructureId = "structure-id";

		EXPECT_TRUE(RendererTabHoldsContent(window));
	}

	// A preview window carries atoms with an empty domainStructureId on purpose
	// (CrystalStructurePreviewWindow.hpp), so atoms have to count on their own - checking only the
	// domain id would let Ctrl+W discard a whole structure without asking.
	TEST(RendererTabHoldsContentTests, AtomsAloneMeanWindowHoldsContent)
	{
		RendererWindowState window;
		window.structure.atoms.emplace_back();

		EXPECT_TRUE(RendererTabHoldsContent(window));
	}

	TEST(RendererTabHoldsContentTests, PinnedMeasurementMeansWindowHoldsContent)
	{
		RendererWindowState window;
		window.pinnedMeasurements.emplace_back();

		EXPECT_TRUE(RendererTabHoldsContent(window));
	}

	TEST(RendererTabHoldsContentTests, SceneOrbitalMeansWindowHoldsContent)
	{
		RendererWindowState window;
		window.sceneOrbitals.emplace_back();

		EXPECT_TRUE(RendererTabHoldsContent(window));
	}

	TEST(RendererTabHoldsContentTests, ScenePlaneMeansWindowHoldsContent)
	{
		RendererWindowState window;
		window.scenePlanes.emplace_back();

		EXPECT_TRUE(RendererTabHoldsContent(window));
	}

	TEST(RendererTabHoldsContentTests, FreeLabelMeansWindowHoldsContent)
	{
		RendererWindowState window;
		window.freeLabels.emplace_back();

		EXPECT_TRUE(RendererTabHoldsContent(window));
	}

	TEST(RendererTabHoldsContentTests, PathMeansWindowHoldsContent)
	{
		RendererWindowState window;
		PathSystem &paths = SceneSystem::EnsurePathSystem(window);
		ASSERT_TRUE(paths.Store().Insert(MakePath(window)));

		EXPECT_TRUE(RendererTabHoldsContent(window));
	}

	TEST(RendererTabHoldsContentTests, RemovingContentLeavesWindowEmpty)
	{
		RendererWindowState window;
		ASSERT_TRUE(SceneSystem::EnsurePathSystem(window).Store().Insert(ScenePath{SceneObjectId{1}}));
		ASSERT_TRUE(RendererTabHoldsContent(window));

		window.paths->Clear();

		EXPECT_FALSE(RendererTabHoldsContent(window));
	}
} // namespace DefectStudio::Tests
