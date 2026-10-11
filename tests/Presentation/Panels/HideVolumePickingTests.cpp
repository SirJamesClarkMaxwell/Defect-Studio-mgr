#include "Core/dspch.hpp"

#include <gtest/gtest.h>
#include <algorithm>

#include "Presentation/Panels/SceneHideVolumeEditor.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Presentation/Panels/ViewportVacancySelection.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneHideVolumeSilhouette.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"
#include "IO/SceneObjectsIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		void Prepare(RendererWindowState &window)
		{
			window.pickLabels = window.pickAtoms = window.pickBonds = true;
			window.viewportSize = glm::vec2(800, 600);
			window.camera = CreateUnique<RendererViewCamera>();
			window.camera->SetViewport(800, 600);
			window.camera->SetOrbitState(glm::vec3(0), 10, 0, 0); // Looking along +Y, X right, Z up.
			window.structure.lattice = glm::mat3(10);
		}

		glm::vec2 Project(const RendererWindowState &window, glm::vec3 world, glm::vec2 imageSize)
		{
			const auto point = SelectionHitTest::ProjectToScreen(window.camera->ProjectionMatrix() * window.camera->ViewMatrix(),
				imageSize, world + window.viewOffset);
			EXPECT_TRUE(point);
			return point.value_or(glm::vec2(-1000));
		}

		SceneObjectId AddOutline(RendererWindowState &window, HideVolumeKind kind = HideVolumeKind::Box)
		{
			SceneHideVolume volume;
			volume.id = window.sceneRegistry.AllocateObjectId();
			volume.kind = kind;
			volume.active = false;
			window.sceneHideVolumes.push_back(volume);
			return volume.id;
		}
	}

	TEST(HideVolumePickingTests, NewAndPersistedVolumesDefaultToOutlineOpacity)
	{
		EXPECT_FLOAT_EQ(SceneHideVolume{}.alpha, 0.75f);
		EXPECT_FLOAT_EQ(PersistedSceneHideVolume{}.alpha, 0.75f);
		EXPECT_EQ(SceneHideVolume{}.color, glm::vec3(0, 1, 1));
		EXPECT_EQ(PersistedSceneHideVolume{}.color, glm::vec3(0, 1, 1));
	}

	TEST(HideVolumePickingTests, SilhouetteIsPickableAwayFromAllThreeGreatCircles)
	{
		for (const auto projection : {CameraProjection::Orthographic, CameraProjection::Perspective})
		{
			RendererWindowState window;
			Prepare(window);
			window.camera->SetProjection(projection);
			window.camera->SetOrbitState(glm::vec3(0), 6, 0.7f, 0.5f);
			window.viewOffset = glm::vec3(0.3f, 0.2f, -0.1f);
			const auto id = AddOutline(window, HideVolumeKind::Sphere);
			const auto silhouette = BuildHideVolumeSilhouette(window.sceneHideVolumes.front(), window.structure,
				*window.camera, window.viewOffset);
			ASSERT_FALSE(silhouette.empty());
			const auto greatCircles = BuildHideVolumeEdges(window.sceneHideVolumes.front(), window.structure);
			int checked = 0;
			for (const auto &point : silhouette)
			{
				const auto screen = Project(window, point, window.viewportSize);
				float closest = 1000;
				for (std::size_t segment = 0; segment < greatCircles.size(); segment += 2)
					closest = std::min(closest, SelectionHitTest::DistancePointToSegment(screen,
						Project(window, greatCircles[segment], window.viewportSize),
						Project(window, greatCircles[segment + 1], window.viewportSize)));
				if (closest <= 8) continue;
				EXPECT_EQ(PickSceneHideVolumeOutline(window, screen, window.viewportSize), id);
				++checked;
			}
			EXPECT_GT(checked, 0);
		}
	}

	TEST(HideVolumePickingTests, HiddenAnchorsExcludeOffsetLabelsFromRectangleAndCircleSelection)
	{
		RendererWindowState window;
		Prepare(window);
		window.structure.atoms = {{"C", {0, 0, 0}}};
		window.structure.vacancies.resize(1);
		RendererWindowState::FreeLabel label;
		label.worldPosition = glm::vec3(2, 0, 0); // Label lies outside the cut around its anchor.
		window.freeLabels = {label};
		const auto screen = Project(window, label.worldPosition, window.viewportSize);
		for (const bool vacancyAnchor : {false, true})
		{
			auto &anchored = window.freeLabels[0];
			anchored.anchorAtom.reset();
			anchored.anchorVacancy.reset();
			if (vacancyAnchor) anchored.anchorVacancy = 0;
			else anchored.anchorAtom = 0;
			window.structure.atoms[0].visible = false;
			window.structure.vacancies[0].hidden = true;
			EXPECT_TRUE(HitTestRectFreeLabels(window, screen - glm::vec2(5), screen + glm::vec2(5)).empty());
			EXPECT_TRUE(HitTestCircleFreeLabels(window, screen, 5).empty());
			window.structure.atoms[0].visible = true;
			window.structure.vacancies[0].hidden = false;
			EXPECT_EQ(HitTestCircleFreeLabels(window, screen, 5), (std::vector<std::size_t>{0}));
			if (vacancyAnchor)
			{
				window.sceneHideVolumes.emplace_back();
				window.sceneHideVolumes[0].visible = false;
				EXPECT_TRUE(HitTestRectFreeLabels(window, screen - glm::vec2(5), screen + glm::vec2(5)).empty());
				EXPECT_TRUE(HitTestCircleFreeLabels(window, screen, 5).empty());
			}
		}
	}

	TEST(HideVolumePickingTests, SixPixelToleranceUsesTheDisplayedImageForEveryShape)
	{
		for (const auto projection : {CameraProjection::Orthographic, CameraProjection::Perspective})
			for (const auto kind : {HideVolumeKind::Sphere, HideVolumeKind::Box, HideVolumeKind::Cylinder})
			{
				RendererWindowState window;
				Prepare(window);
				window.camera->SetProjection(projection);
				const auto id = AddOutline(window, kind);
				for (const auto size : {glm::vec2(400, 300), glm::vec2(1200, 900)})
				{
					const glm::vec3 worldEdge = kind == HideVolumeKind::Box ? glm::vec3(1, -1, 0) : glm::vec3(1, 0, 0);
					const auto edge = Project(window, worldEdge, size);
					EXPECT_EQ(PickSceneHideVolumeOutline(window, edge, size), id);
					EXPECT_EQ(PickSceneHideVolumeOutline(window, edge + glm::vec2(5, 0), size), id);
					EXPECT_FALSE(PickSceneHideVolumeOutline(window, edge + glm::vec2(8, 0), size));
				}
			}
	}

	TEST(HideVolumePickingTests, EyeControlsPickingWhileActiveAndCameraDoNot)
	{
		RendererWindowState window;
		Prepare(window);
		const auto id = AddOutline(window);
		const auto edge = Project(window, glm::vec3(1, -1, 0), window.viewportSize);
		EXPECT_EQ(PickSceneHideVolumeOutline(window, edge, window.viewportSize), id);
		window.sceneHideVolumes.front().active = true;
		window.sceneHideVolumes.front().renderable = true;
		EXPECT_EQ(PickSceneHideVolumeOutline(window, edge, window.viewportSize), id);
		window.sceneHideVolumes.front().visible = false;
		EXPECT_FALSE(PickSceneHideVolumeOutline(window, edge, window.viewportSize));
	}

	TEST(HideVolumePickingTests, AtomsAndBondsWinEvenWhenTheVolumeEdgeIsCloser)
	{
		RendererWindowState window;
		Prepare(window);
		const auto id = AddOutline(window);
		const auto edge = Project(window, glm::vec3(1, -1, 0), window.viewportSize);
		window.structure.atoms = {{"C", {1, 2, 0}}};
		EXPECT_FALSE(PickSceneHideVolumeOutline(window, edge, window.viewportSize));
		window.structure.atoms.front().visible = false;
		EXPECT_EQ(PickSceneHideVolumeOutline(window, edge, window.viewportSize), id);
		window.structure.atoms = {{"C", {1, 2, -2}}, {"C", {1, 2, 2}}};
		RendererBondData bond;
		bond.firstAtomIndex = 0;
		bond.secondAtomIndex = 1;
		window.structure.bonds.push_back(bond);
		EXPECT_FALSE(PickSceneHideVolumeOutline(window, edge, window.viewportSize));
		window.structure.bonds.front().visible = false;
		EXPECT_EQ(PickSceneHideVolumeOutline(window, edge, window.viewportSize), id);
	}

	TEST(HideVolumePickingTests, OverlappingEdgesChooseTheNearerVolume)
	{
		RendererWindowState window;
		Prepare(window);
		AddOutline(window);
		window.sceneHideVolumes.front().center.y = 2;
		const auto nearer = AddOutline(window);
		const auto edge = Project(window, glm::vec3(1, -1, 0), window.viewportSize);
		EXPECT_EQ(PickSceneHideVolumeOutline(window, edge, window.viewportSize), nearer);
	}

	TEST(HideVolumePickingTests, FractionalOutlineAndViewOffsetUseTheDrawnGeometry)
	{
		RendererWindowState window;
		Prepare(window);
		const auto id = AddOutline(window);
		window.sceneHideVolumes.front().frame = HideVolumeFrame::Fractional;
		window.sceneHideVolumes.front().halfExtents = glm::vec3(0.1f);
		window.viewOffset = glm::vec3(1, 0, 0.5f);
		const auto edge = Project(window, glm::vec3(1, -1, 0), window.viewportSize);
		EXPECT_EQ(PickSceneHideVolumeOutline(window, edge, window.viewportSize), id);
		const auto ray = ComputeViewportPickRay(window, edge, window.viewportSize);
		ASSERT_TRUE(ray);
		window.structure.atoms = {{"C", {1, 0, 0}}};
		EXPECT_EQ(PickAtomAlongRay(window, ray->origin, ray->direction), 0u);
		EXPECT_LT(glm::distance(ComputeViewportWorldPosition(window, edge.x, edge.y), glm::vec3(1, 0, 0)), 1e-4f);
	}

	TEST(HideVolumePickingTests, SegmentsCrossingTheNearPlaneRemainPickable)
	{
		RendererWindowState window;
		Prepare(window);
		window.camera->SetProjection(CameraProjection::Perspective);
		const auto id = AddOutline(window);
		auto &volume = window.sceneHideVolumes.front();
		volume.center = glm::vec3(0, -9.99f, 0);
		volume.halfExtents = glm::vec3(0.001f, 0.1f, 0.001f);
		const auto visibleEdge = Project(window, glm::vec3(0.001f, -9.95f, 0.001f), window.viewportSize);
		EXPECT_EQ(PickSceneHideVolumeOutline(window, visibleEdge, window.viewportSize), id);
		volume.center.y = -11;
		EXPECT_FALSE(PickSceneHideVolumeOutline(window, visibleEdge, window.viewportSize));
	}

	TEST(HideVolumePickingTests, CoveredVacanciesAndLabelsCannotBePickedEvenWithAnOldQuad)
	{
		RendererWindowState window;
		Prepare(window);
		window.showVacancies = true;
		window.structure.vacancies.push_back({glm::vec3(0)});
		window.structure.atoms = {{"C", {-1, 0, 0}}, {"C", {1, 0, 0}}};
		RendererWindowState::PinnedMeasurement pin;
		pin.atomIndices = {0, 1};
		window.pinnedMeasurements.push_back(pin);
		RendererWindowState::FreeLabel label;
		label.worldPosition = glm::vec3(0);
		window.freeLabels.push_back(label);
		LabelPickQuad quad;
		quad.valid = true;
		quad.centre = glm::vec3(0);
		quad.min = glm::vec2(-0.2f);
		quad.max = glm::vec2(0.2f);
		window.labelPickQuads.pinned.push_back(quad);
		window.labelPickQuads.free.push_back(quad);
		const glm::vec2 mouse = Project(window, glm::vec3(0), window.viewportSize);
		glm::vec3 position;
		EXPECT_TRUE(ResolvePickablePinnedMeasurementPosition(window, 0, position));
		EXPECT_EQ(PickVacancyAlongRay(window, glm::vec3(0, -10, 0), glm::vec3(0, 1, 0)), 0u);
		EXPECT_EQ(MouseInLabelQuad(window, window.labelPickQuads.free, 0, ImVec2(0, 0), ImVec2(800, 600), mouse), true);
		AddOutline(window, HideVolumeKind::Sphere);
		window.sceneHideVolumes.front().active = true;
		window.sceneHideVolumes.front().visible = false;
		EXPECT_FALSE(PickVacancyAlongRay(window, glm::vec3(0, -10, 0), glm::vec3(0, 1, 0)));
		EXPECT_FALSE(ResolvePickablePinnedMeasurementPosition(window, 0, position));
		EXPECT_EQ(MouseInLabelQuad(window, window.labelPickQuads.free, 0, ImVec2(0, 0), ImVec2(800, 600), mouse), false);
		EXPECT_TRUE(HitTestRectFreeLabels(window, mouse - glm::vec2(5), mouse + glm::vec2(5)).empty());
		EXPECT_TRUE(HitTestCirclePinnedMeasurements(window, mouse, 5).empty());
		window.sceneHideVolumes.front().active = false;
		EXPECT_TRUE(ResolvePickablePinnedMeasurementPosition(window, 0, position));
		EXPECT_EQ(PickVacancyAlongRay(window, glm::vec3(0, -10, 0), glm::vec3(0, 1, 0)), 0u);
	}
}
