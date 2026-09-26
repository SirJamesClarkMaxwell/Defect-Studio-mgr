#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <gtest/gtest.h>

#include <utility>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/ScenePathEditorWidget.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath Path(RendererWindowState &window, std::uint64_t id, float y = 0.0f)
		{
			ScenePath path;
			path.id = SceneObjectId{id};
			PathNode a{AllocateElementId(path), glm::vec3(-1.0f, y, 0.0f), {}};
			PathNode b{AllocateElementId(path), glm::vec3(1.0f, y, 0.0f), {}};
			path.nodes = {a, b};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			return path;
		}

		SceneObjectId Add(RendererWindowState &window, ScenePath path)
		{
			return SceneSystem::AppendScenePath(window, std::move(path));
		}

		[[nodiscard]] RendererStartupConfig EmptyRendererConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}

		void PrimePathCache(RendererWindowState &window, const SceneObjectId id)
		{
			const ScenePath *path = window.paths->Store().Find(id);
			ASSERT_NE(path, nullptr);
			const ResolvedNodes resolved = ResolveNodePositions(*path, BindingContext{});
			const EvaluatedPath evaluated = Tessellate(*path, resolved, TessellationSettings{});
			window.paths->Caches().Store(
				id, PathEvaluationKey{window.paths->Store().RevisionsFor(id), 0, 3},
				CachedPathGeometry{evaluated, {}});
		}

		void PrepareRegionWindow(RendererWindowState &window)
		{
			window.viewportSize = glm::vec2(800.0f, 600.0f);
			window.camera = CreateUnique<RendererViewCamera>();
			window.camera->SetViewport(800.0f, 600.0f);
		}
	}

	TEST(ScenePathOperationsTests, CopyPasteCreatesOffsetDetachedPaths)
	{
		RendererWindowState window;
		const SceneObjectId id = Add(window, Path(window, 1));
		window.selectedScenePaths = {id};
		CopyScenePathsToClipboard(window);
		PasteScenePathsFromClipboard(window);
		ASSERT_EQ(window.paths->Store().Size(), 2u);
		ASSERT_EQ(window.selectedScenePaths.size(), 1u);
		EXPECT_NE(window.selectedScenePaths.front(), id);
		EXPECT_EQ(window.paths->Store().At(1)->nodes.front().position.x, -0.5f);
	}

	TEST(ScenePathOperationsTests, EraseRemovesSelectionAndLeavesOtherIds)
	{
		RendererWindowState window;
		const SceneObjectId first = Add(window, Path(window, 1));
		const SceneObjectId second = Add(window, Path(window, 2, 2.0f));
		window.selectedScenePaths = {first, second};
		EraseScenePaths(window, {first});
		EXPECT_EQ(window.paths->Store().Size(), 1u);
		EXPECT_EQ(window.selectedScenePaths, std::vector<SceneObjectId>({second}));
	}

	TEST(ScenePathOperationsTests, RegionSelectionUsesIdsAndModes)
	{
		RendererWindowState window;
		const SceneObjectId id = Add(window, Path(window, 1));
		window.selectedScenePaths = {SceneObjectId{99}};
		ApplyLabelRegionSelection(window, {}, {}, {}, RendererEvents::Viewport::RegionSelectMode::Replace,
			{id});
		EXPECT_EQ(window.selectedScenePaths, std::vector<SceneObjectId>({id}));
		ApplyLabelRegionSelection(window, {}, {}, {}, RendererEvents::Viewport::RegionSelectMode::Add,
			{id});
		EXPECT_EQ(window.selectedScenePaths.size(), 1u);
		ApplyLabelRegionSelection(window, {}, {}, {}, RendererEvents::Viewport::RegionSelectMode::Subtract,
			{id});
		EXPECT_TRUE(window.selectedScenePaths.empty());
	}

	TEST(ScenePathOperationsTests, AppendCollisionReturnsErrorWithoutAddingAPath)
	{
		RendererWindowState window;
		ScenePath seeded = Path(window, 1);
		seeded.id = SceneObjectId{1};
		ASSERT_TRUE(SceneSystem::EnsurePathSystem(window).Store().Insert(seeded));
		const Result<SceneObjectId> result = AddScenePath(
			MakeSilentPathEditContext(window), Path(window, 2));
		EXPECT_FALSE(result.HasValue());
		EXPECT_EQ(window.paths->Store().Size(), 1u);
	}

	TEST(ScenePathOperationsTests, RegionHitTestsUseCachedPolylineAndSkipInvalidPaths)
	{
		RendererWindowState window;
		PrepareRegionWindow(window);
		const SceneObjectId crossing = Add(window, Path(window, 1));
		const SceneObjectId hidden = Add(window, Path(window, 2));
		const SceneObjectId unrenderable = Add(window, Path(window, 3));
		window.paths->Store().MutateStyle(hidden, [](ScenePath &path) { path.visible = false; });
		window.paths->Store().MutateStyle(unrenderable, [](ScenePath &path) { path.renderable = false; });
		PrimePathCache(window, crossing);
		PrimePathCache(window, hidden);
		PrimePathCache(window, unrenderable);

		EXPECT_EQ(HitTestRectScenePaths(window, glm::vec2(395.0f, 295.0f), glm::vec2(405.0f, 305.0f)),
			std::vector<SceneObjectId>({crossing}));
		EXPECT_EQ(HitTestCircleScenePaths(window, glm::vec2(400.0f, 300.0f), 8.0f),
			std::vector<SceneObjectId>({crossing}));
		RendererWindowState noCache;
		PrepareRegionWindow(noCache);
		const SceneObjectId noCachedId = Add(noCache, Path(noCache, 4));
		EXPECT_TRUE(HitTestRectScenePaths(noCache, glm::vec2(395.0f, 295.0f), glm::vec2(405.0f, 305.0f)).empty());
		EXPECT_TRUE(HitTestCircleScenePaths(noCache, glm::vec2(400.0f, 300.0f), 8.0f).empty());
		EXPECT_NE(noCachedId, SceneObjectId{});
	}

	class ScenePathUndoTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undoStack);
		}

		void TearDown() override
		{
			renderer.OnDetach();
		}

		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{EmptyRendererConfig()};
	};

	TEST_F(ScenePathUndoTests, MultiPathStyleEditUsesOneUndoAndRestoresEveryPath)
	{
		RendererWindowState window;
		window.windowId = "path-style";
		const SceneObjectId first = Add(window, Path(window, 1));
		const SceneObjectId second = Add(window, Path(window, 2, 1.0f));
		window.selectedScenePaths = {first, second};
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		ScenePathStyleEdit edit;
		edit.width = 0.4f;
		const PathDashStyle dash{true, 0.37f, 0.19f, 0.04f};
		const PathGradient gradient{true, {
			{0.2f, glm::vec3(0.1f, 0.2f, 0.3f), 0.4f},
			{0.8f, glm::vec3(0.7f, 0.6f, 0.5f), 0.9f}}};
		edit.dash = dash;
		edit.gradient = gradient;
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit), 2u);
		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);
		for (const SceneObjectId id : live.selectedScenePaths)
		{
			const ScenePath *changed = live.paths->Store().Find(id);
			ASSERT_NE(changed, nullptr);
			EXPECT_EQ(changed->style.dash.enabled, dash.enabled);
			EXPECT_FLOAT_EQ(changed->style.dash.dashLength, dash.dashLength);
			EXPECT_FLOAT_EQ(changed->style.dash.gapLength, dash.gapLength);
			EXPECT_FLOAT_EQ(changed->style.dash.phase, dash.phase);
			EXPECT_EQ(changed->style.gradient.enabled, gradient.enabled);
			ASSERT_EQ(changed->style.gradient.stops.size(), gradient.stops.size());
			for (std::size_t index = 0; index < gradient.stops.size(); ++index)
			{
				EXPECT_FLOAT_EQ(changed->style.gradient.stops[index].position, gradient.stops[index].position);
				EXPECT_EQ(changed->style.gradient.stops[index].color, gradient.stops[index].color);
				EXPECT_FLOAT_EQ(changed->style.gradient.stops[index].alpha, gradient.stops[index].alpha);
			}
		}
		ASSERT_TRUE(undoStack->Undo());
		const ScenePath *restoredFirst = live.paths->Store().Find(first);
		const ScenePath *restoredSecond = live.paths->Store().Find(second);
		ASSERT_NE(restoredFirst, nullptr);
		ASSERT_NE(restoredSecond, nullptr);
		EXPECT_FLOAT_EQ(restoredFirst->style.width, 0.05f);
		EXPECT_FLOAT_EQ(restoredSecond->style.width, 0.05f);
	}

	TEST_F(ScenePathUndoTests, DashAndGradientStyleDragCommitsOneUndoAfterManySilentApplies)
	{
		RendererWindowState window;
		window.windowId = "path-style-dash-gradient-drag";
		const SceneObjectId id = Add(window, Path(window, 1));
		window.selectedScenePaths = {id};
		window.paths->Store().MutateStyle(id, [](ScenePath &path) {
			path.style.dash = {true, 0.11f, 0.07f, 0.03f};
			path.style.gradient = {true, {
				{0.15f, glm::vec3(1.0f, 0.0f, 0.0f), 0.3f},
				{0.85f, glm::vec3(0.0f, 0.0f, 1.0f), 0.8f}}};
		});
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		const ScenePath *beforePath = live.paths->Store().Find(id);
		ASSERT_NE(beforePath, nullptr);
		const PathDashStyle beforeDash = beforePath->style.dash;
		const PathGradient beforeGradient = beforePath->style.gradient;

		BeginScenePathStyleDrag(live);
		ScenePathStyleEdit edit;
		edit.dash = {true, 0.2f, 0.1f, 0.05f};
		edit.gradient = {true, {
			{0.2f, glm::vec3(1.0f, 0.0f, 0.0f), 0.25f},
			{0.8f, glm::vec3(0.0f, 0.0f, 1.0f), 0.75f}}};
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 1u);
		edit.dash.dashLength = 0.3f;
		edit.gradient = {true, {
			{0.25f, glm::vec3(1.0f, 0.0f, 0.0f), 0.25f},
			{0.75f, glm::vec3(0.0f, 0.0f, 1.0f), 0.75f}}};
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 1u);
		edit.dash.dashLength = 0.4f;
		edit.gradient = {true, {
			{0.3f, glm::vec3(1.0f, 0.0f, 0.0f), 0.25f},
			{0.7f, glm::vec3(0.0f, 0.0f, 1.0f), 0.75f}}};
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 1u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);

		ASSERT_TRUE(CommitScenePathStyleDrag(live));
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
		ASSERT_TRUE(undoStack->Undo());
		const ScenePath *restored = live.paths->Store().Find(id);
		ASSERT_NE(restored, nullptr);
		EXPECT_EQ(restored->style.dash.enabled, beforeDash.enabled);
		EXPECT_FLOAT_EQ(restored->style.dash.dashLength, beforeDash.dashLength);
		EXPECT_FLOAT_EQ(restored->style.dash.gapLength, beforeDash.gapLength);
		EXPECT_FLOAT_EQ(restored->style.dash.phase, beforeDash.phase);
		EXPECT_EQ(restored->style.gradient.enabled, beforeGradient.enabled);
		ASSERT_EQ(restored->style.gradient.stops.size(), beforeGradient.stops.size());
		for (std::size_t index = 0; index < beforeGradient.stops.size(); ++index)
		{
			EXPECT_FLOAT_EQ(restored->style.gradient.stops[index].position, beforeGradient.stops[index].position);
			EXPECT_EQ(restored->style.gradient.stops[index].color, beforeGradient.stops[index].color);
			EXPECT_FLOAT_EQ(restored->style.gradient.stops[index].alpha, beforeGradient.stops[index].alpha);
		}
	}

	TEST_F(ScenePathUndoTests, ScenePathStyleDragCommitsOneUndoAfterManySilentApplies)
	{
		RendererWindowState window;
		window.windowId = "path-style-drag";
		const SceneObjectId first = Add(window, Path(window, 1));
		const SceneObjectId second = Add(window, Path(window, 2, 1.0f));
		window.paths->Store().MutateStyle(first, [](ScenePath &path) {
			path.style.alpha = 0.6f;
			path.style.startDecoration = {PathDecorationKind::Circle, 2.0f, 0.75f, false};
			path.style.endDecoration = {PathDecorationKind::Square, 1.5f, 0.5f, true};
		});
		window.paths->Store().MutateStyle(second, [](ScenePath &path) {
			path.style.width = 0.15f;
			path.style.alpha = 0.8f;
			path.style.startDecoration = {PathDecorationKind::Kite, 1.25f, 1.5f, true};
			path.style.endDecoration = {PathDecorationKind::Latex, 0.9f, 1.25f, false};
		});
		window.selectedScenePaths = {first, second};
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		const ScenePath *firstBeforePath = live.paths->Store().Find(first);
		const ScenePath *secondBeforePath = live.paths->Store().Find(second);
		ASSERT_NE(firstBeforePath, nullptr);
		ASSERT_NE(secondBeforePath, nullptr);
		const PathStrokeStyle firstBefore = firstBeforePath->style;
		const PathStrokeStyle secondBefore = secondBeforePath->style;

		BeginScenePathStyleDrag(live);
		ScenePathStyleEdit edit;
		edit.width = 0.2f;
		edit.alpha = 0.25f;
		edit.startDecoration = {PathDecorationKind::Diamond, 1.1f, 0.4f, true};
		edit.endDecoration = {PathDecorationKind::Arrow, 1.6f, 0.8f, false};
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 2u);
		edit.width = 0.3f;
		edit.startDecoration.lengthScale = 1.7f;
		edit.endDecoration.widthScale = 1.2f;
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 2u);
		edit.width = 0.4f;
		edit.startDecoration.filled = false;
		edit.endDecoration.filled = true;
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 2u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);

		ASSERT_TRUE(CommitScenePathStyleDrag(live));
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
		EXPECT_FALSE(live.scenePathStyleEditBefore.has_value());
		ASSERT_TRUE(undoStack->Undo());
		const ScenePath *restoredFirst = live.paths->Store().Find(first);
		const ScenePath *restoredSecond = live.paths->Store().Find(second);
		ASSERT_NE(restoredFirst, nullptr);
		ASSERT_NE(restoredSecond, nullptr);
		EXPECT_FLOAT_EQ(restoredFirst->style.width, firstBefore.width);
		EXPECT_FLOAT_EQ(restoredFirst->style.alpha, firstBefore.alpha);
		EXPECT_EQ(restoredFirst->style.startDecoration.kind, firstBefore.startDecoration.kind);
		EXPECT_FLOAT_EQ(restoredFirst->style.startDecoration.lengthScale, firstBefore.startDecoration.lengthScale);
		EXPECT_FLOAT_EQ(restoredFirst->style.startDecoration.widthScale, firstBefore.startDecoration.widthScale);
		EXPECT_EQ(restoredFirst->style.startDecoration.filled, firstBefore.startDecoration.filled);
		EXPECT_EQ(restoredFirst->style.endDecoration.kind, firstBefore.endDecoration.kind);
		EXPECT_FLOAT_EQ(restoredFirst->style.endDecoration.lengthScale, firstBefore.endDecoration.lengthScale);
		EXPECT_FLOAT_EQ(restoredFirst->style.endDecoration.widthScale, firstBefore.endDecoration.widthScale);
		EXPECT_EQ(restoredFirst->style.endDecoration.filled, firstBefore.endDecoration.filled);
		EXPECT_FLOAT_EQ(restoredSecond->style.width, secondBefore.width);
		EXPECT_FLOAT_EQ(restoredSecond->style.alpha, secondBefore.alpha);
		EXPECT_EQ(restoredSecond->style.startDecoration.kind, secondBefore.startDecoration.kind);
		EXPECT_FLOAT_EQ(restoredSecond->style.startDecoration.lengthScale, secondBefore.startDecoration.lengthScale);
		EXPECT_FLOAT_EQ(restoredSecond->style.startDecoration.widthScale, secondBefore.startDecoration.widthScale);
		EXPECT_EQ(restoredSecond->style.startDecoration.filled, secondBefore.startDecoration.filled);
		EXPECT_EQ(restoredSecond->style.endDecoration.kind, secondBefore.endDecoration.kind);
		EXPECT_FLOAT_EQ(restoredSecond->style.endDecoration.lengthScale, secondBefore.endDecoration.lengthScale);
		EXPECT_FLOAT_EQ(restoredSecond->style.endDecoration.widthScale, secondBefore.endDecoration.widthScale);
		EXPECT_EQ(restoredSecond->style.endDecoration.filled, secondBefore.endDecoration.filled);
	}

	TEST_F(ScenePathUndoTests, FlatRibbonThicknessDragCommitsOneUndo)
	{
		RendererWindowState window;
		window.windowId = "flat-ribbon-thickness-drag";
		ScenePath path = Path(window, 1);
		path.style.profile = StrokeProfile::Flat;
		const SceneObjectId id = Add(window, std::move(path));
		window.selectedScenePaths = {id};
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();

		const ScenePathStyleEditState state = ResolveScenePathStyleEdit(live, live.selectedScenePaths);
		ASSERT_EQ(state.resolved, 1u);
		EXPECT_TRUE(state.anyFlatProfile);
		EXPECT_FALSE(state.mixedRibbonThickness);
		EXPECT_FLOAT_EQ(state.values.ribbonThickness, 0.0f);

		BeginScenePathStyleDrag(live);
		ScenePathStyleEdit edit = state.values;
		for (const float thickness : {0.05f, 0.1f, 0.2f})
		{
			edit.ribbonThickness = thickness;
			ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 1u);
		}
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
		ASSERT_TRUE(CommitScenePathStyleDrag(live));
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);

		const ScenePath *changed = live.paths->Store().Find(id);
		ASSERT_NE(changed, nullptr);
		EXPECT_FLOAT_EQ(changed->style.ribbonThickness, 0.2f);
		ASSERT_TRUE(undoStack->Undo());
		const ScenePath *restored = live.paths->Store().Find(id);
		ASSERT_NE(restored, nullptr);
		EXPECT_FLOAT_EQ(restored->style.ribbonThickness, 0.0f);
	}

	TEST_F(ScenePathUndoTests, BeginningAnExistingStyleDragDoesNotReplaceItsSnapshot)
	{
		RendererWindowState window;
		window.windowId = "path-style-drag-begin";
		const SceneObjectId id = Add(window, Path(window, 1));
		window.selectedScenePaths = {id};
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		const ScenePath *beforePath = live.paths->Store().Find(id);
		ASSERT_NE(beforePath, nullptr);
		const float before = beforePath->style.width;

		BeginScenePathStyleDrag(live);
		ScenePathStyleEdit edit;
		edit.width = 0.2f;
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 1u);
		BeginScenePathStyleDrag(live);
		edit.width = 0.3f;
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, false), 1u);
		ASSERT_TRUE(CommitScenePathStyleDrag(live));
		ASSERT_TRUE(undoStack->Undo());
		const ScenePath *restored = live.paths->Store().Find(id);
		ASSERT_NE(restored, nullptr);
		EXPECT_FLOAT_EQ(restored->style.width, before);
	}

	TEST_F(ScenePathUndoTests, CommittingWithoutAStyleDragDoesNothing)
	{
		RendererWindowState window;
		window.windowId = "path-style-no-drag";
		Add(window, Path(window, 1));
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		EXPECT_FALSE(CommitScenePathStyleDrag(live));
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathUndoTests, UnchangedStyleDragLeavesNoUndoEntry)
	{
		RendererWindowState window;
		window.windowId = "path-style-unchanged";
		Add(window, Path(window, 1));
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		BeginScenePathStyleDrag(live);
		EXPECT_FALSE(CommitScenePathStyleDrag(live));
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
		EXPECT_FALSE(live.scenePathStyleEditBefore.has_value());
	}

	TEST_F(ScenePathUndoTests, ImmediateComboStyleEditsStillRecordOneUndoEach)
	{
		RendererWindowState window;
		window.windowId = "path-style-combos";
		const SceneObjectId id = Add(window, Path(window, 1));
		window.selectedScenePaths = {id};
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		ScenePathStyleEdit edit;
		edit.profile = StrokeProfile::Flat;
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, true), 1u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
		edit.depthMode = PathDepthMode::AlwaysOnTop;
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, true), 1u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 2u);
		edit.startDecoration = {PathDecorationKind::Kite, 1.5f, 0.75f, false};
		edit.endDecoration = {PathDecorationKind::Circle, 0.8f, 1.25f, true};
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit, true), 1u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 3u);
	}

	TEST_F(ScenePathUndoTests, DuplicateThreePathsUsesOneUndo)
	{
		RendererWindowState window;
		window.windowId = "path-duplicate";
		for (std::uint64_t id = 1; id <= 3; ++id)
			window.selectedScenePaths.push_back(Add(window, Path(window, id, static_cast<float>(id))));
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		DuplicateSelectedScenePaths(live);
		ASSERT_EQ(live.paths->Store().Size(), 6u);
		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);
		ASSERT_TRUE(undoStack->Undo());
		EXPECT_EQ(live.paths->Store().Size(), 3u);
	}

	TEST_F(ScenePathUndoTests, EraseUsesOneUndo)
	{
		RendererWindowState window;
		window.windowId = "path-erase";
		const SceneObjectId first = Add(window, Path(window, 1));
		const SceneObjectId second = Add(window, Path(window, 2, 1.0f));
		window.selectedScenePaths = {first, second};
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();
		EraseScenePaths(live, live.selectedScenePaths);
		ASSERT_EQ(live.paths->Store().Size(), 0u);
		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);
		ASSERT_TRUE(undoStack->Undo());
		EXPECT_EQ(live.paths->Store().Size(), 2u);
	}

	TEST(ScenePathOperationsTests, DevPresetsKeepTheirStrokeProfile)
	{
		EXPECT_EQ(MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0.0f), StrokeProfile::Flat).style.profile,
			StrokeProfile::Flat);
		EXPECT_EQ(MakeDevScenePath(ScenePathDevPreset::Arc, glm::vec3(0.0f), StrokeProfile::CameraFacing).style.profile,
			StrokeProfile::CameraFacing);
		EXPECT_EQ(MakeDevScenePath(ScenePathDevPreset::Cubic, glm::vec3(0.0f)).style.profile, StrokeProfile::Round);
		EXPECT_EQ(MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0.0f), StrokeProfile::Flat).style.ribbonNormal,
			glm::vec3(0.0f, 0.0f, 1.0f));
	}

	TEST(ScenePathOperationsTests, CurvedFlatDevPresetBuildsAFlatArrowPath)
	{
		const ScenePath path = MakeDevScenePath(ScenePathDevPreset::Cubic, glm::vec3(2.0f, -1.0f, 3.0f), StrokeProfile::Flat);
		EXPECT_EQ(path.style.profile, StrokeProfile::Flat);
		EXPECT_EQ(path.style.endDecoration.kind, PathDecorationKind::Arrow);
		ASSERT_EQ(path.nodes.size(), 2u);
		ASSERT_EQ(path.segments.size(), 1u);
		ASSERT_TRUE(std::holds_alternative<CubicBezierSegmentData>(path.segments[0].data));
	}

	TEST_F(ScenePathUndoTests, DecorationGalleryAddsEveryKindNamedAndSpacedInOneUndo)
	{
		constexpr std::array<std::pair<PathDecorationKind, const char *>, 8> galleryKinds = {{
			{PathDecorationKind::Arrow, "Arrow"},
			{PathDecorationKind::Stealth, "Stealth"},
			{PathDecorationKind::Latex, "Latex"},
			{PathDecorationKind::Bar, "Bar"},
			{PathDecorationKind::Circle, "Circle"},
			{PathDecorationKind::Square, "Square"},
			{PathDecorationKind::Diamond, "Diamond"},
			{PathDecorationKind::Kite, "Kite"},
		}};

		RendererWindowState window;
		window.windowId = "path-decoration-gallery";
		renderer.AddWindow(std::move(window));
		ASSERT_FALSE(renderer.GetWindows().empty());
		RendererWindowState &live = renderer.GetWindows().front();

		AddScenePathDecorationGallery(live, glm::vec3(2.0f, -1.0f, 3.0f));
		ASSERT_NE(live.paths, nullptr);
		ASSERT_EQ(live.paths->Store().Size(), galleryKinds.size());
		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);

		for (std::size_t index = 0; index < galleryKinds.size(); ++index)
		{
			const ScenePath *path = live.paths->Store().At(index);
			ASSERT_NE(path, nullptr);
			ASSERT_GE(path->nodes.size(), 1u);
			EXPECT_EQ(path->style.startDecoration.kind, galleryKinds[index].first);
			EXPECT_EQ(path->style.endDecoration.kind, PathDecorationKind::Arrow);
			EXPECT_EQ(path->name, galleryKinds[index].second);

			if (index == 0u)
				continue;

			const ScenePath *previous = live.paths->Store().At(index - 1u);
			ASSERT_NE(previous, nullptr);
			ASSERT_GE(previous->nodes.size(), 1u);
			const DecorationContour previousContour = BuildDecorationContour(
				previous->style.startDecoration, previous->style.width);
			const DecorationContour currentContour = BuildDecorationContour(
				path->style.startDecoration, path->style.width);
			ASSERT_FALSE(previousContour.points.empty());
			ASSERT_FALSE(currentContour.points.empty());
			const auto previousWidest = std::max_element(previousContour.points.begin(), previousContour.points.end(),
				[](const DecorationContourPoint &a, const DecorationContourPoint &b) {
					return a.halfWidth < b.halfWidth;
				});
			const auto currentWidest = std::max_element(currentContour.points.begin(), currentContour.points.end(),
				[](const DecorationContourPoint &a, const DecorationContourPoint &b) {
					return a.halfWidth < b.halfWidth;
				});
			ASSERT_NE(previousWidest, previousContour.points.end());
			ASSERT_NE(currentWidest, currentContour.points.end());
			const double layoutSeparation = std::abs(static_cast<double>(
				path->nodes.front().position.z - previous->nodes.front().position.z));
			EXPECT_GT(layoutSeparation, previousWidest->halfWidth + currentWidest->halfWidth);
		}
	}

	TEST(ScenePathOperationsTests, RibbonNormalStyleEditInvalidatesCachedGeometry)
	{
		RendererWindowState window;
		const SceneObjectId id = Add(window, Path(window, 1));
		PrimePathCache(window, id);
		const PathEvaluationKey before{window.paths->Store().RevisionsFor(id), 0, 3};
		ASSERT_NE(window.paths->Caches().Find(id, before), nullptr);
		window.paths->Store().MutateStyle(id, [](ScenePath &path) { path.style.ribbonNormal = glm::vec3(0.0f, 0.0f, 1.0f); });
		const PathEvaluationKey after{window.paths->Store().RevisionsFor(id), 0, 3};
		EXPECT_NE(before.revisions.style, after.revisions.style);
		EXPECT_EQ(window.paths->Caches().Find(id, after), nullptr);
	}

	TEST(ScenePathOperationsTests, StyleDragSnapshotsBelongToTheirOwnWindows)
	{
		RendererWindowState firstWindow;
		RendererWindowState secondWindow;
		const SceneObjectId firstId = Add(firstWindow, Path(firstWindow, 1));
		const SceneObjectId secondId = Add(secondWindow, Path(secondWindow, 2));
		secondWindow.paths->Store().MutateStyle(secondId, [](ScenePath &path) { path.style.width = 0.15f; });

		BeginScenePathStyleDrag(firstWindow);
		BeginScenePathStyleDrag(secondWindow);
		ASSERT_TRUE(firstWindow.scenePathStyleEditBefore.has_value());
		ASSERT_TRUE(secondWindow.scenePathStyleEditBefore.has_value());
		const ScenePath *firstSnapshot = firstWindow.scenePathStyleEditBefore->paths.Find(firstId);
		const ScenePath *secondSnapshot = secondWindow.scenePathStyleEditBefore->paths.Find(secondId);
		ASSERT_NE(firstSnapshot, nullptr);
		ASSERT_NE(secondSnapshot, nullptr);
		EXPECT_FLOAT_EQ(firstSnapshot->style.width, 0.05f);
		EXPECT_FLOAT_EQ(secondSnapshot->style.width, 0.15f);

		ScenePathStyleEdit edit;
		edit.width = 0.4f;
		ASSERT_EQ(ApplyScenePathStyleEdit(firstWindow, {firstId}, edit, false), 1u);
		ASSERT_TRUE(firstWindow.scenePathStyleEditBefore.has_value());
		ASSERT_TRUE(secondWindow.scenePathStyleEditBefore.has_value());
		firstSnapshot = firstWindow.scenePathStyleEditBefore->paths.Find(firstId);
		secondSnapshot = secondWindow.scenePathStyleEditBefore->paths.Find(secondId);
		ASSERT_NE(firstSnapshot, nullptr);
		ASSERT_NE(secondSnapshot, nullptr);
		EXPECT_FLOAT_EQ(firstSnapshot->style.width, 0.05f);
		EXPECT_FLOAT_EQ(secondSnapshot->style.width, 0.15f);
	}
}
