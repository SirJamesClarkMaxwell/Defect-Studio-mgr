#include "Core/dspch.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/Path/PathHandleGeometry.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr glm::vec2 kViewport{800.0f, 600.0f};

		// Camera on +Z looking at the origin with +Y up, so world +X is exactly the camera's right.
		[[nodiscard]] glm::mat4 ViewProjection(const float distance = 5.0f)
		{
			return glm::perspective(glm::radians(45.0f), kViewport.x / kViewport.y, 0.1f, 100.0f) *
				glm::lookAt(glm::vec3(0.0f, 0.0f, distance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}

		[[nodiscard]] PathNode Node(ScenePath &path, const glm::vec3 &position)
		{
			PathNode node;
			node.id = AllocateElementId(path);
			node.position = position;
			return node;
		}

		[[nodiscard]] PathSegment LineSegment(ScenePath &path)
		{
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			return segment;
		}

		[[nodiscard]] PathSegment ArcSegment(ScenePath &path)
		{
			PathSegment segment;
			segment.id = AllocateElementId(path);
			CircularArcSegmentData arc;
			arc.signedSweepRadians = 1.0f;
			segment.data = arc;
			return segment;
		}

		[[nodiscard]] PathSegment CubicSegment(ScenePath &path, const glm::vec3 &start, const glm::vec3 &end)
		{
			PathSegment segment;
			segment.id = AllocateElementId(path);
			CubicBezierSegmentData cubic;
			cubic.startHandle.id = AllocateElementId(path);
			cubic.startHandle.position = start;
			cubic.endHandle.id = AllocateElementId(path);
			cubic.endHandle.position = end;
			segment.data = cubic;
			return segment;
		}

		// Three nodes across the origin: a Line then a Cubic.
		[[nodiscard]] ScenePath LineThenCubic()
		{
			ScenePath path;
			path.nodes = {Node(path, {-1.0f, 0.0f, 0.0f}), Node(path, {0.0f, 0.0f, 0.0f}), Node(path, {1.0f, 0.0f, 0.0f})};
			path.segments = {LineSegment(path), CubicSegment(path, {0.3f, 0.5f, 0.0f}, {0.7f, 0.5f, 0.0f})};
			return path;
		}

		[[nodiscard]] ResolvedNodes Resolved(const ScenePath &path)
		{
			ResolvedNodes resolved;
			for (const PathNode &node : path.nodes)
				resolved.positions.push_back(node.position);
			return resolved;
		}

		[[nodiscard]] std::size_t CountKind(const std::vector<PathHandleMarker> &markers, const PathMarkerKind kind)
		{
			std::size_t count = 0;
			for (const PathHandleMarker &marker : markers)
				count += marker.kind == kind ? 1u : 0u;
			return count;
		}
	} // namespace

	// Criterion 1.
	TEST(PathHandleGeometryTests, NodesThenCubicHandlesInOrder)
	{
		const ScenePath path = LineThenCubic();
		const std::vector<PathHandleMarker> markers =
			BuildPathHandleMarkers(path, Resolved(path), ViewProjection(), kViewport, PathElementId{});

		ASSERT_EQ(markers.size(), 5u);
		for (std::size_t index = 0; index < 3; ++index)
		{
			EXPECT_EQ(markers[index].kind, PathMarkerKind::Node);
			EXPECT_EQ(markers[index].element, path.nodes[index].id);
		}
		const auto &cubic = std::get<CubicBezierSegmentData>(path.segments[1].data);
		EXPECT_EQ(markers[3].kind, PathMarkerKind::BezierHandle);
		EXPECT_EQ(markers[3].element, cubic.startHandle.id);
		EXPECT_EQ(markers[4].kind, PathMarkerKind::BezierHandle);
		EXPECT_EQ(markers[4].element, cubic.endHandle.id);
	}

	// Criterion 2.
	TEST(PathHandleGeometryTests, RigidSegmentsContributeNoHandles)
	{
		ScenePath path;
		path.nodes = {Node(path, {-1.0f, 0.0f, 0.0f}), Node(path, {0.0f, 0.5f, 0.0f}), Node(path, {1.0f, 0.0f, 0.0f})};
		path.segments = {LineSegment(path), ArcSegment(path)};

		const std::vector<PathHandleMarker> markers =
			BuildPathHandleMarkers(path, Resolved(path), ViewProjection(), kViewport, PathElementId{});

		EXPECT_EQ(markers.size(), 3u);
		EXPECT_EQ(CountKind(markers, PathMarkerKind::BezierHandle), 0u);
	}

	// Criterion 3.
	TEST(PathHandleGeometryTests, HandleOwnerIsTheNodeItHangsOff)
	{
		const ScenePath path = LineThenCubic();
		const std::vector<PathHandleMarker> markers =
			BuildPathHandleMarkers(path, Resolved(path), ViewProjection(), kViewport, PathElementId{});

		ASSERT_EQ(markers.size(), 5u);
		for (std::size_t index = 0; index < 3; ++index)
			EXPECT_EQ(markers[index].owner, markers[index].element);
		// Segment 1 spans nodes 1 and 2.
		EXPECT_EQ(markers[3].owner, path.nodes[1].id);
		EXPECT_EQ(markers[4].owner, path.nodes[2].id);
	}

	// Criterion 4.
	TEST(PathHandleGeometryTests, MarkerFollowsTheResolvedPositionNotTheAuthoredOne)
	{
		const ScenePath path = LineThenCubic();
		ResolvedNodes resolved = Resolved(path);
		resolved.positions[0] = glm::vec3(-1.0f, 1.5f, 0.0f);

		const std::vector<PathHandleMarker> markers =
			BuildPathHandleMarkers(path, resolved, ViewProjection(), kViewport, PathElementId{});

		ASSERT_FALSE(markers.empty());
		EXPECT_FLOAT_EQ(markers[0].worldPosition.y, 1.5f);
		EXPECT_NE(markers[0].worldPosition.y, path.nodes[0].position.y);
	}

	// Criterion 5.
	TEST(PathHandleGeometryTests, ElementBehindTheCameraIsOmitted)
	{
		ScenePath path = LineThenCubic();
		// The camera sits at z == 5 looking towards -Z, so z == 20 is behind it.
		path.nodes[0].position = glm::vec3(-1.0f, 0.0f, 20.0f);

		const std::vector<PathHandleMarker> markers =
			BuildPathHandleMarkers(path, Resolved(path), ViewProjection(), kViewport, PathElementId{});

		EXPECT_EQ(markers.size(), 4u);
		for (const PathHandleMarker &marker : markers)
			EXPECT_NE(marker.element, path.nodes[0].id);
	}

	// Criterion 6.
	TEST(PathHandleGeometryTests, ActiveElementGetsTheEnlargedRadii)
	{
		const ScenePath path = LineThenCubic();
		const PathElementId active = path.nodes[1].id;
		const std::vector<PathHandleMarker> markers =
			BuildPathHandleMarkers(path, Resolved(path), ViewProjection(), kViewport, active);

		ASSERT_EQ(markers.size(), 5u);
		for (const PathHandleMarker &marker : markers)
		{
			const bool isActive = marker.element == active;
			EXPECT_FLOAT_EQ(marker.drawRadius, isActive ? kPathActiveHandleDrawRadius : kPathHandleDrawRadius);
			EXPECT_FLOAT_EQ(marker.pickRadius, isActive ? kPathActiveHandlePickRadius : kPathHandlePickRadius);
		}
	}

	// Criterion 6, second half.
	TEST(PathHandleGeometryTests, UnsetActiveElementEnlargesNothing)
	{
		const ScenePath path = LineThenCubic();
		const std::vector<PathHandleMarker> markers =
			BuildPathHandleMarkers(path, Resolved(path), ViewProjection(), kViewport, PathElementId{});

		for (const PathHandleMarker &marker : markers)
			EXPECT_FLOAT_EQ(marker.pickRadius, kPathHandlePickRadius);
	}

	// Criterion 7.
	TEST(PathHandleGeometryTests, MalformedInputYieldsNoMarkers)
	{
		ScenePath path = LineThenCubic();
		const glm::mat4 viewProjection = ViewProjection();

		EXPECT_TRUE(BuildPathHandleMarkers(path, Resolved(path), viewProjection, glm::vec2(0.0f), PathElementId{}).empty());
		EXPECT_TRUE(
			BuildPathHandleMarkers(path, Resolved(path), viewProjection, glm::vec2(-8.0f, 6.0f), PathElementId{}).empty());

		glm::mat4 broken = viewProjection;
		broken[2][2] = std::numeric_limits<float>::quiet_NaN();
		EXPECT_TRUE(BuildPathHandleMarkers(path, Resolved(path), broken, kViewport, PathElementId{}).empty());

		path.nodes[1].position.y = std::numeric_limits<float>::infinity();
		EXPECT_TRUE(BuildPathHandleMarkers(path, Resolved(path), viewProjection, kViewport, PathElementId{}).empty());
	}

	// Criterion 8.
	TEST(PathHandleGeometryTests, WorldRadiusProjectsToGrowingPixelRadius)
	{
		const glm::mat4 viewProjection = ViewProjection();
		const glm::vec3 right(1.0f, 0.0f, 0.0f);

		const std::optional<float> distant =
			ProjectWorldRadiusToPixels(viewProjection, kViewport, glm::vec3(0.0f), right, 0.25f);
		const std::optional<float> close = ProjectWorldRadiusToPixels(
			viewProjection, kViewport, glm::vec3(0.0f, 0.0f, 3.0f), right, 0.25f);
		ASSERT_TRUE(distant.has_value());
		ASSERT_TRUE(close.has_value());
		EXPECT_GT(*distant, 0.0f);
		EXPECT_TRUE(std::isfinite(*distant));
		EXPECT_GT(*close, *distant);

		EXPECT_FALSE(ProjectWorldRadiusToPixels(viewProjection, kViewport, glm::vec3(0.0f, 0.0f, 20.0f), right, 0.25f)
						 .has_value());
		EXPECT_FALSE(ProjectWorldRadiusToPixels(viewProjection, kViewport, glm::vec3(0.0f), right,
			std::numeric_limits<float>::quiet_NaN())
						 .has_value());
		EXPECT_FALSE(
			ProjectWorldRadiusToPixels(viewProjection, glm::vec2(0.0f), glm::vec3(0.0f), right, 0.25f).has_value());
	}
}
