#include "Core/dspch.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/Path/PathHandleGeometry.hpp"
#include "Renderer/Path/PathPicking.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr glm::vec2 kViewport{800.0f, 600.0f};
		constexpr float kStrokeWidth = 0.2f;

		// Camera on +Z looking at the origin with +Y up: world +X is the camera's right, and the
		// pixels-per-world scale is the same horizontally and vertically, so an offset measured with
		// ProjectWorldRadiusToPixels along X can be applied to the cursor along screen Y.
		[[nodiscard]] glm::mat4 ViewProjection()
		{
			return glm::perspective(glm::radians(45.0f), kViewport.x / kViewport.y, 0.1f, 100.0f) *
				glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}

		[[nodiscard]] PathPickSettings Settings(const glm::vec2 cursor, const bool editMode = true)
		{
			PathPickSettings settings;
			settings.viewProjection = ViewProjection();
			settings.viewportSize = kViewport;
			settings.cursor = cursor;
			settings.cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
			settings.editMode = editMode;
			return settings;
		}

		[[nodiscard]] glm::vec2 Screen(const glm::vec3 &world)
		{
			const std::optional<glm::vec2> point =
				SelectionHitTest::ProjectToScreen(ViewProjection(), kViewport, world);
			EXPECT_TRUE(point.has_value());
			return point.value_or(glm::vec2(0.0f));
		}

		[[nodiscard]] float HalfWidthPixels(const glm::vec3 &world, const float worldHalfWidth)
		{
			const std::optional<float> radius = ProjectWorldRadiusToPixels(
				ViewProjection(), kViewport, world, glm::vec3(1.0f, 0.0f, 0.0f), worldHalfWidth);
			EXPECT_TRUE(radius.has_value());
			return radius.value_or(0.0f);
		}

		[[nodiscard]] PathNode Node(ScenePath &path, const glm::vec3 &position)
		{
			PathNode node;
			node.id = AllocateElementId(path);
			node.position = position;
			return node;
		}

		// A straight stroke from (-1,0,0) to (1,0,0), two nodes, one Line segment, no decorations.
		[[nodiscard]] ScenePath StraightPath()
		{
			ScenePath path;
				path.nodes = {Node(path, {-1.0f, 0.0f, 0.0f}), Node(path, {1.0f, 0.0f, 0.0f})};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments.push_back(segment);
			path.style.width = kStrokeWidth;
			return path;
		}

		[[nodiscard]] ResolvedNodes Resolved(const ScenePath &path)
		{
			ResolvedNodes resolved;
			for (const PathNode &node : path.nodes)
				resolved.positions.push_back(node.position);
			for (std::size_t index = 0; index < path.segments.size(); ++index)
			{
				const auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[index].data);
				if (cubic == nullptr)
				{
					resolved.handlePositions.push_back(glm::vec3(0.0f));
					resolved.handlePositions.push_back(glm::vec3(0.0f));
					continue;
				}
				resolved.handlePositions.push_back(path.nodes[index].position + cubic->startHandle.offset);
				resolved.handlePositions.push_back(path.nodes[index + 1].position + cubic->endHandle.offset);
			}
			return resolved;
		}

		[[nodiscard]] EvaluatedSample Sample(const glm::dvec3 &position, const double arcLength, const double total,
			const PathElementId segment)
		{
			EvaluatedSample sample;
			sample.position = position;
			sample.tangent = glm::dvec3(1.0, 0.0, 0.0);
			sample.normal = glm::dvec3(0.0, 1.0, 0.0);
			sample.binormal = glm::dvec3(0.0, 0.0, 1.0);
			sample.segment = segment;
			sample.arcLength = arcLength;
			sample.normalizedT = total > 0.0 ? arcLength / total : 0.0;
			return sample;
		}

		// Matches StraightPath: the two endpoints, 2 units apart.
		[[nodiscard]] EvaluatedPath StraightEvaluated(const ScenePath &path)
		{
			EvaluatedPath evaluated;
			evaluated.totalLength = 2.0;
			const PathElementId segment = path.segments.front().id;
			evaluated.samples = {Sample({-1.0, 0.0, 0.0}, 0.0, 2.0, segment), Sample({1.0, 0.0, 0.0}, 2.0, 2.0, segment)};
			return evaluated;
		}

		[[nodiscard]] PathPickResult Pick(
			const ScenePath &path, const EvaluatedPath &evaluated, const PathPickSettings &settings)
		{
			return PickPath(path, Resolved(path), evaluated, settings);
		}
	} // namespace

	// Criterion 9.
	TEST(PathPickingTests, HiddenPathHasNoHitbox)
	{
		ScenePath path = StraightPath();
		const EvaluatedPath evaluated = StraightEvaluated(path);
		const PathPickSettings settings = Settings(Screen(glm::vec3(0.0f)));
		ASSERT_TRUE(Pick(path, evaluated, settings).Hit());

		path.visible = false;
		EXPECT_EQ(Pick(path, evaluated, settings).kind, PathPickKind::None);

		path.visible = true;
		path.renderable = false;
		EXPECT_EQ(Pick(path, evaluated, settings).kind, PathPickKind::None);
	}

	// Criterion 10.
	TEST(PathPickingTests, StrokePicksExactlyAsWideAsItDraws)
	{
		const ScenePath path = StraightPath();
		const EvaluatedPath evaluated = StraightEvaluated(path);
		const glm::vec2 centre = Screen(glm::vec3(0.0f));
		const float halfWidth = HalfWidthPixels(glm::vec3(-1.0f, 0.0f, 0.0f), 0.5f * kStrokeWidth);

		const PathPickResult onCentreline = Pick(path, evaluated, Settings(centre));
		EXPECT_EQ(onCentreline.kind, PathPickKind::Segment);
		EXPECT_FLOAT_EQ(onCentreline.screenDistance, 0.0f);

		const float inside = halfWidth + kPathStrokePickTolerance - 1.0f;
		EXPECT_EQ(Pick(path, evaluated, Settings(centre + glm::vec2(0.0f, inside))).kind, PathPickKind::Segment);

		const float outside = halfWidth + kPathStrokePickTolerance + 1.0f;
		EXPECT_EQ(Pick(path, evaluated, Settings(centre + glm::vec2(0.0f, outside))).kind, PathPickKind::None);
	}

	// Criterion 11.
	TEST(PathPickingTests, CursorPastTheCapMissesTheStroke)
	{
		const ScenePath path = StraightPath();
		const EvaluatedPath evaluated = StraightEvaluated(path);
		// Half a world unit past the end node, which is many times the 0.1 half-width.
		EXPECT_EQ(Pick(path, evaluated, Settings(Screen(glm::vec3(1.5f, 0.0f, 0.0f)))).kind, PathPickKind::None);
	}

	// Criterion 12, node half.
	TEST(PathPickingTests, EditModePicksTheNodeUnderTheCursor)
	{
		const ScenePath path = StraightPath();
		const PathPickResult result =
			Pick(path, StraightEvaluated(path), Settings(Screen(path.nodes.back().position)));

		EXPECT_EQ(result.kind, PathPickKind::Node);
		EXPECT_EQ(result.element, path.nodes.back().id);
		EXPECT_FLOAT_EQ(result.screenDistance, 0.0f);
	}

	// Criteria 12 and 13: a handle coincident with a node beats it, the node beats the stroke, and
	// with neither in reach the stroke is what is left.
	TEST(PathPickingTests, ArbitrationRunsHandleThenNodeThenSegment)
	{
		ScenePath path;
		path.nodes = {Node(path, {-1.0f, 0.0f, 0.0f}), Node(path, {1.0f, 0.0f, 0.0f})};
		PathSegment segment;
		segment.id = AllocateElementId(path);
		CubicBezierSegmentData cubic;
		cubic.startHandle.id = AllocateElementId(path);
		cubic.startHandle.offset = glm::vec3(0.0f); // deliberately on top of the node
		cubic.endHandle.id = AllocateElementId(path);
		cubic.endHandle.offset = glm::vec3(-0.4f, 0.0f, 0.0f);
		segment.data = cubic;
		path.segments.push_back(segment);
		path.style.width = kStrokeWidth;

		EvaluatedPath evaluated;
		evaluated.totalLength = 2.0;
		evaluated.samples = {Sample({-1.0, 0.0, 0.0}, 0.0, 2.0, segment.id), Sample({1.0, 0.0, 0.0}, 2.0, 2.0, segment.id)};

		const PathPickSettings atFirstNode = Settings(Screen(path.nodes[0].position));
		const PathPickResult handle = Pick(path, evaluated, atFirstNode);
		EXPECT_EQ(handle.kind, PathPickKind::Handle);
		EXPECT_EQ(handle.element, cubic.startHandle.id);

		path.segments[0].data = LineSegmentData{};
		const PathPickResult node = Pick(path, evaluated, atFirstNode);
		EXPECT_EQ(node.kind, PathPickKind::Node);
		EXPECT_EQ(node.element, path.nodes[0].id);

		// The midpoint is a full world unit from either node - far outside the 20px node radius.
		const PathPickResult stroke = Pick(path, evaluated, Settings(Screen(glm::vec3(0.0f))));
		EXPECT_EQ(stroke.kind, PathPickKind::Segment);
		EXPECT_EQ(stroke.element, segment.id);
	}

	// Criterion 14.
	TEST(PathPickingTests, DecorationBeatsTheShaftItSitsOn)
	{
		ScenePath path = StraightPath();
		path.style.endDecoration.kind = PathDecorationKind::Arrow;
		path.style.endDecoration.lengthScale = 4.0f;
		path.style.endDecoration.widthScale = 6.0f;
		const EvaluatedPath evaluated = StraightEvaluated(path);

		// Inside the arrowhead but well outside the end node's 20px radius, where the shaft's
		// tolerance also reaches.
		const PathPickResult result = Pick(path, evaluated, Settings(Screen(glm::vec3(0.6f, 0.0f, 0.0f)), false));
		EXPECT_EQ(result.kind, PathPickKind::WholePath);

		const PathPickResult edit = Pick(path, evaluated, Settings(Screen(glm::vec3(0.6f, 0.0f, 0.0f))));
		EXPECT_EQ(edit.kind, PathPickKind::Decoration);
		EXPECT_EQ(edit.element, path.nodes.back().id);

		// Off-axis by more than the shaft's half-width but inside the fat arrowhead.
		const float wide = HalfWidthPixels(glm::vec3(1.0f, 0.0f, 0.0f), 0.5f * kStrokeWidth * 4.0f);
		const PathPickResult offAxis =
			Pick(path, evaluated, Settings(Screen(glm::vec3(1.0f, 0.0f, 0.0f)) + glm::vec2(0.0f, wide)));
		EXPECT_EQ(offAxis.kind, PathPickKind::Decoration);
	}

	// Criterion 15.
	TEST(PathPickingTests, NoneDecorationIsNotACandidate)
	{
		ScenePath path = StraightPath();
		path.style.endDecoration.kind = PathDecorationKind::None;

		const PathPickResult result =
			Pick(path, StraightEvaluated(path), Settings(Screen(glm::vec3(0.6f, 0.0f, 0.0f))));
		EXPECT_EQ(result.kind, PathPickKind::Segment);
	}

	// Criterion 16.
	TEST(PathPickingTests, ObjectModeCollapsesEveryHitToTheWholePath)
	{
		const ScenePath path = StraightPath();
		const EvaluatedPath evaluated = StraightEvaluated(path);

		const PathPickResult onNode = Pick(path, evaluated, Settings(Screen(path.nodes.back().position), false));
		EXPECT_EQ(onNode.kind, PathPickKind::WholePath);
		EXPECT_FALSE(onNode.element.IsValid());

		const PathPickResult onStroke = Pick(path, evaluated, Settings(Screen(glm::vec3(0.0f)), false));
		EXPECT_EQ(onStroke.kind, PathPickKind::WholePath);
		EXPECT_FALSE(onStroke.element.IsValid());
	}

	// Criterion 17.
	TEST(PathPickingTests, HandleBehindTheCameraIsNotPickable)
	{
		ScenePath path;
		path.nodes = {Node(path, {-1.0f, 0.0f, 0.0f}), Node(path, {1.0f, 0.0f, 0.0f})};
		PathSegment segment;
		segment.id = AllocateElementId(path);
		CubicBezierSegmentData cubic;
		cubic.startHandle.id = AllocateElementId(path);
		cubic.startHandle.offset = glm::vec3(0.5f, 0.0f, 20.0f); // authored world point is behind the camera at z == 5
		cubic.endHandle.id = AllocateElementId(path);
		cubic.endHandle.offset = glm::vec3(-0.5f, 0.0f, 20.0f);
		segment.data = cubic;
		path.segments.push_back(segment);
		path.style.width = kStrokeWidth;

		EvaluatedPath evaluated;
		evaluated.totalLength = 2.0;
		evaluated.samples = {Sample({-1.0, 0.0, 0.0}, 0.0, 2.0, segment.id), Sample({1.0, 0.0, 0.0}, 2.0, 2.0, segment.id)};

		const PathPickResult result = Pick(path, evaluated, Settings(Screen(glm::vec3(0.0f))));
		EXPECT_EQ(result.kind, PathPickKind::Segment);
	}

	// Criterion 18.
	TEST(PathPickingTests, ActiveElementWidensOnlyItsOwnRadius)
	{
		const ScenePath path = StraightPath();
		const EvaluatedPath evaluated = StraightEvaluated(path);
		// 23px is inside the active radius (26) and outside the plain one (20), and far enough off the
		// 0.1-wide stroke that the segment cannot answer instead.
		const glm::vec2 offset(0.0f, 23.0f);

		PathPickSettings active = Settings(Screen(path.nodes.front().position) + offset);
		active.activeElement = path.nodes.front().id;
		const PathPickResult hit = Pick(path, evaluated, active);
		EXPECT_EQ(hit.kind, PathPickKind::Node);
		EXPECT_EQ(hit.element, path.nodes.front().id);

		const PathPickResult miss = Pick(path, evaluated, Settings(Screen(path.nodes.front().position) + offset));
		EXPECT_EQ(miss.kind, PathPickKind::None);
	}

	// Criterion 19.
	TEST(PathPickingTests, NodesAndDecorationsStillPickWithoutTessellation)
	{
		ScenePath path = StraightPath();
		path.style.startDecoration.kind = PathDecorationKind::Arrow;
		path.style.startDecoration.lengthScale = 4.0f;
		path.style.startDecoration.widthScale = 6.0f;
		const EvaluatedPath empty;

		EXPECT_EQ(Pick(path, empty, Settings(Screen(path.nodes.back().position))).kind, PathPickKind::Node);
		EXPECT_EQ(Pick(path, empty, Settings(Screen(glm::vec3(-0.7f, 0.0f, 0.0f)))).kind, PathPickKind::Decoration);
		EXPECT_EQ(Pick(path, empty, Settings(Screen(glm::vec3(0.0f, 2.0f, 0.0f)))).kind, PathPickKind::None);
	}

	// Criterion 20.
	TEST(PathPickingTests, DashGapsStillPick)
	{
		ScenePath path = StraightPath();
		path.style.dash.enabled = true;
		path.style.dash.dashLength = 0.1f;
		path.style.dash.gapLength = 0.1f;
		// Arc length 1.0 falls in a gap of the 0.1/0.1 pattern starting at 0.
		EXPECT_EQ(Pick(path, StraightEvaluated(path), Settings(Screen(glm::vec3(0.0f)))).kind, PathPickKind::Segment);
	}

	// Criterion 21.
	TEST(PathPickingTests, MalformedSettingsPickNothing)
	{
		const ScenePath path = StraightPath();
		const EvaluatedPath evaluated = StraightEvaluated(path);
		const glm::vec2 centre = Screen(glm::vec3(0.0f));
		constexpr float nan = std::numeric_limits<float>::quiet_NaN();

		PathPickSettings badCursor = Settings(glm::vec2(nan, centre.y));
		PathPickSettings badViewport = Settings(centre);
		badViewport.viewportSize = glm::vec2(0.0f);
		PathPickSettings badRight = Settings(centre);
		badRight.cameraRight = glm::vec3(0.0f);
		PathPickSettings badMatrix = Settings(centre);
		badMatrix.viewProjection[1][1] = nan;

		for (const PathPickSettings &settings : {badCursor, badViewport, badRight, badMatrix})
		{
			const PathPickResult result = Pick(path, evaluated, settings);
			EXPECT_EQ(result.kind, PathPickKind::None);
			EXPECT_FLOAT_EQ(result.screenDistance, 0.0f);
		}
	}

	// Criterion 22.
	TEST(PathPickingTests, SegmentHitReportsAPointOnThePolyline)
	{
		const ScenePath path = StraightPath();
		const EvaluatedPath evaluated = StraightEvaluated(path);

		const PathPickResult result = Pick(path, evaluated, Settings(Screen(glm::vec3(0.4f, 0.0f, 0.0f))));
		ASSERT_EQ(result.kind, PathPickKind::Segment);
		EXPECT_NEAR(result.worldPosition.x, 0.4f, 1e-3f);
		EXPECT_NEAR(result.worldPosition.y, 0.0f, 1e-5f);
		EXPECT_NEAR(result.worldPosition.z, 0.0f, 1e-5f);
	}
}
