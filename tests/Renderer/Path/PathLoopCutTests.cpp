#include "Core/dspch.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <numbers>

#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathCommands.hpp"
#include "Renderer/Path/PathEditSession.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathPicking.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Renderer/Path/PathTopology.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath Curve(int kind)
		{
			ScenePath path;
			path.id = SceneObjectId{1};
			path.nodes = {{AllocateElementId(path), {0, 0, 0}, {}},
				{AllocateElementId(path), {2, 0, 0}, {}}};
			path.segments = {{AllocateElementId(path), LineSegmentData{}}};
			if (kind == 1)
				path.segments[0].data = CubicBezierSegmentData{
					{AllocateElementId(path), {0.3f, 1.0f, 0}, BezierHandleType::Free},
					{AllocateElementId(path), {-0.6f, 0.8f, 0}, BezierHandleType::Free}};
			if (kind == 2)
				path.segments[0].data = CircularArcSegmentData{{0, 0, 1}, std::numbers::pi_v<float> / 2};
			return path;
		}
	}

	TEST(PathLoopCutTests, EvenSplitsPreserveLineCubicAndArcShape)
	{
		for (int kind = 0; kind < 3; ++kind)
			for (const std::size_t count : {1u, 3u, 4u, 32u, 64u})
			{
				SCOPED_TRACE(testing::Message() << kind << "," << count);
				const auto original = Curve(kind);
				auto split = original;
				const auto inserted = InsertNodes(split, 0, count);
				ASSERT_TRUE(inserted);
				EXPECT_EQ(inserted->size(), count);
				EXPECT_EQ(split.nodes.size(), count + 2);
				EXPECT_EQ(split.segments.size(), count + 1);
				const auto before = ResolveNodePositions(original, BindingContext{});
				const auto after = ResolveNodePositions(split, BindingContext{});
				for (int sample = 0; sample <= 128; ++sample)
				{
					const double t = sample / 128.0;
					const double scaled = t * (count + 1);
					const auto segment = std::min(count, static_cast<std::size_t>(scaled));
					const auto a = EvaluateSegment(original, before, 0, t);
					const auto b = EvaluateSegment(split, after, segment, scaled - segment);
					ASSERT_TRUE(a);
					ASSERT_TRUE(b);
					EXPECT_NEAR(glm::distance(a->position, b->position), 0.0, 2.0e-5);
				}
			}
	}

	TEST(PathLoopCutTests, SegmentOnlyPickingIgnoresEndpointMarkers)
	{
		const auto path = Curve(0);
		const auto resolved = ResolveNodePositions(path, BindingContext{});
		const auto evaluated = Tessellate(path, resolved, TessellationSettings{});
		PathPickSettings settings;
		settings.viewportSize = {200, 200};
		settings.cursor = {102, 100};
		settings.editMode = true;
		settings.segmentOnly = true;
		const auto hit = PickPath(path, resolved, evaluated, settings);
		EXPECT_EQ(hit.kind, PathPickKind::Segment);
		EXPECT_EQ(hit.element, path.segments[0].id);
	}

	TEST(PathLoopCutTests, ModalSegmentToleranceReachesBeyondTheNormalPickBand)
	{
		const auto path = Curve(0);
		const auto resolved = ResolveNodePositions(path, {});
		const auto evaluated = Tessellate(path, resolved, {});
		PathPickSettings settings;
		settings.viewportSize = {200, 200};
		settings.cursor = {160, 130};
		settings.editMode = true;
		settings.segmentOnly = true;
		EXPECT_FALSE(PickPath(path, resolved, evaluated, settings).Hit());
		settings.strokePickTolerance = 48;
		EXPECT_EQ(PickPath(path, resolved, evaluated, settings).element, path.segments[0].id);
	}

	TEST(PathLoopCutTests, InvalidCountOrSegmentLeavesPathUntouched)
	{
		for (const std::size_t count : {0u, 65u})
		{
			auto path = Curve(1);
			EXPECT_FALSE(InsertNodes(path, 0, count));
			EXPECT_EQ(path.nodes.size(), 2u);
			EXPECT_EQ(path.nextElementId, Curve(1).nextElementId);
		}
		auto path = Curve(0);
		EXPECT_FALSE(InsertNodes(path, 9, 1));
		EXPECT_EQ(path.nodes.size(), 2u);
	}

	TEST(PathLoopCutTests, CommitHasOneUndoAndRestoresTheOriginalCurve)
	{
		RendererWindowState window;
		window.windowId = "loop-cut";
		SceneSystem::EnsurePathSystem(window).Store().Insert(Curve(1));
		UndoStack undo;
		int calls = 0;
		const PathEditContext context{&window,
			[&](RendererWindowState &, SceneObjectsSnapshot before, const std::string &description) {
				++calls;
				EXPECT_TRUE(undo.PushExecuted(CreateSceneObjectsSnapshotCommand(
					[&](const std::string &) { return &window; }, window.windowId, std::move(before), {}, description)));
			}};
		ASSERT_TRUE(InsertScenePathNodes(context, SceneObjectId{1}, 0, 4));
		EXPECT_EQ(calls, 1);
		EXPECT_EQ(window.paths->Store().Find(SceneObjectId{1})->nodes.size(), 6u);
		ASSERT_TRUE(undo.Undo());
		const auto &restored = *window.paths->Store().Find(SceneObjectId{1});
		EXPECT_EQ(restored.nodes.size(), 2u);
		EXPECT_EQ(restored.nextElementId, Curve(1).nextElementId);
		EXPECT_EQ(std::get<CubicBezierSegmentData>(restored.segments[0].data).startHandle.offset,
			std::get<CubicBezierSegmentData>(Curve(1).segments[0].data).startHandle.offset);
	}

	TEST(PathLoopCutTests, BufferedBoundEndpointKeepsItsDisplayedPositionAndBinding)
	{
		for (const std::size_t count : {1u, 3u, 32u, 64u})
		{
			RendererWindowState window;
			window.windowId = "bound-cut";
			window.structure.atoms.push_back({"C", {0, 0, 0}});
			auto path = Curve(1);
			path.nodes.front().binding.value = PathBinding::CopyPosition{0, {}, 1.0f};
			SceneSystem::EnsurePathSystem(window).Store().Insert(path);
			const auto before = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
			ASSERT_TRUE(InsertScenePathNodes(PathEditContext{&window, {}}, path.id, 0, count));
			const auto &split = *window.paths->Store().Find(path.id);
			const auto after = ResolveNodePositions(split, SceneSystem::MakePathBindingContext(window));
			EXPECT_NEAR(glm::distance(before.positions.front(), after.positions.front()), 0.0f, 2.0e-5f);
			EXPECT_EQ(std::get<PathBinding::CopyPosition>(split.nodes.front().binding.value).buffer, 1.0f);
			for (int index = 0; index <= 100; ++index)
			{
				const double scaled = index / 100.0 * (count + 1);
				const auto segment = std::min(count, static_cast<std::size_t>(scaled));
				const auto a = EvaluateSegment(path, before, 0, index / 100.0);
				const auto b = EvaluateSegment(split, after, segment, scaled - segment);
				ASSERT_TRUE(a);
				ASSERT_TRUE(b);
				EXPECT_NEAR(glm::distance(a->position, b->position), 0.0, 2.0e-5);
			}
		}
	}

	TEST(PathLoopCutTests, TransformedArcInsertPreservesTheDisplayedCircle)
	{
		RendererWindowState window;
		window.windowId = "arc-cut";
		auto path = Curve(2);
		path.transform.position = {3, 2, 1};
		path.transform.scale = {2, 0.7f, 1};
		SceneSystem::EnsurePathSystem(window).Store().Insert(path);
		const auto before = ResolveNodePositions(path, BindingContext{});
		ASSERT_TRUE(InsertScenePathNodes(PathEditContext{&window, {}}, path.id, 0, 4));
		const auto &split = *window.paths->Store().Find(path.id);
		const auto after = ResolveNodePositions(split, BindingContext{});
		for (int index = 0; index <= 100; ++index)
		{
			const double scaled = index / 20.0;
			const auto segment = std::min(std::size_t{4}, static_cast<std::size_t>(scaled));
			const auto a = EvaluateSegment(path, before, 0, index / 100.0);
			const auto b = EvaluateSegment(split, after, segment, scaled - segment);
			ASSERT_TRUE(a);
			ASSERT_TRUE(b);
			EXPECT_NEAR(glm::distance(a->position, b->position), 0.0, 2.0e-5);
		}
	}

	TEST(PathLoopCutTests, CommandInsertsThreeNodesWithoutChangingCubicOrArcGeometry)
	{
		for (const int kind : {1, 2})
		{
			RendererWindowState window;
			auto original = Curve(kind);
			original.transform.position = {3, -2, 1};
			original.transform.scale = {2, 0.7f, 1.3f};
			SceneSystem::EnsurePathSystem(window).Store().Insert(original);
			const auto before = ResolveNodePositions(original, {});
			ASSERT_TRUE(InsertScenePathNodes(PathEditContext{&window, {}}, original.id, 0, 3));
			const auto &split = *window.paths->Store().Find(original.id);
			const auto after = ResolveNodePositions(split, {});
			for (int index = 0; index <= 256; ++index)
			{
				const double t = index / 256.0, scaled = t * 4;
				const auto segment = std::min(std::size_t{3}, static_cast<std::size_t>(scaled));
				const auto a = EvaluateSegment(original, before, 0, t);
				const auto b = EvaluateSegment(split, after, segment, scaled - segment);
				ASSERT_TRUE(a); ASSERT_TRUE(b);
				EXPECT_LT(glm::distance(a->position, b->position), 1.0e-4);
			}
		}
	}

	TEST(PathLoopCutTests, EmptyPreviewAndRepickingKeepTheModalAndItsCount)
	{
		PathEditSession session;
		session.Enter(SceneObjectId{1});
		session.RequestInsert();
		session.BeginInsert({});
		ASSERT_TRUE(session.InsertPreview());
		EXPECT_FALSE(session.InsertPreview()->segment.IsValid());
		session.ChangeInsertCount(2);
		session.BeginInsert(PathElementId{3});
		EXPECT_EQ(session.InsertPreview()->count, 3u);
		session.BeginInsert(PathElementId{4});
		EXPECT_EQ(session.InsertPreview()->segment, PathElementId{4});
		EXPECT_EQ(session.InsertPreview()->count, 3u);
	}

	TEST(PathLoopCutTests, ModalCountClampsAndCancelAndLeaveClearThePreview)
	{
		PathEditSession session;
		session.Enter(SceneObjectId{1});
		session.RequestInsert();
		EXPECT_TRUE(session.InsertRequested());
		session.BeginInsert(PathElementId{3});
		ASSERT_TRUE(session.InsertPreview());
		EXPECT_EQ(session.InsertPreview()->count, 1u);
		session.ChangeInsertCount(100);
		EXPECT_EQ(session.InsertPreview()->count, 64u);
		session.ChangeInsertCount(-100);
		EXPECT_EQ(session.InsertPreview()->count, 1u);
		session.CancelInsert();
		EXPECT_FALSE(session.InsertPreview());
		EXPECT_FALSE(session.InsertRequested());
		session.BeginInsert(PathElementId{3});
		session.Leave();
		EXPECT_FALSE(session.InsertPreview());
	}
}
