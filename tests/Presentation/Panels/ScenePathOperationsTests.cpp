#include "Core/dspch.hpp"

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
		ASSERT_EQ(ApplyScenePathStyleEdit(live, live.selectedScenePaths, edit), 2u);
		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);
		ASSERT_TRUE(undoStack->Undo());
		EXPECT_FLOAT_EQ(live.paths->Store().Find(first)->style.width, 0.05f);
		EXPECT_FLOAT_EQ(live.paths->Store().Find(second)->style.width, 0.05f);
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
}
