#include "Core/dspch.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		void PrepareTriangle(RendererWindowState &window)
		{
			window.structure.atoms = {
				{"C", {2, 0, 0}}, {"C", {-1, std::sqrt(3.0f), 0}}, {"C", {-1, -std::sqrt(3.0f), 0}}};
			window.selectedAtomIndices = {2, 0, 1};
		}

		void ExpectPositiveTriangle(const RendererWindowState &window, const std::vector<SceneObjectId> &ids)
		{
			ASSERT_EQ(ids.size(), 3u);
			EXPECT_EQ(window.selectedScenePaths, ids);
			EXPECT_TRUE(window.selectedAtomIndices.empty());
			EXPECT_TRUE(window.selectedVacancies.empty());
			bool seen[3] = {};
			for (const auto id : ids)
			{
				const auto &path = *window.paths->Store().Find(id);
				ASSERT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(path.nodes.front().binding.value));
				ASSERT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(path.nodes.back().binding.value));
				const auto start = std::get<PathBinding::CopyPosition>(path.nodes.front().binding.value).atomIndex;
				const auto end = std::get<PathBinding::CopyPosition>(path.nodes.back().binding.value).atomIndex;
				ASSERT_LT(start, 3u);
				EXPECT_FALSE(seen[start]);
				seen[start] = true;
				EXPECT_EQ(end, (start + 1) % 3);
				EXPECT_GT(std::get<CircularArcSegmentData>(path.segments.front().data).signedSweepRadians, 0.0f);
				EXPECT_TRUE(ValidatePath(path).empty());
			}
		}
	}

	TEST(SceneCurvedArrowTests, DefectZAxisGivesAShallowOutward120DegreeStepWithBoundEnds)
	{
		RendererWindowState window;
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->origin = {1, 2, 3};
		window.structure.defectFrame->z = {0, 0, 1};
		window.structure.atoms = {{"C", {3, 2, 3}}, {"N", {0, 2 + std::sqrt(3.0f), 3}}};
		window.selectedAtomIndices = {0, 1};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 1u);
		const auto &path = *window.paths->Store().Find(added->front());
		EXPECT_EQ(window.selectedScenePaths, *added);
		EXPECT_EQ(path.style.startDecoration.kind, PathDecorationKind::None);
		EXPECT_EQ(path.style.endDecoration.kind, PathDecorationKind::Arrow);
		EXPECT_EQ(path.style.depthMode, PathDepthMode::DepthTest);
		EXPECT_FLOAT_EQ(path.style.width, 0.03f);
		EXPECT_FLOAT_EQ(path.style.endDecoration.lengthScale, 3.0f);
		EXPECT_FLOAT_EQ(path.style.endDecoration.widthScale, 1.0f);
		ASSERT_TRUE(std::holds_alternative<CircularArcSegmentData>(path.segments.front().data));
		EXPECT_NEAR(std::get<CircularArcSegmentData>(path.segments.front().data).signedSweepRadians,
			std::numbers::pi_v<float> / 3, 1.0e-5);
		for (const auto &node : path.nodes)
		{
			ASSERT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(node.binding.value));
			EXPECT_FLOAT_EQ(std::get<PathBinding::CopyPosition>(node.binding.value).buffer, GetScenePathAtomBuffer());
		}
		// No radius callback: measure flatness independently of surface clearance.
		const auto resolved = ResolveNodePositions(path, {});
		const auto sample = EvaluateSegment(path, resolved, 0, 0.5);
		ASSERT_TRUE(sample);
		const glm::dvec3 chordMidpoint = (glm::dvec3(resolved.positions[0]) + glm::dvec3(resolved.positions[1])) * 0.5;
		const double chordLength = glm::distance(glm::dvec3(resolved.positions[0]), glm::dvec3(resolved.positions[1]));
		const double sagitta = glm::distance(sample->position, chordMidpoint);
		const double expected = chordLength * 0.5 * std::tan(std::numbers::pi_v<double> / 12);
		EXPECT_NEAR(sagitta, expected, 1.0e-5);
		EXPECT_GT(sagitta / chordLength, 0.12);
		EXPECT_LT(sagitta / chordLength, 0.15);
		EXPECT_NEAR(glm::length(glm::dvec2(sample->position) - glm::dvec2(1, 2)), 1.0 + expected, 1.0e-5);
		EXPECT_LT(sagitta, 0.5); // Old orbit: sagitta = 1 at radius 2.
		EXPECT_NEAR(sample->position.z, 3.0, 1.0e-5);
		window.structure.atoms[1].cartesianPosition += glm::vec3(0, 0, 1);
		const auto moved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
		EXPECT_GT(moved.positions.back().z, 3.5f);
		EXPECT_TRUE(EvaluateSegment(path, moved, 0, 0.5));
	}

	TEST(SceneCurvedArrowTests, SelectedVacancySetsTheAxisForTwoAtoms)
	{
		RendererWindowState window;
		window.structure.atoms = {{"C", {2, 0, 0}}, {"N", {-1, std::sqrt(3.0f), 0}}};
		window.structure.vacancies.push_back({{0, 0, 0}});
		window.selectedAtomIndices = {0, 1};
		window.selectedVacancies = {0};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 1u);
		const auto &path = *window.paths->Store().Find(added->front());
		const auto &arc = std::get<CircularArcSegmentData>(path.segments[0].data);
		EXPECT_NEAR(arc.signedSweepRadians, std::numbers::pi_v<float> / 3, 1.0e-5);
		EXPECT_NEAR(arc.planeNormal.z, 1.0f, 1.0e-5);
	}

	TEST(SceneCurvedArrowTests, BufferedArrowRemainsShallowAndStartsAtAtomSurfaces)
	{
		RendererWindowState window;
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->origin = {0, 0, 0};
		window.structure.defectFrame->z = {0, 0, 1};
		window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {-1, std::sqrt(3.0f), 0}}};
		window.structure.atoms[0].radius = 0.35f;
		window.structure.atoms[1].radius = 0.55f;
		window.selectedAtomIndices = {0, 1};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		const auto &path = *window.paths->Store().Find(added->front());
		const auto resolved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
		const auto sample = EvaluateSegment(path, resolved, 0, 0.5);
		ASSERT_TRUE(sample);
		const glm::dvec3 chordMidpoint = (glm::dvec3(resolved.positions[0]) + glm::dvec3(resolved.positions[1])) * 0.5;
		const double chordLength = glm::distance(glm::dvec3(resolved.positions[0]), glm::dvec3(resolved.positions[1]));
		EXPECT_NEAR(glm::distance(sample->position, chordMidpoint), chordLength * 0.5 * std::tan(std::numbers::pi_v<double> / 12), 1.0e-5);
		EXPECT_GT(glm::length(sample->position), glm::length(chordMidpoint));
		for (std::size_t i = 0; i < 2; ++i)
			EXPECT_NEAR(glm::distance(resolved.positions[i], window.structure.atoms[i].cartesianPosition),
				window.structure.atoms[i].radius * GetScenePathAtomBuffer(), 1.0e-5);
	}

	TEST(SceneCurvedArrowTests, UnequalRadiiAndHeightsKeepVacancyBindings)
	{
		RendererWindowState window;
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->origin = {0, 0, 0};
		window.structure.defectFrame->z = {0, 0, 1};
		window.structure.atoms = {{"C", {2, 0, 0}}};
		window.structure.vacancies.push_back({{0, 3, 1}});
		window.selectedAtomIndices = {0};
		window.selectedVacancies = {0};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 1u);
		const auto &path = *window.paths->Store().Find(added->front());
		EXPECT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(path.nodes.back().binding.value));
		EXPECT_TRUE(ValidatePath(path).empty());
		EXPECT_TRUE(EvaluateSegment(path, ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window)), 0, 0.5));
	}

	TEST(SceneCurvedArrowTests, TwoAtomsKeepSelectionOrderAndNegativeRotationSense)
	{
		RendererWindowState window;
		PrepareTriangle(window);
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->z = {0, 0, 1};
		window.selectedAtomIndices = {1, 0};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 1u);
		const auto &path = *window.paths->Store().Find(added->front());
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(path.nodes[0].binding.value).atomIndex, 1u);
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(path.nodes[1].binding.value).atomIndex, 0u);
		EXPECT_NEAR(std::get<CircularArcSegmentData>(path.segments[0].data).signedSweepRadians,
			-std::numbers::pi_v<float> / 3, 1.0e-5);
	}

	TEST(SceneCurvedArrowTests, ShuffledAtomsUseBestFitPlaneWithOrWithoutVacancyAxis)
	{
		for (const bool vacancy : {false, true})
		{
			RendererWindowState window;
			PrepareTriangle(window);
			if (vacancy)
			{
				window.structure.vacancies.push_back({{0.3f, 0.1f, 0}});
				window.selectedVacancies = {0};
			}
			const auto added = AddCurvedArrowThroughSelectedAtoms(window);
			ASSERT_TRUE(added);
			ExpectPositiveTriangle(window, *added);
			if (vacancy)
			{
				// Off-centre vacancy changes the sweep; it must not silently use the centroid.
				bool changed = false;
				for (const auto id : *added)
					changed |= std::abs(std::get<CircularArcSegmentData>(window.paths->Store().Find(id)->segments[0].data).signedSweepRadians -
						std::numbers::pi_v<float> / 3) > 0.01f;
				EXPECT_TRUE(changed);
			}
		}
	}

	TEST(SceneCurvedArrowTests, BestFitPlaneSupportsAVerticalCycle)
	{
		RendererWindowState window;
		PrepareTriangle(window);
		// Move the triangle into the yz plane, whose deterministic best-fit normal is +x.
		for (auto &atom : window.structure.atoms)
			atom.cartesianPosition = {0, atom.cartesianPosition.x, atom.cartesianPosition.y};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		ExpectPositiveTriangle(window, *added);
		for (const auto id : *added)
			EXPECT_NEAR(std::get<CircularArcSegmentData>(window.paths->Store().Find(id)->segments[0].data).planeNormal.x,
				1.0f, 1.0e-5);
	}

	TEST(SceneCurvedArrowTests, FourAtomsFollowTheSignedDefectAxis)
	{
		RendererWindowState window;
		window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {0, 2, 0}}, {"C", {-2, 0, 0}}, {"C", {0, -2, 0}}};
		window.selectedAtomIndices = {2, 0, 3, 1};
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->z = {0, 0, -1};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 4u);
		for (const auto id : *added)
		{
			const auto &path = *window.paths->Store().Find(id);
			const auto start = std::get<PathBinding::CopyPosition>(path.nodes.front().binding.value).atomIndex;
			const auto end = std::get<PathBinding::CopyPosition>(path.nodes.back().binding.value).atomIndex;
			EXPECT_EQ(end, (start + 3) % 4);
			const auto &arc = std::get<CircularArcSegmentData>(path.segments[0].data);
			EXPECT_NEAR(arc.signedSweepRadians, std::numbers::pi_v<float> / 4, 1.0e-5);
			EXPECT_FLOAT_EQ(arc.planeNormal.z, -1.0f);
		}
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

	class SceneCurvedArrowUndoTests : public testing::Test
	{
	protected:
		void SetUp() override { renderer.BindUndoStack(undoStack); }
		void TearDown() override { renderer.OnDetach(); }
		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{[] { RendererStartupConfig config; config.loadDefaultScene = false; return config; }()};
	};

	TEST_F(SceneCurvedArrowUndoTests, ShuffledTriangleCreatesPositiveCycleInOneUndoStep)
	{
		RendererWindowState window;
		window.windowId = "curved-arrow-cycle";
		PrepareTriangle(window);
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->origin = {0, 0, 0};
		window.structure.defectFrame->z = {0, 0, 1};
		// Frame takes priority over a selected vacancy.
		window.structure.vacancies.push_back({{2, 0, 0}});
		window.selectedVacancies = {0};
		renderer.AddWindow(std::move(window));
		auto &live = renderer.GetWindows().front();
		const auto added = AddCurvedArrowThroughSelectedAtoms(live);
		ASSERT_TRUE(added);
		ExpectPositiveTriangle(live, *added);
		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);
		ASSERT_TRUE(undoStack->Undo());
		EXPECT_TRUE(live.paths->Store().Empty());
		EXPECT_TRUE(live.selectedScenePaths.empty());
		ASSERT_TRUE(undoStack->Redo());
		for (const auto id : *added) EXPECT_TRUE(live.paths->Store().Contains(id));
	}

	TEST_F(SceneCurvedArrowUndoTests, InvalidLaterPairLeavesNoPartialCycleOrUndo)
	{
		RendererWindowState window;
		window.windowId = "curved-arrow-invalid-cycle";
		PrepareTriangle(window);
		window.structure.defectFrame.emplace();
		window.structure.defectFrame->z = {0, 0, 1};
		window.structure.atoms[2].cartesianPosition = window.structure.atoms[1].cartesianPosition * 2.0f;
		renderer.AddWindow(std::move(window));
		auto &live = renderer.GetWindows().front();
		EXPECT_FALSE(AddCurvedArrowThroughSelectedAtoms(live));
		EXPECT_TRUE(!live.paths || live.paths->Store().Empty());
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
		EXPECT_EQ(live.selectedAtomIndices, std::vector<std::size_t>({2, 0, 1}));
		live.structure.atoms[2].cartesianPosition.x = std::numeric_limits<float>::quiet_NaN();
		EXPECT_FALSE(AddCurvedArrowThroughSelectedAtoms(live));
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}
}
