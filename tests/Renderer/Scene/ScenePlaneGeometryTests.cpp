#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "Renderer/Scene/ScenePlaneGeometry.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// The camera looks down -z unless a test says otherwise, so a plane fitted to face it
		// should come back with its normal pointing at +z.
		const glm::vec3 kLookingDown = glm::vec3(0.0f, 0.0f, -1.0f);

		[[nodiscard]] float DistanceToPlane(const ScenePlaneFit &fit, const glm::vec3 &point)
		{
			return std::abs(glm::dot(point - fit.center, fit.normal));
		}
	} // namespace

	TEST(FitScenePlaneTests, ThreePointsGiveThePlaneTheyLieIn)
	{
		const std::vector<glm::vec3> points = {
			glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(3.0f, 0.0f, 2.0f), glm::vec3(0.0f, 4.0f, 2.0f)};

		const std::optional<ScenePlaneFit> fit = FitScenePlane(points, kLookingDown);
		ASSERT_TRUE(fit.has_value());

		for (const glm::vec3 &point : points)
			EXPECT_NEAR(DistanceToPlane(*fit, point), 0.0f, 1e-4f);

		EXPECT_NEAR(glm::length(fit->normal), 1.0f, 1e-4f);
		EXPECT_NEAR(glm::length(fit->tangent), 1.0f, 1e-4f);
		EXPECT_NEAR(glm::dot(fit->normal, fit->tangent), 0.0f, 1e-4f);
		// Centroid, not the first point or the bounding-box middle.
		EXPECT_NEAR(fit->center.x, 1.0f, 1e-4f);
		EXPECT_NEAR(fit->center.y, 4.0f / 3.0f, 1e-4f);
		EXPECT_NEAR(fit->center.z, 2.0f, 1e-4f);
	}

	TEST(FitScenePlaneTests, TheNormalTurnsToFaceTheViewer)
	{
		const std::vector<glm::vec3> points = {
			glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};

		const std::optional<ScenePlaneFit> facingUs = FitScenePlane(points, kLookingDown);
		ASSERT_TRUE(facingUs.has_value());
		EXPECT_LT(glm::dot(facingUs->normal, kLookingDown), 0.0f);

		// Same points, camera on the other side: the plane must flip rather than present its back.
		const std::optional<ScenePlaneFit> fromBehind =
			FitScenePlane(points, glm::vec3(0.0f, 0.0f, 1.0f));
		ASSERT_TRUE(fromBehind.has_value());
		EXPECT_LT(glm::dot(fromBehind->normal, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
	}

	TEST(FitScenePlaneTests, TwoPointsGiveTheLegiblePlaneThroughThem)
	{
		// Two atoms do not determine a plane. The useful answer is the one plane through them that
		// the camera is looking straight at - anything else is drawn edge-on and invisible.
		const std::vector<glm::vec3> points = {
			glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};

		const std::optional<ScenePlaneFit> fit = FitScenePlane(points, kLookingDown);
		ASSERT_TRUE(fit.has_value());

		for (const glm::vec3 &point : points)
			EXPECT_NEAR(DistanceToPlane(*fit, point), 0.0f, 1e-4f);
		// Face-on to a camera looking down -z means the normal is +z.
		EXPECT_NEAR(std::abs(fit->normal.z), 1.0f, 1e-3f);
		EXPECT_GT(fit->normal.z, 0.0f);
		// The quad runs along the pair, not across it.
		EXPECT_NEAR(std::abs(glm::dot(fit->tangent, glm::vec3(1.0f, 0.0f, 0.0f))), 1.0f, 1e-3f);
	}

	TEST(FitScenePlaneTests, CollinearPointsDoNotCollapseTheFit)
	{
		// Three atoms in a row are as under-determined as two, and hit the same fallback rather
		// than producing a degenerate normal.
		const std::vector<glm::vec3> points = {
			glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(2.0f, 2.0f, 0.0f)};

		const std::optional<ScenePlaneFit> fit = FitScenePlane(points, kLookingDown);
		ASSERT_TRUE(fit.has_value());
		EXPECT_NEAR(glm::length(fit->normal), 1.0f, 1e-4f);
		for (const glm::vec3 &point : points)
			EXPECT_NEAR(DistanceToPlane(*fit, point), 0.0f, 1e-4f);
	}

	TEST(FitScenePlaneTests, TheQuadIsBigEnoughToFrameWhatWasPicked)
	{
		const std::vector<glm::vec3> points = {glm::vec3(-5.0f, -3.0f, 0.0f),
			glm::vec3(5.0f, -3.0f, 0.0f), glm::vec3(5.0f, 3.0f, 0.0f), glm::vec3(-5.0f, 3.0f, 0.0f)};

		const std::optional<ScenePlaneFit> fit = FitScenePlane(points, kLookingDown);
		ASSERT_TRUE(fit.has_value());

		const glm::vec3 bitangent = glm::cross(fit->normal, fit->tangent);
		for (const glm::vec3 &point : points)
		{
			const glm::vec3 offset = point - fit->center;
			EXPECT_LE(std::abs(glm::dot(offset, fit->tangent)), fit->halfExtents.x + 1e-3f);
			EXPECT_LE(std::abs(glm::dot(offset, bitangent)), fit->halfExtents.y + 1e-3f);
		}
		EXPECT_GT(fit->halfExtents.x, 0.0f);
		EXPECT_GT(fit->halfExtents.y, 0.0f);
	}

	TEST(FitScenePlaneTests, NothingToFitIsNulloptRatherThanAZeroSizedPlane)
	{
		EXPECT_FALSE(FitScenePlane({}, kLookingDown).has_value());
		EXPECT_FALSE(FitScenePlane({glm::vec3(1.0f, 2.0f, 3.0f)}, kLookingDown).has_value());
		EXPECT_FALSE(
			FitScenePlane({glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f)}, kLookingDown)
				.has_value());
	}

	TEST(ScenePlaneCornersTests, FourCornersAroundTheCentreInThePlane)
	{
		RendererWindowState::ScenePlane plane;
		plane.center = glm::vec3(1.0f, 2.0f, 3.0f);
		plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
		plane.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		plane.halfExtents = glm::vec2(2.0f, 0.5f);

		const std::array<glm::vec3, 4> corners = ScenePlaneCorners(plane);

		glm::vec3 sum(0.0f);
		for (const glm::vec3 &corner : corners)
		{
			EXPECT_NEAR(glm::dot(corner - plane.center, plane.normal), 0.0f, 1e-4f);
			sum += corner;
		}
		// The four corners average back to the centre - the quad is centred, not hung off it.
		const glm::vec3 average = sum / 4.0f;
		EXPECT_NEAR(average.x, plane.center.x, 1e-4f);
		EXPECT_NEAR(average.y, plane.center.y, 1e-4f);
		EXPECT_NEAR(average.z, plane.center.z, 1e-4f);

		// ...and it is 2 x halfExtents across, wound counter-clockwise seen from +normal.
		EXPECT_NEAR(glm::length(corners[1] - corners[0]), 2.0f * plane.halfExtents.x, 1e-4f);
		EXPECT_NEAR(glm::length(corners[2] - corners[1]), 2.0f * plane.halfExtents.y, 1e-4f);
		const glm::vec3 winding = glm::cross(corners[1] - corners[0], corners[2] - corners[1]);
		EXPECT_GT(glm::dot(winding, plane.normal), 0.0f);
	}

	TEST(MakeScenePlaneTests, ANewPlaneCarriesTheFitAndTheDrawingDefaults)
	{
		ScenePlaneFit fit;
		fit.center = glm::vec3(4.0f, 0.0f, -1.0f);
		fit.normal = glm::vec3(0.0f, 1.0f, 0.0f);
		fit.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		fit.halfExtents = glm::vec2(3.0f, 1.5f);

		const RendererWindowState::ScenePlane plane = MakeScenePlane(fit);
		EXPECT_EQ(plane.center, fit.center);
		EXPECT_EQ(plane.normal, fit.normal);
		EXPECT_EQ(plane.tangent, fit.tangent);
		EXPECT_EQ(plane.halfExtents, fit.halfExtents);
		// Translucent by default - a plane you cannot see through hides the structure it is about.
		EXPECT_GT(plane.alpha, 0.0f);
		EXPECT_LT(plane.alpha, 1.0f);
		EXPECT_TRUE(plane.visible);
		EXPECT_TRUE(plane.renderable);
		// Identity is assigned by the caller from the SceneRegistry, not invented here.
		EXPECT_FALSE(plane.id.IsValid());
	}

	namespace
	{
		// One 4x2 plane in the z=0 sheet, facing +z, so a ray straight down the -z axis hits it and a
		// ray past its edge does not.
		RendererWindowState::ScenePlane MakeFacingPlane()
		{
			RendererWindowState::ScenePlane plane;
			plane.id = SceneObjectId{1};
			plane.center = glm::vec3(0.0f);
			plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
			plane.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
			plane.halfExtents = glm::vec2(2.0f, 1.0f);
			return plane;
		}
	} // namespace

	TEST(ScenePlanePickTests, RayThroughTheQuadHitsIt)
	{
		RendererWindowState window;
		window.scenePlanes = {MakeFacingPlane()};

		const std::optional<std::size_t> hit =
			PickScenePlane(window, glm::vec3(0.5f, 0.5f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f));

		ASSERT_TRUE(hit.has_value());
		EXPECT_EQ(*hit, 0u);
	}

	TEST(ScenePlanePickTests, RayPastTheEdgeMisses)
	{
		RendererWindowState window;
		window.scenePlanes = {MakeFacingPlane()};

		// Inside the half-width along the tangent, outside the half-height along the bitangent.
		EXPECT_FALSE(PickScenePlane(window, glm::vec3(1.0f, 1.5f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f)).has_value());
	}

	TEST(ScenePlanePickTests, APlaneBehindTheRayOriginMisses)
	{
		RendererWindowState window;
		window.scenePlanes = {MakeFacingPlane()};

		EXPECT_FALSE(PickScenePlane(window, glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, 1.0f)).has_value());
	}

	TEST(ScenePlanePickTests, TheNearestOfTwoStackedPlanesWins)
	{
		RendererWindowState window;
		RendererWindowState::ScenePlane far = MakeFacingPlane();
		RendererWindowState::ScenePlane near = MakeFacingPlane();
		near.id = SceneObjectId{2};
		near.center = glm::vec3(0.0f, 0.0f, 5.0f);
		window.scenePlanes = {far, near};

		const std::optional<std::size_t> hit =
			PickScenePlane(window, glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f));

		ASSERT_TRUE(hit.has_value());
		EXPECT_EQ(*hit, 1u);
	}

	TEST(ScenePlanePickTests, AHiddenPlaneIsNotPickable)
	{
		RendererWindowState window;
		RendererWindowState::ScenePlane plane = MakeFacingPlane();
		plane.visible = false;
		window.scenePlanes = {plane};

		EXPECT_FALSE(PickScenePlane(window, glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f)).has_value());
	}

	TEST(ScenePlanePickTests, AnEdgeOnPlaneIsNotPickable)
	{
		RendererWindowState window;
		window.scenePlanes = {MakeFacingPlane()};

		// Ray parallel to the sheet: no thickness to hit.
		EXPECT_FALSE(PickScenePlane(window, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)).has_value());
	}

	namespace
	{
		RendererWindowState MakeAnchoredPlaneWindow()
		{
			RendererWindowState window;
			for (const glm::vec3 &position : {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f),
					 glm::vec3(0.0f, 2.0f, 0.0f)})
			{
				RendererAtomData atom;
				atom.cartesianPosition = position;
				window.structure.atoms.push_back(atom);
			}

			RendererWindowState::ScenePlane plane;
			plane.id = SceneObjectId{7};
			plane.anchorAtoms = {0, 1, 2};
			window.scenePlanes.push_back(plane);
			return window;
		}
	} // namespace

	TEST(ScenePlaneAnchorTests, AnAnchoredPlaneFollowsItsAtoms)
	{
		RendererWindowState window = MakeAnchoredPlaneWindow();

		ResolveAnchoredScenePlanes(window);
		const glm::vec3 first = window.scenePlanes[0].center;
		// Centroid of the three, and the frame is the one the atoms span.
		EXPECT_NEAR(first.x, 4.0f / 3.0f, 1e-4f);
		EXPECT_NEAR(first.y, 2.0f / 3.0f, 1e-4f);
		EXPECT_NEAR(std::abs(window.scenePlanes[0].normal.z), 1.0f, 1e-4f);

		window.structure.atoms[1].cartesianPosition = glm::vec3(4.0f, 0.0f, 6.0f);
		ResolveAnchoredScenePlanes(window);
		EXPECT_GT(std::abs(window.scenePlanes[0].center.z), 1e-3f);
		EXPECT_LT(std::abs(window.scenePlanes[0].normal.z), 0.999f);
		EXPECT_NEAR(glm::length(window.scenePlanes[0].normal), 1.0f, 1e-4f);
		EXPECT_NEAR(glm::dot(window.scenePlanes[0].normal, window.scenePlanes[0].tangent), 0.0f, 1e-4f);
	}

	TEST(ScenePlaneAnchorTests, AFreePlaneIsLeftAlone)
	{
		RendererWindowState window = MakeAnchoredPlaneWindow();
		window.scenePlanes[0].anchorAtoms.clear();
		window.scenePlanes[0].center = glm::vec3(9.0f, 9.0f, 9.0f);

		ResolveAnchoredScenePlanes(window);

		EXPECT_NEAR(window.scenePlanes[0].center.x, 9.0f, 1e-4f);
	}

	TEST(ScenePlaneAnchorTests, TooFewResolvableAnchorsChangeNothing)
	{
		RendererWindowState window = MakeAnchoredPlaneWindow();
		// One real index and one past the end: not two points, so there is no plane to fit.
		window.scenePlanes[0].anchorAtoms = {0, 99};
		window.scenePlanes[0].center = glm::vec3(9.0f, 9.0f, 9.0f);

		ResolveAnchoredScenePlanes(window);

		EXPECT_NEAR(window.scenePlanes[0].center.x, 9.0f, 1e-4f);
		EXPECT_EQ(window.scenePlanes[0].anchorAtoms.size(), 2u);
	}

	TEST(ScenePlaneAnchorTests, ADefaultPlaneFacesTheViewerAndHasRoom)
	{
		RendererWindowState window = MakeAnchoredPlaneWindow();

		const RendererWindowState::ScenePlane plane =
			MakeDefaultScenePlane(window, glm::vec3(1.0f, 2.0f, 3.0f));

		EXPECT_NEAR(plane.center.x, 1.0f, 1e-4f);
		EXPECT_NEAR(plane.center.z, 3.0f, 1e-4f);
		EXPECT_TRUE(plane.anchorAtoms.empty());
		EXPECT_GT(plane.halfExtents.x, 0.1f);
		EXPECT_GT(plane.halfExtents.y, 0.1f);
		EXPECT_NEAR(glm::length(plane.normal), 1.0f, 1e-4f);
		EXPECT_NEAR(glm::dot(plane.normal, plane.tangent), 0.0f, 1e-4f);
	}
} // namespace DefectStudio::Tests
