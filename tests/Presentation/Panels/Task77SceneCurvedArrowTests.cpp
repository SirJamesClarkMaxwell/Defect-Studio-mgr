#include "Core/dspch.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		void Prepare(RendererWindowState &window, bool cycle)
		{
			window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {-1, std::sqrt(3.0f), 0}}};
			if (cycle) window.structure.atoms.push_back({"C", {-1, -std::sqrt(3.0f), 0}});
			for (std::size_t i = 0; i < window.structure.atoms.size(); ++i)
				window.structure.atoms[i].radius = 0.35f + 0.1f * static_cast<float>(i);
			window.selectedAtomIndices = cycle ? std::vector<std::size_t>{0, 1, 2} : std::vector<std::size_t>{0, 1};
			window.structure.defectFrame.emplace();
			window.structure.defectFrame->z = {0, 0, 1};
		}

		std::vector<glm::dvec3> Geometry(const RendererWindowState &window, const std::vector<SceneObjectId> &ids)
		{
			std::vector<glm::dvec3> points;
			for (const auto id : ids)
			{
				const auto &path = *window.paths->Store().Find(id);
				const auto resolved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
				for (double t : {0.0, 0.5, 1.0})
				{
					const auto sample = EvaluateSegment(path, resolved, 0, t);
					EXPECT_TRUE(sample);
					if (sample) points.push_back(sample->position);
				}
			}
			return points;
		}
	}

	TEST(Task77SceneCurvedArrowTests, RadiusGapAndCurvatureSweepsAreContinuousAtEveryEndAndMidpoint)
	{
		for (bool cycle : {false, true})
			for (int parameter = 0; parameter < 3; ++parameter)
			{
				std::vector<glm::dvec3> previous;
				const int last = parameter == 0 ? 50 : parameter == 1 ? 100 : 145;
				for (int step = 0; step <= last; ++step)
				{
					SCOPED_TRACE(testing::Message() << "cycle=" << cycle << " parameter=" << parameter << " step=" << step);
					RendererWindowState window;
					Prepare(window, cycle);
					CurvedArrowParameters p;
					p.axisMode = CurvedArrowAxisMode::DefectZ;
					p.radiusScale = parameter == 0 ? 1.0f + step * 0.01f : 1.3f;
					p.endGap = parameter == 1 ? step * 0.01f : 0.15f;
					p.curvature = parameter == 2 ? 0.05f + step * 0.01f : 0.631f;
					const auto added = AddCurvedArrowThroughSelectedAtoms(window, p, SceneOperationUndo::Suppress);
					ASSERT_TRUE(added);
					const auto points = Geometry(window, *added);
					ASSERT_EQ(points.size(), added->size() * 3);
					if (!previous.empty())
						for (std::size_t i = 0; i < points.size(); ++i)
							// A change of 0.01 moves a 2 A circle by ~0.02-0.1 A; the old branch jumped by ~0.8 A.
							EXPECT_LT(glm::distance(previous[i], points[i]), 0.2);
					previous = points;
				}
			}
	}

	TEST(Task77SceneCurvedArrowTests, ScaleThirteenMovesTheEndsOutOntoTheLargerCircle)
	{
		RendererWindowState window;
		Prepare(window, true);
		const auto added = AddCurvedArrowThroughSelectedAtoms(window, {.radiusScale = 1.3f}, SceneOperationUndo::Suppress);
		ASSERT_TRUE(added);
		const auto context = SceneSystem::MakePathBindingContext(window);
		for (const auto id : *added)
		{
			const auto &path = *window.paths->Store().Find(id);
			const auto ends = ResolveNodePositions(path, context);
			for (std::size_t i = 0; i < 2; ++i)
			{
				const auto atomIndex = std::get<PathBinding::CopyPosition>(path.nodes[i].binding.value).atomIndex;
				const auto &atom = window.structure.atoms[atomIndex];
				// On the larger circle, and at least r(1 + gap) from the atom.
				EXPECT_NEAR(glm::length(glm::vec2(ends.positions[i])), 1.3f * glm::length(glm::vec2(atom.cartesianPosition)), 1.0e-4f);
				EXPECT_GE(glm::distance(ends.positions[i], atom.cartesianPosition), atom.radius * GetScenePathAtomBuffer() - 1.0e-4f);
			}
		}
	}

	TEST(Task77SceneCurvedArrowTests, TwoEndsMakeOppositeDirectedArcsForAtomsAndVacancies)
	{
		for (int kind = 0; kind < 3; ++kind)
			for (float tilt : {0.0f, 90.0f})
			for (int count : {0, 1, 2, 6})
			{
				RendererWindowState window;
				Prepare(window, false);
				window.structure.vacancies.push_back({window.structure.atoms[1].cartesianPosition});
				window.structure.vacancies.push_back({window.structure.atoms[0].cartesianPosition});
				if (kind > 0)
				{
					window.selectedAtomIndices = kind == 1 ? std::vector<std::size_t>{0} : std::vector<std::size_t>{};
					window.selectedVacancies = kind == 1 ? std::vector<std::size_t>{0} : std::vector<std::size_t>{1, 0};
				}
				const auto added = AddCurvedArrowThroughSelectedAtoms(window,
					{.axisMode = CurvedArrowAxisMode::DefectZ, .arrowCount = count, .tiltDegrees = tilt}, SceneOperationUndo::Suppress);
				ASSERT_TRUE(added);
				ASSERT_EQ(added->size(), count <= 1 ? 1u : 2u);
				EXPECT_EQ(window.selectedScenePaths, *added);
				const auto &first = *window.paths->Store().Find(added->front());
				EXPECT_TRUE(first.style.shadeSmooth);
				EXPECT_EQ(std::holds_alternative<PathBinding::CopyVacancy>(first.nodes.front().binding.value), kind == 2);
				EXPECT_EQ(std::holds_alternative<PathBinding::CopyVacancy>(first.nodes.back().binding.value), kind != 0);
				if (added->size() == 1) continue;
				const auto &second = *window.paths->Store().Find(added->back());
				const auto context = SceneSystem::MakePathBindingContext(window);
				const auto a = ResolveNodePositions(first, context), b = ResolveNodePositions(second, context);
				// A C_2 pair: A -> B and B -> A turn the same way about the axis and together close the
				// circle, so the two arcs lie on opposite sides of the A-B chord.
				const auto ma = EvaluateSegment(first, a, 0, 0.5), mb = EvaluateSegment(second, b, 0, 0.5);
				ASSERT_TRUE(ma);
				ASSERT_TRUE(mb);
				const glm::dvec3 chord = glm::dvec3(a.positions.back()) - glm::dvec3(a.positions.front());
				const glm::dvec3 axis(0, 0, 1);
				const auto side = [&](const glm::dvec3 &p) {
					return glm::dot(glm::cross(chord, p - glm::dvec3(a.positions.front())), axis);
				};
				if (tilt == 0.0f)
					EXPECT_LT(side(ma->position) * side(mb->position), 0.0);
				EXPECT_GT(glm::distance(ma->position, mb->position), 0.5);
			}
	}

	TEST(Task77SceneCurvedArrowTests, QuarterTiltMovesMidpointsByTheArcHeightAndKeepsEveryEnd)
	{
		for (bool cycle : {false, true})
		{
			RendererWindowState before, after;
			Prepare(before, cycle);
			Prepare(after, cycle);
			const auto a = AddCurvedArrowThroughSelectedAtoms(before,
				{.axisMode = CurvedArrowAxisMode::DefectZ}, SceneOperationUndo::Suppress);
			const auto b = AddCurvedArrowThroughSelectedAtoms(after,
				{.axisMode = CurvedArrowAxisMode::DefectZ, .tiltDegrees = 90}, SceneOperationUndo::Suppress);
			ASSERT_TRUE(a);
			ASSERT_TRUE(b);
			const auto oldPoints = Geometry(before, *a), newPoints = Geometry(after, *b);
			ASSERT_EQ(oldPoints.size(), newPoints.size());
			for (std::size_t i = 0; i < oldPoints.size(); i += 3)
			{
				EXPECT_LT(glm::distance(oldPoints[i], newPoints[i]), 1.0e-5);
				EXPECT_LT(glm::distance(oldPoints[i+2], newPoints[i+2]), 1.0e-5);
				const auto center = (oldPoints[i] + oldPoints[i+2]) * 0.5;
				const double height = glm::distance(oldPoints[i+1], center);
				EXPECT_NEAR(std::abs(newPoints[i+1].z), height, 1.0e-5);
				EXPECT_NEAR(newPoints[i+1].x, center.x, 1.0e-5);
				EXPECT_NEAR(newPoints[i+1].y, center.y, 1.0e-5);
			}
		}
	}

	class Task77SceneCurvedArrowUndoTests : public testing::Test
	{
	protected:
		void SetUp() override { renderer.BindUndoStack(undo); }
		void TearDown() override { renderer.OnDetach(); }
		Ref<UndoStack> undo = CreateRef<UndoStack>();
		RendererLayer renderer{[] { RendererStartupConfig c; c.loadDefaultScene = false; return c; }()};
	};

	TEST_F(Task77SceneCurvedArrowUndoTests, TiltedPairIsSelectedAndRestoredAsOneUndoStep)
	{
		RendererWindowState window;
		window.windowId = "task77-pair";
		Prepare(window, false);
		renderer.AddWindow(std::move(window));
		auto &live = renderer.GetWindows().front();
		const auto added = AddCurvedArrowThroughSelectedAtoms(live,
			{.axisMode = CurvedArrowAxisMode::DefectZ, .tiltDegrees = 90});
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 2u);
		EXPECT_EQ(live.selectedScenePaths, *added);
		const auto points = Geometry(live, *added);
		ASSERT_EQ(undo->GetUndoDepth(), 1u);
		ASSERT_TRUE(undo->Undo());
		EXPECT_TRUE(live.paths->Store().Empty());
		EXPECT_TRUE(live.selectedScenePaths.empty());
		ASSERT_TRUE(undo->Redo());
		EXPECT_EQ(Geometry(live, *added), points);
	}

	TEST(Task77SceneCurvedArrowTests, BoxAndCircleIncludeVisibleBoundAndBondFrameArcs)
	{
		for (bool bond : {false, true})
			for (bool labels : {false, true})
			{
				RendererWindowState window;
				Prepare(window, false);
				window.pickLabels = labels;
				window.viewportSize = {800, 600};
				window.camera = CreateUnique<RendererViewCamera>();
				window.camera->SetViewport(800, 600);
				const auto ids = AddCurvedArrowThroughSelectedAtoms(window,
					{.axisMode = bond ? CurvedArrowAxisMode::Bond : CurvedArrowAxisMode::DefectZ}, SceneOperationUndo::Suppress);
				ASSERT_TRUE(ids);
				const auto bindings = SceneSystem::MakePathBindingContext(window);
				const auto vp = window.camera->ProjectionMatrix() * window.camera->ViewMatrix();
				for (const auto id : *ids)
				{
					const auto &path = *window.paths->Store().Find(id);
					const auto resolved = ResolveNodePositions(path, bindings);
					const auto evaluated = Tessellate(path, resolved, TessellationSettings{});
					ASSERT_GT(evaluated.samples.size(), 2u);
					window.paths->Caches().Store(id, {window.paths->Store().RevisionsFor(id), BindingSourceRevision(path, resolved), 3},
						CachedPathGeometry{evaluated, {}});
				}
				for (const auto id : *ids)
				{
					const auto &evaluated = window.paths->Caches().FindLastBuilt(id)->evaluated;
					const auto center = SelectionHitTest::ProjectToScreen(vp, window.viewportSize,
						glm::vec3(evaluated.samples[evaluated.samples.size()/2].position));
					ASSERT_TRUE(center);
					const auto rect = HitTestRectScenePaths(window, *center - glm::vec2(3), *center + glm::vec2(3));
					const auto circle = HitTestCircleScenePaths(window, *center, 3);
					EXPECT_NE(std::find(rect.begin(), rect.end(), id), rect.end());
					EXPECT_NE(std::find(circle.begin(), circle.end(), id), circle.end());
					ApplyLabelRegionSelection(window, {}, {}, RendererEvents::Viewport::RegionSelectMode::Replace, rect);
					EXPECT_NE(std::find(window.selectedScenePaths.begin(), window.selectedScenePaths.end(), id), window.selectedScenePaths.end());
				}
			}
	}

	TEST(Task80SceneCurvedArrowTests, PerpendicularAxisPairGoesOverAndUnderTheBondFromOutsideTheAtoms)
	{
		// The user's C_2 sketch: two bonded atoms, axis perpendicular to the bond, a larger circle.
		RendererWindowState window;
		window.structure.atoms = {{"C", {-0.77f, 0, 0}}, {"C", {0.77f, 0, 0}}, {"C", {0, 1.0f, 0}}};
		for (auto &atom : window.structure.atoms) atom.radius = 0.35f;
		window.structure.defectFrame.emplace(); // ignored by this axis choice
		window.structure.defectFrame->z = {1, 0, 0};
		window.selectedAtomIndices = {0, 1};
		const auto added = AddCurvedArrowThroughSelectedAtoms(window,
			{.axisMode = CurvedArrowAxisMode::PerpendicularToBond, .arrowCount = 2, .radiusScale = 1.6f},
			SceneOperationUndo::Suppress);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 2u);
		const auto context = SceneSystem::MakePathBindingContext(window);
		std::vector<double> sides;
		for (const auto id : *added)
		{
			const auto &path = *window.paths->Store().Find(id);
			const auto ends = ResolveNodePositions(path, context);
			for (std::size_t i = 0; i < 2; ++i)
			{
				const auto atomIndex = std::get<PathBinding::CopyPosition>(path.nodes[i].binding.value).atomIndex;
				EXPECT_GE(glm::distance(ends.positions[i], window.structure.atoms[atomIndex].cartesianPosition),
					0.35f * GetScenePathAtomBuffer() - 1.0e-4f);
				EXPECT_NEAR(glm::length(ends.positions[i]), 1.6f * 0.77f, 1.0e-4f); // on the larger circle
			}
			const auto middle = EvaluateSegment(path, ends, 0, 0.5);
			ASSERT_TRUE(middle);
			EXPECT_LT(std::abs(middle->position.x), 0.3); // crosses the bond's perpendicular bisector
			sides.push_back(middle->position.y);
		}
		EXPECT_LT(sides[0] * sides[1], 0.0); // one over, one under
	}
}
