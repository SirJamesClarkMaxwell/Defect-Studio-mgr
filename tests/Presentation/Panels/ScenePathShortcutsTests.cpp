#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneVisibility.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath MakePath(std::uint64_t id, float y = 0.0f)
		{
			ScenePath path;
			path.id = SceneObjectId{id};
			PathNode first{AllocateElementId(path), glm::vec3(-1.0f, y, 0.0f), {}};
			PathNode second{AllocateElementId(path), glm::vec3(1.0f, y, 0.0f), {}};
			path.nodes = {first, second};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			return path;
		}

		SceneObjectId AddPath(RendererWindowState &window, ScenePath path)
		{
			return SceneSystem::AppendScenePath(window, std::move(path));
		}

		RendererStartupConfig EmptyRendererConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}
	}

	TEST(ScenePathShortcutsTests, HideGuardCountsPathsAndEachExistingAnnotationKind)
	{
		RendererWindowState empty;
		EXPECT_FALSE(HasSelectedSceneObjectsForHide(empty));

		RendererWindowState pins;
		pins.selectedPinnedMeasurements = {SceneObjectId{1}};
		EXPECT_TRUE(HasSelectedSceneObjectsForHide(pins));

		RendererWindowState labels;
		labels.selectedFreeLabels = {SceneObjectId{2}};
		EXPECT_TRUE(HasSelectedSceneObjectsForHide(labels));

		RendererWindowState arrows;
		arrows.selectedSceneArrows = {SceneObjectId{3}};
		EXPECT_TRUE(HasSelectedSceneObjectsForHide(arrows));

		RendererWindowState orbitals;
		orbitals.selectedSceneOrbitals = {SceneObjectId{4}};
		EXPECT_TRUE(HasSelectedSceneObjectsForHide(orbitals));

		RendererWindowState planes;
		planes.selectedScenePlanes = {SceneObjectId{5}};
		EXPECT_TRUE(HasSelectedSceneObjectsForHide(planes));

		RendererWindowState paths;
		paths.selectedScenePaths = {SceneObjectId{6}};
		EXPECT_TRUE(HasSelectedSceneObjectsForHide(paths));
	}

	TEST(ScenePathShortcutsTests, HideAndUndoAPathSelectionAffectsOnlySelectedPaths)
	{
		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{EmptyRendererConfig()};
		renderer.BindUndoStack(undoStack);

		RendererWindowState window;
		window.windowId = "path-shortcuts-hide";
		const SceneObjectId selected = AddPath(window, MakePath(1));
		const SceneObjectId unselected = AddPath(window, MakePath(2, 2.0f));
		window.selectedScenePaths = {selected};
		renderer.AddWindow(std::move(window));
		RendererWindowState &live = renderer.GetWindows().front();

		ASSERT_TRUE(HasSelectedSceneObjectsForHide(live));
		PushPinnedMeasurementUndoSnapshot(live);
		SetSelectedSceneObjectsVisible(live, false);
		ASSERT_FALSE(live.paths->Store().Find(selected)->visible);
		EXPECT_TRUE(live.paths->Store().Find(unselected)->visible);
		ASSERT_EQ(undoStack->GetUndoDepth(), 1u);

		ASSERT_TRUE(undoStack->Undo());
		EXPECT_TRUE(live.paths->Store().Find(selected)->visible);
		EXPECT_TRUE(live.paths->Store().Find(unselected)->visible);

		SetSelectedSceneObjectsVisible(live, false);
		ShowAllSceneObjects(live);
		EXPECT_TRUE(live.paths->Store().Find(selected)->visible);
		renderer.OnDetach();
	}

	TEST(ScenePathShortcutsTests, DrawingKindResolverPreservesPrecedenceAndAddsPaths)
	{
		RendererWindowState empty;
		EXPECT_EQ(ResolveSelectedDrawingKind(empty), std::nullopt);

		RendererWindowState path;
		path.selectedScenePaths = {SceneObjectId{1}};
		EXPECT_EQ(ResolveSelectedDrawingKind(path), SceneObjectEditKind::Path);

		RendererWindowState plane;
		plane.selectedScenePlanes = {SceneObjectId{2}};
		EXPECT_EQ(ResolveSelectedDrawingKind(plane), SceneObjectEditKind::Plane);

		RendererWindowState orbital;
		orbital.selectedSceneOrbitals = {SceneObjectId{3}};
		EXPECT_EQ(ResolveSelectedDrawingKind(orbital), SceneObjectEditKind::Orbital);

		RendererWindowState arrow;
		arrow.selectedSceneArrows = {SceneObjectId{4}};
		EXPECT_EQ(ResolveSelectedDrawingKind(arrow), SceneObjectEditKind::Arrow);

		RendererWindowState mixed;
		mixed.selectedScenePaths = {SceneObjectId{1}};
		mixed.selectedScenePlanes = {SceneObjectId{2}};
		mixed.selectedSceneOrbitals = {SceneObjectId{3}};
		mixed.selectedSceneArrows = {SceneObjectId{4}};
		EXPECT_EQ(ResolveSelectedDrawingKind(mixed), SceneObjectEditKind::Arrow);
		mixed.selectedSceneArrows.clear();
		EXPECT_EQ(ResolveSelectedDrawingKind(mixed), SceneObjectEditKind::Orbital);
		mixed.selectedSceneOrbitals.clear();
		EXPECT_EQ(ResolveSelectedDrawingKind(mixed), SceneObjectEditKind::Plane);
	}

	TEST(ScenePathShortcutsTests, DeleteRemovesPathsAloneAndAlongsideArrows)
	{
		RendererWindowState pathOnly;
		const SceneObjectId pathId = AddPath(pathOnly, MakePath(1));
		pathOnly.selectedScenePaths = {pathId};
		EXPECT_TRUE(ExecuteSceneObjectEditAction(
			pathOnly, SceneObjectEditKind::Path, SceneObjectEditAction::Delete));
		EXPECT_TRUE(pathOnly.paths->Store().Empty());

		RendererWindowState mixed;
		const SceneObjectId mixedPathId = AddPath(mixed, MakePath(2));
		RendererWindowState::SceneArrow arrow;
		arrow.id = mixed.sceneRegistry.AllocateObjectId();
		mixed.sceneArrows.push_back(arrow);
		mixed.selectedScenePaths = {mixedPathId};
		mixed.selectedSceneArrows = {arrow.id};
		EXPECT_TRUE(ExecuteSceneObjectEditAction(
			mixed, SceneObjectEditKind::Path, SceneObjectEditAction::Delete));
		EXPECT_TRUE(ExecuteSceneObjectEditAction(
			mixed, SceneObjectEditKind::Arrow, SceneObjectEditAction::Delete));
		EXPECT_TRUE(mixed.paths->Store().Empty());
		EXPECT_TRUE(mixed.sceneArrows.empty());
	}
} // namespace DefectStudio::Tests
