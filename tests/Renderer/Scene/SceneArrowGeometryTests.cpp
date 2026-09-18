#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <tuple>

#include "Renderer/Scene/SceneArrowGeometry.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneArrowTipParametersTests, EveryTipHasDistinctParametersAndNoneBuildsNoGeometry)
	{
		using Tip = RendererWindowState::ArrowTip;
		const std::array tips = {Tip::None, Tip::Plain, Tip::Barbed, Tip::Open, Tip::Bar, Tip::Circle};
		std::set<std::tuple<float, float, bool, bool>> distinct;
		for (const Tip tip : tips)
		{
			const ArrowTipParameters parameters = GetArrowTipParameters(tip);
			distinct.emplace(
				parameters.lengthScale, parameters.widthScale, parameters.filled, parameters.closesBack);
		}

		EXPECT_EQ(distinct.size(), tips.size());
		EXPECT_FALSE(GetArrowTipParameters(Tip::None).producesGeometry());
		for (std::size_t index = 1; index < tips.size(); ++index)
			EXPECT_TRUE(GetArrowTipParameters(tips[index]).producesGeometry());

		RendererWindowState::SceneArrow arrow;
		arrow.style.shaftWidth = 0.0f;
		arrow.startTip = Tip::None;
		arrow.endTip = Tip::None;
		EXPECT_TRUE(BuildSceneArrowMesh(arrow, 12u).positions.empty());
		arrow.endTip = Tip::Plain;
		EXPECT_FALSE(BuildSceneArrowMesh(arrow, 12u).positions.empty());
	}

	TEST(SceneArrowGeometryTests, CurvedMeshFollowsControlPointAndSegmentsOnlyChangeTessellation)
	{
		RendererWindowState::SceneArrow arrow;
		arrow.points = {glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f)};
		arrow.controlPoint = glm::vec3(1.0f, 2.0f, 0.0f);
		arrow.startTip = RendererWindowState::ArrowTip::None;
		arrow.endTip = RendererWindowState::ArrowTip::None;
		arrow.style.shaftWidth = 0.1f;
		arrow.curveSegments = 4;

		const SceneArrowMeshData coarse = BuildSceneArrowMesh(arrow, 12u);
		ASSERT_EQ(coarse.path.points.size(), 5u);
		EXPECT_EQ(coarse.path.points.front(), arrow.points.front());
		EXPECT_EQ(coarse.path.points.back(), arrow.points.back());
		EXPECT_NEAR(coarse.path.points[2].x, 1.0f, 1e-5f);
		EXPECT_NEAR(coarse.path.points[2].y, 1.0f, 1e-5f);

		arrow.curveSegments = 16;
		const SceneArrowMeshData fine = BuildSceneArrowMesh(arrow, 12u);
		EXPECT_GT(fine.positions.size(), coarse.positions.size());
		EXPECT_EQ(fine.path.points.front(), coarse.path.points.front());
		EXPECT_EQ(fine.path.points.back(), coarse.path.points.back());
		EXPECT_NEAR(fine.path.points[8].x, 1.0f, 1e-5f);
		EXPECT_NEAR(fine.path.points[8].y, 1.0f, 1e-5f);
	}

	TEST(SceneArrowGeometryTests, DashedPathContinuesItsPatternAcrossAJoint)
	{
		SceneArrowPath path;
		path.points = {
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.8f, 0.0f, 0.0f),
			glm::vec3(1.6f, 0.0f, 0.0f)};
		path.cumulativeLengths = {0.0f, 0.8f, 1.6f};
		path.totalLength = 1.6f;

		const std::vector<SceneArrowShaftSpan> spans =
			BuildSceneArrowShaftSpans(path, true, 0.5f, 0.25f, 0.0f, 0.0f);
		const auto afterJoint = std::find_if(spans.begin(), spans.end(), [](const SceneArrowShaftSpan &span) {
			return std::abs(span.startDistance - 0.8f) < 1e-5f;
		});
		ASSERT_NE(afterJoint, spans.end());
		EXPECT_NEAR(afterJoint->endDistance, 1.25f, 1e-5f);
		EXPECT_NEAR(glm::distance(afterJoint->start, afterJoint->end), 0.45f, 1e-5f);
		EXPECT_NEAR(afterJoint->endDistance - afterJoint->startDistance, 0.45f, 1e-5f);
	}
} // namespace DefectStudio::Tests
