#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
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

	// Task 33's decision on the open question: only the exact mathematical singularity above -
	// a ray running perfectly parallel to the sheet - stays unpickable. A ray that grazes the
	// plane at even a fraction of a degree off parallel (which is what "clicking a plane you can
	// see edge-on" actually looks like, now that its border keeps a minimum screen-space width
	// instead of vanishing) still resolves against the real quad geometry unchanged; picking
	// needed no change for the rendering fix to make edge-on planes usable again.
	TEST(ScenePlanePickTests, ARayJustBarelyOffParallelStillHitsTheEdgeOnQuad)
	{
		RendererWindowState window;
		window.scenePlanes = {MakeFacingPlane()};

		const std::optional<std::size_t> hit = PickScenePlane(
			window, glm::vec3(-2.0f, 0.0f, -0.02f), glm::vec3(1.0f, 0.0f, 0.01f));

		ASSERT_TRUE(hit.has_value());
		EXPECT_EQ(*hit, 0u);
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

	namespace
	{
		// Looking almost exactly down +y (yaw=0, pitch=0 would be exactly down +y - a hair of yaw
		// keeps every direction this test probes non-degenerate in orthographic projection, where a
		// probe offset that is *exactly* along the view axis contributes literally zero screen
		// delta and WorldUnitsPerPixelAt has nothing to measure).
		[[nodiscard]] RendererViewCamera MakeTestCamera(const glm::vec2 &viewportPixelSize)
		{
			RendererViewCamera camera;
			camera.SetProjection(CameraProjection::Orthographic);
			camera.SetViewport(viewportPixelSize.x, viewportPixelSize.y);
			camera.SetOrbitState(glm::vec3(0.0f), 10.0f, 0.001f, 0.0f);
			return camera;
		}

		[[nodiscard]] float ScreenPixelsForWorldWidth(
			const RendererViewCamera &camera, const glm::vec3 &worldPoint, const glm::vec3 &axis, float worldWidth,
			const glm::vec2 &viewportPixelSize)
		{
			const std::optional<float> worldPerPixel = WorldUnitsPerPixelAt(camera, worldPoint, axis, viewportPixelSize);
			// nullopt only when the axis truly has zero screen extent (the camera setup above avoids
			// that); a test that hit it would be a broken test, not a passing one.
			EXPECT_TRUE(worldPerPixel.has_value());
			return worldWidth / worldPerPixel.value_or(std::numeric_limits<float>::infinity());
		}
	} // namespace

	TEST(WorldUnitsPerPixelAtTests, OrthographicRatioIsTheSameEverywhereInTheScene)
	{
		const glm::vec2 viewportPixelSize(800.0f, 600.0f);
		const RendererViewCamera camera = MakeTestCamera(viewportPixelSize);

		const std::optional<float> nearOrigin =
			WorldUnitsPerPixelAt(camera, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), viewportPixelSize);
		const std::optional<float> farFromOrigin =
			WorldUnitsPerPixelAt(camera, glm::vec3(5.0f, 0.0f, 5.0f), glm::vec3(1.0f, 0.0f, 0.0f), viewportPixelSize);
		ASSERT_TRUE(nearOrigin.has_value());
		ASSERT_TRUE(farFromOrigin.has_value());
		// Orthographic: no perspective foreshortening, so the ratio does not depend on where in the
		// scene it is measured - only a genuinely edge-on *direction* (see below) changes it.
		EXPECT_NEAR(*nearOrigin, *farFromOrigin, 1e-4f);
	}

	TEST(WorldUnitsPerPixelAtTests, ANearlyEdgeOnDirectionNeedsManyMoreWorldUnitsPerPixel)
	{
		const glm::vec2 viewportPixelSize(800.0f, 600.0f);
		const RendererViewCamera camera = MakeTestCamera(viewportPixelSize);

		// +x is roughly the screen's right axis for this camera (yaw~=0 looks down +y) - a normal,
		// well-conditioned direction to measure.
		const std::optional<float> acrossScreen =
			WorldUnitsPerPixelAt(camera, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), viewportPixelSize);
		// +y is almost exactly the view direction - the direction a plane's in-plane axis points
		// when that plane has turned edge-on to this camera.
		const std::optional<float> intoTheScreen =
			WorldUnitsPerPixelAt(camera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), viewportPixelSize);
		ASSERT_TRUE(acrossScreen.has_value());
		ASSERT_TRUE(intoTheScreen.has_value());
		EXPECT_GT(*intoTheScreen, *acrossScreen * 10.0f);
	}

	TEST(ScenePlaneBorderWidthTests, WithoutTheFloorAnEdgeOnPlanesBorderWouldBeSubPixel)
	{
		const glm::vec2 viewportPixelSize(800.0f, 600.0f);
		const RendererViewCamera camera = MakeTestCamera(viewportPixelSize);

		// bitangent = cross(normal, tangent) = (0, -1, 0) here - almost exactly this camera's view
		// axis, i.e. this plane is (almost) edge-on to it. halfExtents is deliberately small so the
		// pre-existing "0.02 * halfExtents" world-space proportion is tiny before any flooring.
		RendererWindowState::ScenePlane plane;
		plane.center = glm::vec3(0.0f);
		plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);
		plane.tangent = glm::vec3(0.0f, 0.0f, 1.0f);
		plane.halfExtents = glm::vec2(0.3f);

		const float plainWorldWidth = 0.02f * std::max(0.05f, std::max(plane.halfExtents.x, plane.halfExtents.y));
		const glm::vec3 bitangent = glm::cross(plane.normal, plane.tangent);
		const float plainScreenPixels =
			ScreenPixelsForWorldWidth(camera, plane.center, bitangent, plainWorldWidth, viewportPixelSize);
		EXPECT_LT(plainScreenPixels, 1.5f);
	}

	TEST(ScenePlaneBorderWidthTests, TheFlooredWidthReachesTheMinimumScreenSpaceWidth)
	{
		const glm::vec2 viewportPixelSize(800.0f, 600.0f);
		const RendererViewCamera camera = MakeTestCamera(viewportPixelSize);

		RendererWindowState::ScenePlane plane;
		plane.center = glm::vec3(0.0f);
		plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);
		plane.tangent = glm::vec3(0.0f, 0.0f, 1.0f);
		plane.halfExtents = glm::vec2(0.3f);

		const float width = ScenePlaneBorderWidth(plane, camera, viewportPixelSize);
		const glm::vec3 bitangent = glm::cross(plane.normal, plane.tangent);
		const float screenPixels = ScreenPixelsForWorldWidth(camera, plane.center, bitangent, width, viewportPixelSize);
		// A hair under the 1.5px floor to absorb the small-angle rounding MakeTestCamera's yaw
		// leaves in, not a change of what the floor is meant to guarantee.
		EXPECT_GE(screenPixels, 1.49f);

		// The axis that was NOT foreshortened (tangent, this camera's near-vertical screen axis)
		// must not have shrunk - only ever widened, per ScenePlaneBorderWidth's contract.
		EXPECT_GE(width, 0.02f * std::max(0.05f, std::max(plane.halfExtents.x, plane.halfExtents.y)));
	}

	TEST(ScenePlaneBorderWidthTests, AWellFramedPlaneKeepsItsPlainWorldSpaceWidth)
	{
		const glm::vec2 viewportPixelSize(800.0f, 600.0f);
		RendererViewCamera camera;
		camera.SetProjection(CameraProjection::Orthographic);
		camera.SetViewport(viewportPixelSize.x, viewportPixelSize.y);
		// Looking straight down at a plane that faces the viewer head-on: neither in-plane axis is
		// anywhere near the view direction, so the floor should not have anything to do.
		camera.SetOrbitState(glm::vec3(0.0f), 10.0f, 0.0f, 1.5f);

		RendererWindowState::ScenePlane plane;
		plane.center = glm::vec3(0.0f);
		plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
		plane.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		plane.halfExtents = glm::vec2(5.0f);

		const float width = ScenePlaneBorderWidth(plane, camera, viewportPixelSize);
		EXPECT_NEAR(width, 0.02f * plane.halfExtents.x, 1e-4f);
	}
} // namespace DefectStudio::Tests
