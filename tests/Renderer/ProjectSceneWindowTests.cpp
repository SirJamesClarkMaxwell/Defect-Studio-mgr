#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>
#include <variant>
#include <vector>

#include "Core/Undo/UndoStack.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "Renderer/ProjectSceneWindow.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStartupConfig EmptyRendererConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}

		// A two-node line path with an Arrow end decoration and a bevelled Flat ribbon - the kind of
		// object the bevel gallery is made of.
		PersistedScenePath GalleryPath(const std::string &key, const float y)
		{
			PersistedScenePath path;
			path.persistKey = key;
			path.nodes = {{{-1.0f, y, 0.0f}}, {{1.0f, y, 0.0f}}};
			path.segments.resize(1);
			path.segments[0].kind = PersistedPathSegmentKind::Line;
			path.style.profile = "Flat";
			path.style.ribbonThickness = 0.2f;
			path.style.ribbonBevel = 0.05f;
			path.style.ribbonBevelSegments = 3;
			path.style.endDecoration = "Arrow";
			return path;
		}

		std::size_t CountProjectSceneWindows(RendererLayer &layer)
		{
			const auto &windows = layer.GetWindows();
			return static_cast<std::size_t>(std::count_if(windows.begin(), windows.end(),
				[](const RendererWindowState &window) { return window.isProjectScene; }));
		}

		std::size_t PathCount(const RendererWindowState &window)
		{
			return window.paths == nullptr ? 0u : window.paths->Store().Size();
		}
	} // namespace

	class ProjectSceneWindowTests : public testing::Test
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
		std::vector<StructuredError> warnings;
	};

	// Criterion 6: one project, one project-scene window, however often it is reset.
	TEST_F(ProjectSceneWindowTests, ResetCreatesTheWindowOnceAndReusesIt)
	{
		EXPECT_EQ(FindProjectSceneWindow(renderer), nullptr);

		RendererWindowState &first = ResetProjectSceneWindow(renderer, {}, warnings);
		EXPECT_TRUE(first.isProjectScene);
		EXPECT_EQ(first.windowId, kProjectSceneWindowId);
		EXPECT_TRUE(first.structureId.is_nil());

		ResetProjectSceneWindow(renderer, {GalleryPath("a", 0.0f)}, warnings);
		ResetProjectSceneWindow(renderer, {}, warnings);
		EXPECT_EQ(CountProjectSceneWindows(renderer), 1u);
		EXPECT_EQ(renderer.GetWindows().size(), 1u);
	}

	// Criterion 4: the second project's scene never shows the first project's objects.
	TEST_F(ProjectSceneWindowTests, ResetReplacesThePreviousProjectsObjects)
	{
		ResetProjectSceneWindow(renderer, {GalleryPath("a", 0.0f), GalleryPath("b", 1.0f)}, warnings);
		RendererWindowState *window = FindProjectSceneWindow(renderer);
		ASSERT_NE(window, nullptr);
		EXPECT_EQ(PathCount(*window), 2u);
		window->selectedScenePaths = {window->paths->Store().At(0)->id};

		ResetProjectSceneWindow(renderer, {GalleryPath("c", 2.0f)}, warnings);

		window = FindProjectSceneWindow(renderer);
		ASSERT_NE(window, nullptr);
		ASSERT_EQ(PathCount(*window), 1u);
		EXPECT_EQ(window->paths->Store().At(0)->persistKey, "c");
		EXPECT_TRUE(window->selectedScenePaths.empty());
		EXPECT_FALSE(window->pathEdit.IsActive());
		EXPECT_TRUE(warnings.empty());
	}

	// Criterion 2: only the project-scene window is gathered - not an ad-hoc empty window, not a
	// structure-backed one.
	TEST_F(ProjectSceneWindowTests, GatherReadsOnlyTheProjectSceneWindow)
	{
		EXPECT_TRUE(GatherProjectSceneObjects(renderer).empty());

		ResetProjectSceneWindow(renderer, {GalleryPath("project", 0.0f)}, warnings);
		RendererWindowState adHoc;
		adHoc.windowId = "ad-hoc-empty";
		renderer.AddWindow(std::move(adHoc));
		std::vector<StructuredError> ignored;
		ApplyPersistedSceneObjects(renderer.GetWindows().back(), {GalleryPath("ad-hoc", 3.0f)}, ignored);

		const std::vector<PersistedSceneObject> gathered = GatherProjectSceneObjects(renderer);

		ASSERT_EQ(gathered.size(), 1u);
		ASSERT_TRUE(std::holds_alternative<PersistedScenePath>(gathered[0]));
		EXPECT_EQ(std::get<PersistedScenePath>(gathered[0]).persistKey, "project");
	}

	// The gallery's style survives window -> persisted form, bevel included.
	TEST_F(ProjectSceneWindowTests, GatherKeepsDecorationAndBevel)
	{
		ResetProjectSceneWindow(renderer, {GalleryPath("bevel", 0.0f)}, warnings);

		const std::vector<PersistedSceneObject> gathered = GatherProjectSceneObjects(renderer);

		ASSERT_EQ(gathered.size(), 1u);
		const auto &path = std::get<PersistedScenePath>(gathered[0]);
		EXPECT_EQ(path.style.endDecoration, "Arrow");
		EXPECT_FLOAT_EQ(path.style.ribbonBevel, 0.05f);
		EXPECT_EQ(path.style.ribbonBevelSegments, 3);
	}

	// Criterion 3 (renderer half): an undoable edit in the project scene marks it dirty; a reset
	// (load) clears it. A structure-free window that is NOT the project scene stays clean.
	TEST_F(ProjectSceneWindowTests, SceneEditMarksTheProjectSceneDirtyAndResetClearsIt)
	{
		RendererWindowState &window = ResetProjectSceneWindow(renderer, {}, warnings);
		EXPECT_FALSE(window.sceneObjectsDirty);

		PushPinnedMeasurementUndoSnapshot(window);
		EXPECT_TRUE(FindProjectSceneWindow(renderer)->sceneObjectsDirty);

		ResetProjectSceneWindow(renderer, {}, warnings);
		EXPECT_FALSE(FindProjectSceneWindow(renderer)->sceneObjectsDirty);

		RendererWindowState adHoc;
		adHoc.windowId = "ad-hoc-empty";
		renderer.AddWindow(std::move(adHoc));
		PushPinnedMeasurementUndoSnapshot(renderer.GetWindows().back());
		EXPECT_FALSE(renderer.GetWindows().back().sceneObjectsDirty);
	}
} // namespace DefectStudio::Tests
