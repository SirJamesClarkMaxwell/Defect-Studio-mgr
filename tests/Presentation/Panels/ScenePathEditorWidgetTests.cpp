#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Presentation/Panels/ScenePathEditorWidget.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath MakePath(RendererWindowState &window, const char *name, float width)
		{
			ScenePath path;
			path.id = window.sceneRegistry.AllocateObjectId();
			path.name = name;
			path.style.width = width;
			PathNode first;
			first.id = AllocateElementId(path);
			first.position = glm::vec3(0.0f);
			PathNode second;
			second.id = AllocateElementId(path);
			second.position = glm::vec3(1.0f, 0.0f, 0.0f);
			path.nodes = {first, second};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			return path;
		}

		void AddPaths(RendererWindowState &window)
		{
			PathSystem &paths = SceneSystem::EnsurePathSystem(window);
			paths.Store().Insert(MakePath(window, "First", 0.05f));
			paths.Store().Insert(MakePath(window, "Second", 0.15f));
		}
	}

	TEST(ScenePathEditorWidgetTests, EmptySelectionUsesDefaults)
	{
		RendererWindowState window;
		AddPaths(window);
		const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, {});
		EXPECT_EQ(state.resolved, 0u);
		EXPECT_FALSE(state.mixedWidth);
	}

	TEST(ScenePathEditorWidgetTests, ResolvesFirstPathAndMarksOnlyDifferingFieldsMixed)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		const ScenePathStyleEditState one = ResolveScenePathStyleEdit(window, {ids[0]});
		EXPECT_EQ(one.resolved, 1u);
		EXPECT_FLOAT_EQ(one.values.width, 0.05f);
		EXPECT_FALSE(one.mixedWidth);

		const ScenePathStyleEditState two = ResolveScenePathStyleEdit(window, ids);
		EXPECT_EQ(two.resolved, 2u);
		EXPECT_TRUE(two.mixedWidth);
		EXPECT_FALSE(two.mixedAlpha);
	}

	TEST(ScenePathEditorWidgetTests, ResolvesAndAppliesMixedRibbonNormals)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		window.paths->Store().MutateStyle(ids[0], [](ScenePath &path) {
			path.style.profile = StrokeProfile::Flat;
			path.style.ribbonNormal = glm::vec3(0.0f, 1.0f, 0.0f);
		});
		window.paths->Store().MutateStyle(ids[1], [](ScenePath &path) {
			path.style.profile = StrokeProfile::Flat;
			path.style.ribbonNormal = glm::vec3(1.0f, 0.0f, 0.0f);
		});
		const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, ids);
		EXPECT_TRUE(state.mixedRibbonNormal);
		ScenePathStyleEdit edit = state.values;
		edit.ribbonNormal = glm::vec3(0.0f, 0.0f, 1.0f);
		EXPECT_EQ(ApplyScenePathStyleEdit(window, ids, edit), 2u);
		for (const SceneObjectId id : ids)
			EXPECT_EQ(window.paths->Store().Find(id)->style.ribbonNormal, edit.ribbonNormal);
	}

	TEST(ScenePathEditorWidgetTests, ApplyWritesEveryLiveSelectionAndOneUnknownIsIgnored)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ScenePathStyleEdit edit;
		edit.width = 0.3f;
		edit.alpha = 0.4f;
		EXPECT_EQ(ApplyScenePathStyleEdit(window, {ids[0], ids[1], SceneObjectId{999}}, edit), 2u);
		EXPECT_FLOAT_EQ(window.paths->Store().Find(ids[0])->style.width, 0.3f);
		EXPECT_FLOAT_EQ(window.paths->Store().Find(ids[1])->style.alpha, 0.4f);
	}

	TEST(ScenePathEditorWidgetTests, ApplyWritesAllStyleComboValuesToEverySelectedPath)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ScenePathStyleEdit edit;
		edit.profile = StrokeProfile::CameraFacing;
		edit.startDecoration = PathDecorationKind::Bar;
		edit.endDecoration = PathDecorationKind::Diamond;
		edit.depthMode = PathDepthMode::AlwaysOnTop;

		EXPECT_EQ(ApplyScenePathStyleEdit(window, ids, edit), 2u);
		for (const SceneObjectId id : ids)
		{
			const ScenePath *path = window.paths->Store().Find(id);
			ASSERT_NE(path, nullptr);
			EXPECT_EQ(path->style.profile, StrokeProfile::CameraFacing);
			EXPECT_EQ(path->style.startDecoration.kind, PathDecorationKind::Bar);
			EXPECT_EQ(path->style.endDecoration.kind, PathDecorationKind::Diamond);
			EXPECT_EQ(path->style.depthMode, PathDepthMode::AlwaysOnTop);
		}
	}

	TEST(ScenePathEditorWidgetTests, DisplayNameUsesNameOrStoreIndex)
	{
		RendererWindowState window;
		AddPaths(window);
		const ScenePath *named = window.paths->Store().At(0);
		ASSERT_NE(named, nullptr);
		EXPECT_EQ(ScenePathDisplayName(*named, 0), "First");
		ScenePath unnamed = *window.paths->Store().At(1);
		unnamed.name.clear();
		EXPECT_EQ(ScenePathDisplayName(unnamed, 4), "Path #4");
	}

	TEST(ScenePathEditorWidgetTests, RenameUnknownFailsAndLiveRenameChangesName)
	{
		RendererWindowState window;
		AddPaths(window);
		const SceneObjectId id = window.paths->Store().Ids().front();
		EXPECT_TRUE(RenameScenePath(window, id, "Renamed"));
		EXPECT_EQ(window.paths->Store().Find(id)->name, "Renamed");
		EXPECT_FALSE(RenameScenePath(window, SceneObjectId{999}, "Nope"));
	}
}
