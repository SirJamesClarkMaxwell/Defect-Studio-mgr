#include <gtest/gtest.h>

#include "Renderer/Scene/SceneVisibility.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// One of every kind the outliner lists, each with a stable id, so a test can select one kind
		// and assert the others were left alone.
		RendererWindowState MakeSceneWithOneOfEveryKind()
		{
			RendererWindowState window;

			RendererWindowState::PinnedMeasurement pin;
			pin.id = SceneObjectId{1};
			window.pinnedMeasurements = {pin};

			RendererWindowState::FreeLabel label;
			label.id = SceneObjectId{2};
			window.freeLabels = {label};

			ScenePath path;
			path.id = SceneObjectId{3};
			EXPECT_TRUE(SceneSystem::EnsurePathSystem(window).Store().Insert(path));

			RendererWindowState::SceneOrbital orbital;
			orbital.id = SceneObjectId{4};
			window.sceneOrbitals = {orbital};

			RendererWindowState::ScenePlane plane;
			plane.id = SceneObjectId{5};
			window.scenePlanes = {plane};

			return window;
		}

		void SelectEveryKind(RendererWindowState &window)
		{
			window.selectedPinnedMeasurements = {window.pinnedMeasurements.front().id};
			window.selectedFreeLabels = {window.freeLabels.front().id};
			window.selectedScenePaths = {window.paths->Store().At(0)->id};
			window.selectedSceneOrbitals = {window.sceneOrbitals.front().id};
			window.selectedScenePlanes = {window.scenePlanes.front().id};
		}

		bool EveryKindIsVisible(const RendererWindowState &window)
		{
			return window.pinnedMeasurements.front().visible && window.freeLabels.front().visible &&
				window.paths->Store().At(0)->visible && window.sceneOrbitals.front().visible &&
				window.scenePlanes.front().visible;
		}

		bool EveryKindIsRenderable(const RendererWindowState &window)
		{
			return window.pinnedMeasurements.front().renderable && window.freeLabels.front().renderable &&
				window.paths->Store().At(0)->renderable && window.sceneOrbitals.front().renderable &&
				window.scenePlanes.front().renderable;
		}
	} // namespace

	TEST(SceneVisibilityTests, NothingSelectedMeansNoSceneObjectSelection)
	{
		const RendererWindowState window = MakeSceneWithOneOfEveryKind();

		EXPECT_FALSE(AnySceneObjectSelected(window));
	}

	TEST(SceneVisibilityTests, EverySelectableKindCountsAsASceneObjectSelection)
	{
		// RendererWindowState is non-copyable, so each kind gets its own freshly built scene.
		RendererWindowState atoms = MakeSceneWithOneOfEveryKind();
		atoms.selectedAtomIndices = {0};
		EXPECT_TRUE(AnySceneObjectSelected(atoms));

		RendererWindowState pins = MakeSceneWithOneOfEveryKind();
		pins.selectedPinnedMeasurements = {SceneObjectId{1}};
		EXPECT_TRUE(AnySceneObjectSelected(pins));

		RendererWindowState labels = MakeSceneWithOneOfEveryKind();
		labels.selectedFreeLabels = {SceneObjectId{2}};
		EXPECT_TRUE(AnySceneObjectSelected(labels));

		RendererWindowState paths = MakeSceneWithOneOfEveryKind();
		paths.selectedScenePaths = {SceneObjectId{3}};
		EXPECT_TRUE(AnySceneObjectSelected(paths));

		RendererWindowState orbitals = MakeSceneWithOneOfEveryKind();
		orbitals.selectedSceneOrbitals = {SceneObjectId{4}};
		EXPECT_TRUE(AnySceneObjectSelected(orbitals));

		RendererWindowState planes = MakeSceneWithOneOfEveryKind();
		planes.selectedScenePlanes = {SceneObjectId{5}};
		EXPECT_TRUE(AnySceneObjectSelected(planes));
	}

	TEST(SceneVisibilityTests, HidingTheSelectionHidesEverySelectedKind)
	{
		RendererWindowState window = MakeSceneWithOneOfEveryKind();
		SelectEveryKind(window);

		SetSelectedSceneObjectsVisible(window, false);

		EXPECT_FALSE(window.pinnedMeasurements.front().visible);
		EXPECT_FALSE(window.freeLabels.front().visible);
		EXPECT_FALSE(window.paths->Store().At(0)->visible);
		EXPECT_FALSE(window.sceneOrbitals.front().visible);
		EXPECT_FALSE(window.scenePlanes.front().visible);
	}

	TEST(SceneVisibilityTests, HidingTheSelectionLeavesUnselectedObjectsAlone)
	{
		RendererWindowState window = MakeSceneWithOneOfEveryKind();
		ScenePath other;
		other.id = SceneObjectId{30};
		ASSERT_TRUE(window.paths->Store().Insert(other));
		window.selectedScenePaths = {SceneObjectId{3}};

		SetSelectedSceneObjectsVisible(window, false);

		EXPECT_FALSE(window.paths->Store().At(0)->visible);
		EXPECT_TRUE(window.paths->Store().At(1)->visible);
		EXPECT_TRUE(window.freeLabels.front().visible);
		EXPECT_TRUE(window.sceneOrbitals.front().visible);
	}

	TEST(SceneVisibilityTests, HidingNeverTouchesTheCameraColumn)
	{
		RendererWindowState window = MakeSceneWithOneOfEveryKind();
		SelectEveryKind(window);

		SetSelectedSceneObjectsVisible(window, false);

		EXPECT_TRUE(EveryKindIsRenderable(window));
	}

	TEST(SceneVisibilityTests, ShowingTheSelectionBringsItBack)
	{
		RendererWindowState window = MakeSceneWithOneOfEveryKind();
		SelectEveryKind(window);
		SetSelectedSceneObjectsVisible(window, false);

		SetSelectedSceneObjectsVisible(window, true);

		EXPECT_TRUE(EveryKindIsVisible(window));
	}

	TEST(SceneVisibilityTests, ShowAllRevealsEveryKindIncludingUnselectedOnes)
	{
		RendererWindowState window = MakeSceneWithOneOfEveryKind();
		window.pinnedMeasurements.front().visible = false;
		window.freeLabels.front().visible = false;
		window.paths->Store().MutateStyle(SceneObjectId{3}, [](ScenePath &path) { path.visible = false; });
		window.sceneOrbitals.front().visible = false;
		window.scenePlanes.front().visible = false;

		ShowAllSceneObjects(window);

		EXPECT_TRUE(EveryKindIsVisible(window));
	}

	TEST(SceneVisibilityTests, ShowAllLeavesTheCameraColumnAlone)
	{
		RendererWindowState window = MakeSceneWithOneOfEveryKind();
		window.paths->Store().MutateStyle(SceneObjectId{3}, [](ScenePath &path) { path.renderable = false; });
		window.paths->Store().MutateStyle(SceneObjectId{3}, [](ScenePath &path) { path.visible = false; });

		ShowAllSceneObjects(window);

		EXPECT_TRUE(window.paths->Store().At(0)->visible);
		EXPECT_FALSE(window.paths->Store().At(0)->renderable);
	}

	TEST(SceneVisibilityTests, RenderPassVisibilityFollowsTheCameraColumnAlone)
	{
		RendererWindowState window = MakeSceneWithOneOfEveryKind();
		// Hidden in the viewport but still in the render, and the other way round - the two columns
		// are independent, so neither may leak into the other.
		window.paths->Store().MutateStyle(SceneObjectId{3}, [](ScenePath &path) { path.visible = false; });
		window.paths->Store().MutateStyle(SceneObjectId{3}, [](ScenePath &path) { path.renderable = true; });
		window.sceneOrbitals.front().visible = true;
		window.sceneOrbitals.front().renderable = false;

		RendererAtomData atom;
		atom.visible = false;
		atom.renderable = true;
		window.structure.atoms = {atom};
		RendererBondData bond;
		bond.visible = true;
		bond.renderable = false;
		window.structure.bonds = {bond};

		ApplyRenderPassVisibility(window);

		EXPECT_TRUE(window.paths->Store().At(0)->visible);
		EXPECT_FALSE(window.sceneOrbitals.front().visible);
		EXPECT_TRUE(window.structure.atoms.front().visible);
		EXPECT_FALSE(window.structure.bonds.front().visible);
	}

	TEST(SceneVisibilityTests, RenderPassVisibilityIsIdempotent)
	{
		RendererWindowState window = MakeSceneWithOneOfEveryKind();
		window.scenePlanes.front().renderable = false;

		ApplyRenderPassVisibility(window);
		ApplyRenderPassVisibility(window);

		EXPECT_FALSE(window.scenePlanes.front().visible);
		EXPECT_TRUE(window.freeLabels.front().visible);
	}
} // namespace DefectStudio::Tests
