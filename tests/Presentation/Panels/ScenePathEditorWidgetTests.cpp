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
		ASSERT_GE(ids.size(), 2u);
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
		ASSERT_GE(ids.size(), 2u);
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
		{
			const ScenePath *path = window.paths->Store().Find(id);
			ASSERT_NE(path, nullptr);
			EXPECT_EQ(path->style.ribbonNormal, edit.ribbonNormal);
		}
	}

	TEST(ScenePathEditorWidgetTests, ApplyWritesEveryLiveSelectionAndOneUnknownIsIgnored)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		ScenePathStyleEdit edit;
		edit.width = 0.3f;
		edit.alpha = 0.4f;
		EXPECT_EQ(ApplyScenePathStyleEdit(window, {ids[0], ids[1], SceneObjectId{999}}, edit), 2u);
		const ScenePath *first = window.paths->Store().Find(ids[0]);
		const ScenePath *second = window.paths->Store().Find(ids[1]);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		EXPECT_FLOAT_EQ(first->style.width, 0.3f);
		EXPECT_FLOAT_EQ(second->style.alpha, 0.4f);
	}

	TEST(ScenePathEditorWidgetTests, ApplyWritesAllStyleComboValuesToEverySelectedPath)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		ScenePathStyleEdit edit;
		edit.profile = StrokeProfile::CameraFacing;
		edit.startDecoration = {PathDecorationKind::Bar, 2.0f, 0.5f, false};
		edit.endDecoration = {PathDecorationKind::Diamond, 1.5f, 2.5f, true};
		edit.depthMode = PathDepthMode::AlwaysOnTop;

		EXPECT_EQ(ApplyScenePathStyleEdit(window, ids, edit), 2u);
		for (const SceneObjectId id : ids)
		{
			const ScenePath *path = window.paths->Store().Find(id);
			ASSERT_NE(path, nullptr);
			EXPECT_EQ(path->style.profile, StrokeProfile::CameraFacing);
			EXPECT_EQ(path->style.startDecoration.kind, PathDecorationKind::Bar);
			EXPECT_FLOAT_EQ(path->style.startDecoration.lengthScale, 2.0f);
			EXPECT_FLOAT_EQ(path->style.startDecoration.widthScale, 0.5f);
			EXPECT_FALSE(path->style.startDecoration.filled);
			EXPECT_EQ(path->style.endDecoration.kind, PathDecorationKind::Diamond);
			EXPECT_FLOAT_EQ(path->style.endDecoration.lengthScale, 1.5f);
			EXPECT_FLOAT_EQ(path->style.endDecoration.widthScale, 2.5f);
			EXPECT_TRUE(path->style.endDecoration.filled);
			EXPECT_EQ(path->style.depthMode, PathDepthMode::AlwaysOnTop);
		}
	}

	TEST(ScenePathEditorWidgetTests, ResolvesWholeEndpointDecorationsAndMarksAllFieldDifferencesMixed)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		const PathEndpointDecoration start{PathDecorationKind::Latex, 2.0f, 0.75f, false};
		const PathEndpointDecoration end{PathDecorationKind::Kite, 1.25f, 1.5f, true};
		for (const SceneObjectId id : ids)
			window.paths->Store().MutateStyle(id, [&](ScenePath &path) {
				path.style.startDecoration = start;
				path.style.endDecoration = end;
			});

		const ScenePathStyleEditState resolved = ResolveScenePathStyleEdit(window, ids);
		EXPECT_EQ(resolved.values.startDecoration.kind, start.kind);
		EXPECT_FLOAT_EQ(resolved.values.startDecoration.lengthScale, start.lengthScale);
		EXPECT_FLOAT_EQ(resolved.values.startDecoration.widthScale, start.widthScale);
		EXPECT_EQ(resolved.values.startDecoration.filled, start.filled);
		EXPECT_EQ(resolved.values.endDecoration.kind, end.kind);
		EXPECT_FLOAT_EQ(resolved.values.endDecoration.lengthScale, end.lengthScale);
		EXPECT_FLOAT_EQ(resolved.values.endDecoration.widthScale, end.widthScale);
		EXPECT_EQ(resolved.values.endDecoration.filled, end.filled);
		EXPECT_FALSE(resolved.mixedStartDecoration);
		EXPECT_FALSE(resolved.mixedEndDecoration);

		const auto expectStartMixed = [&](const auto &change) {
			for (const SceneObjectId id : ids)
				window.paths->Store().MutateStyle(id, [&](ScenePath &path) { path.style.startDecoration = start; });
			window.paths->Store().MutateStyle(ids[1], [&](ScenePath &path) { change(path.style.startDecoration); });
			const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, ids);
			EXPECT_TRUE(state.mixedStartDecoration);
		};
		expectStartMixed([](PathEndpointDecoration &decoration) { decoration.kind = PathDecorationKind::Arrow; });
		expectStartMixed([](PathEndpointDecoration &decoration) { decoration.lengthScale = 3.0f; });
		expectStartMixed([](PathEndpointDecoration &decoration) { decoration.widthScale = 1.25f; });
		expectStartMixed([](PathEndpointDecoration &decoration) { decoration.filled = true; });

		const auto expectEndMixed = [&](const auto &change) {
			for (const SceneObjectId id : ids)
				window.paths->Store().MutateStyle(id, [&](ScenePath &path) { path.style.endDecoration = end; });
			window.paths->Store().MutateStyle(ids[1], [&](ScenePath &path) { change(path.style.endDecoration); });
			const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, ids);
			EXPECT_TRUE(state.mixedEndDecoration);
		};
		expectEndMixed([](PathEndpointDecoration &decoration) { decoration.kind = PathDecorationKind::Arrow; });
		expectEndMixed([](PathEndpointDecoration &decoration) { decoration.lengthScale = 2.0f; });
		expectEndMixed([](PathEndpointDecoration &decoration) { decoration.widthScale = 0.5f; });
		expectEndMixed([](PathEndpointDecoration &decoration) { decoration.filled = false; });
	}

	TEST(ScenePathEditorWidgetTests, DisplayNameUsesNameOrStoreIndex)
	{
		RendererWindowState window;
		AddPaths(window);
		ASSERT_GE(window.paths->Store().Size(), 2u);
		const ScenePath *named = window.paths->Store().At(0);
		ASSERT_NE(named, nullptr);
		EXPECT_EQ(ScenePathDisplayName(*named, 0), "First");
		const ScenePath *second = window.paths->Store().At(1);
		ASSERT_NE(second, nullptr);
		ScenePath unnamed = *second;
		unnamed.name.clear();
		EXPECT_EQ(ScenePathDisplayName(unnamed, 4), "Path #4");
	}

	TEST(ScenePathEditorWidgetTests, RenameUnknownFailsAndLiveRenameChangesName)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_FALSE(ids.empty());
		const SceneObjectId id = ids.front();
		EXPECT_TRUE(RenameScenePath(window, id, "Renamed"));
		const ScenePath *renamed = window.paths->Store().Find(id);
		ASSERT_NE(renamed, nullptr);
		EXPECT_EQ(renamed->name, "Renamed");
		EXPECT_FALSE(RenameScenePath(window, SceneObjectId{999}, "Nope"));
	}
}
