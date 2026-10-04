#include "Core/dspch.hpp"

#include <cmath>
#include <gtest/gtest.h>

#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneCurvedArrowTests, DefectZAxisGivesAnExact120DegreeCircleWithBoundEnds)
	{
		RendererWindowState window;
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->origin = {1, 2, 3};
		window.structure.defectFrame->z = {0, 0, 1};
		window.structure.atoms = {{"C", {3, 2, 3}}, {"N", {0, 2 + std::sqrt(3.0f), 3}}};
		window.selectedAtomIndices = {0, 1};
		const float buffer = GetScenePathAtomBuffer();
		GetScenePathAtomBuffer() = 0;
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		GetScenePathAtomBuffer() = buffer;
		ASSERT_TRUE(added);
		const auto &path = *window.paths->Store().Find(*added);
		EXPECT_EQ(window.selectedScenePaths, std::vector<SceneObjectId>{*added});
		EXPECT_EQ(path.style.startDecoration.kind, PathDecorationKind::None);
		EXPECT_EQ(path.style.endDecoration.kind, PathDecorationKind::Arrow);
		EXPECT_EQ(path.style.depthMode, PathDepthMode::DepthTest);
		ASSERT_TRUE(std::holds_alternative<CircularArcSegmentData>(path.segments.front().data));
		for (const auto &node : path.nodes)
		{
			ASSERT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(node.binding.value));
			EXPECT_FLOAT_EQ(std::get<PathBinding::CopyPosition>(node.binding.value).buffer, 0.0f);
		}
		// No radius callback: test the authored circle independently of the requested atom clearance.
		const auto sample = EvaluateSegment(path, ResolveNodePositions(path, {}), 0, 0.5);
		ASSERT_TRUE(sample);
		EXPECT_NEAR(glm::length(glm::dvec2(sample->position) - glm::dvec2(1, 2)), 2.0, 1.0e-5);
		EXPECT_NEAR(sample->position.z, 3.0, 1.0e-5);
		EXPECT_NEAR(sample->position.x, 2.0, 1.0e-5);
		EXPECT_NEAR(sample->position.y, 2 + std::sqrt(3.0), 1.0e-5);
		window.structure.atoms[1].cartesianPosition += glm::vec3(0, 0, 1);
		const auto moved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
		EXPECT_GT(moved.positions.back().z, 3.5f);
	}

	TEST(SceneCurvedArrowTests, SelectedVacancySetsTheAxisForTwoAtoms)
	{
		RendererWindowState window;
		window.structure.atoms = {{"C", {2, 0, 0}}, {"N", {-1, std::sqrt(3.0f), 0}}};
		window.structure.vacancies.push_back({{0, 0, 0}});
		window.selectedAtomIndices = {0, 1};
		window.selectedVacancies = {0};
		const float buffer = GetScenePathAtomBuffer();
		GetScenePathAtomBuffer() = 0;
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		GetScenePathAtomBuffer() = buffer;
		ASSERT_TRUE(added);
		const auto &path = *window.paths->Store().Find(*added);
		const auto arc = DeriveArc(path.nodes[0].position + path.transform.position,
			path.nodes[1].position + path.transform.position,
			std::get<CircularArcSegmentData>(path.segments[0].data).planeNormal,
			std::get<CircularArcSegmentData>(path.segments[0].data).signedSweepRadians);
		ASSERT_TRUE(arc);
		EXPECT_NEAR(glm::length(arc->center), 0.0, 1.0e-5);
	}

	TEST(SceneCurvedArrowTests, Buffered120DegreeArrowKeepsItsMeanRadiusAtTheMidpoint)
	{
		RendererWindowState window;
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->origin = {0, 0, 0};
		window.structure.defectFrame->z = {0, 0, 1};
		window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {-1, std::sqrt(3.0f), 0}}};
		window.structure.atoms[1].radius = 0.55f;
		window.selectedAtomIndices = {0, 1};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		const auto &path = *window.paths->Store().Find(*added);
		const auto resolved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
		const auto sample = EvaluateSegment(path, resolved, 0, 0.5);
		ASSERT_TRUE(sample);
		EXPECT_NEAR(glm::length(sample->position), 2.0, 1.0e-4);
		EXPECT_FLOAT_EQ(std::get<PathBinding::CopyPosition>(path.nodes[0].binding.value).buffer, GetScenePathAtomBuffer());
	}

	TEST(SceneCurvedArrowTests, UnequalRadiiUseCircularBezierControlsAndVacancyBindings)
	{
		RendererWindowState window;
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->origin = {0, 0, 0};
		window.structure.defectFrame->z = {0, 0, 1};
		window.structure.atoms = {{"C", {2, 0, 0}}};
		window.structure.vacancies.push_back({{0, 3, 0}});
		window.selectedAtomIndices = {0};
		window.selectedVacancies = {0};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		const auto &path = *window.paths->Store().Find(*added);
		EXPECT_TRUE(std::holds_alternative<CubicBezierSegmentData>(path.segments[0].data));
		EXPECT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(path.nodes.back().binding.value));
		EXPECT_TRUE(ValidatePath(path).empty());
	}

	TEST(SceneCurvedArrowTests, MissingSelectionAndDegenerateAxisDoNotAddObjects)
	{
		RendererWindowState window;
		EXPECT_FALSE(AddCurvedArrowThroughSelectedAtoms(window));
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->origin = {0, 0, 0};
		window.structure.defectFrame->z = {0, 0, 1};
		window.structure.atoms = {{"C", {0, 0, 0}}, {"N", {0, 0, 1}}};
		window.selectedAtomIndices = {0, 1};
		EXPECT_FALSE(AddCurvedArrowThroughSelectedAtoms(window));
		EXPECT_TRUE(!window.paths || window.paths->Store().Empty());
	}
}
