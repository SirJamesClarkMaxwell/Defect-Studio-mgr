#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/Commands/RendererCommandRegistration.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SceneSelection.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStartupConfig EmptyConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}

		DefectFrame TurnedFrame()
		{
			return {glm::vec3(0.0f), glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1)};
		}

		void ExpectPosition(glm::vec3 actual, glm::vec3 expected)
		{
			EXPECT_NEAR(glm::length(actual - expected), 0.0f, 1e-5f);
		}

		ScenePath AddPath(RendererWindowState &window)
		{
			ScenePath path;
			path.id = window.sceneRegistry.AllocateObjectId();
			PathNode a, b;
			a.id = AllocateElementId(path);
			b.id = AllocateElementId(path);
			b.position = glm::vec3(1.0f, 0.0f, 0.0f);
			path.nodes = {a, b};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			SceneSystem::EnsurePathSystem(window).Store().Insert(path);
			return path;
		}

		RendererWindowState AllKinds()
		{
			RendererWindowState window;
			window.camera = CreateUnique<RendererViewCamera>();
			window.structure.atoms.resize(3);
			window.structure.atoms[1].cartesianPosition = glm::vec3(1,0,0);
			window.structure.atoms[2].visible = false;
			window.structure.bonds.resize(1);
			window.structure.bonds[0].firstAtomIndex = 0;
			window.structure.bonds[0].secondAtomIndex = 1;
			window.structure.vacancies.resize(2);
			window.structure.vacancies[1].hidden = true;
			window.structure.defectFrame = TurnedFrame();
			SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
			RendererWindowState::FreeLabel label;
			label.id = window.sceneRegistry.AllocateObjectId();
			window.freeLabels = {label};
			label.id = window.sceneRegistry.AllocateObjectId();
			label.visible = false;
			window.freeLabels.push_back(label);
			RendererWindowState::PinnedMeasurement pin;
			pin.id = window.sceneRegistry.AllocateObjectId();
			pin.atomIndices = {0, 1};
			window.pinnedMeasurements = {pin};
			pin.id = window.sceneRegistry.AllocateObjectId();
			pin.visible = false;
			window.pinnedMeasurements.push_back(pin);
			RendererWindowState::SceneOrbital orbital;
			orbital.id = window.sceneRegistry.AllocateObjectId();
			orbital.preset = OrbitalPreset::S;
			orbital.resolution = 12;
			window.sceneOrbitals = {orbital};
			orbital.id = window.sceneRegistry.AllocateObjectId();
			orbital.visible = false;
			window.sceneOrbitals.push_back(orbital);
			RendererWindowState::ScenePlane plane;
			plane.id = window.sceneRegistry.AllocateObjectId();
			window.scenePlanes = {plane};
			plane.id = window.sceneRegistry.AllocateObjectId();
			plane.visible = false;
			window.scenePlanes.push_back(plane);
			(void)AddPath(window);
			const auto hiddenPath = AddPath(window);
			window.paths->Store().MutateStyle(hiddenPath.id, [](ScenePath &path) { path.visible = false; });
			SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
			return window;
		}
	}

	class Task78VisibilityTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer->BindUndoStack(stack);
			registry.SetUndoStack(stack);
			RegisterRendererCommands(registry, bus, domain, renderer, styles, {});
			CrystalStructure structure;
			structure.isPeriodic = false;
			structure.atoms.push_back(AtomSite{"C", glm::vec3(0.0f), glm::vec3(0.0f), 0});
			structure.vacancies.resize(2);
			const auto record = domain->Workspace().Structures().Add(std::move(structure));
			RendererWindowState window;
			window.windowId = "task78";
			window.structure = BuildRendererStructureData(record->structure, {}, "test", styles, ToString(record->id));
			SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
			renderer->AddWindow(std::move(window));
		}
		void TearDown() override { renderer->OnDetach(); }
		Ref<UndoStack> stack = CreateRef<UndoStack>();
		Ref<EventBus> bus = CreateRef<EventBus>();
		Ref<DomainLayer> domain = CreateRef<DomainLayer>();
		Ref<RendererLayer> renderer = CreateRef<RendererLayer>(EmptyConfig());
		CommandRegistry registry;
		AtomStyleTable styles;
		RendererWindowState &Window() { return renderer->GetWindows().front(); }

		void SetFrame(std::optional<DefectFrame> frame)
		{
			CommandContext context;
			context.Set<SetDefectFramePayload>(kSetDefectFramePayloadKey,
				SetDefectFramePayload{Window().windowId, frame});
			ASSERT_TRUE(registry.Execute(CommandID{kSetDefectFrameCommandId}, std::move(context)).HasValue());
		}
	};

	TEST_F(Task78VisibilityTests, HideVacancyAtomAndLabelIsOneUndoStepAndShowAllAlsoUndoes)
	{
		auto &window = Window();
		window.selectedVacancies = {0};
		window.sceneRegistry.AtomEntityAt(0).GetComponent<SelectionComponent>().selected = true;
		SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
		RendererWindowState::FreeLabel label;
		label.id = window.sceneRegistry.AllocateObjectId();
		window.freeLabels = {label};
		window.selectedFreeLabels = {label.id};

		// Exercise the real command while the registry's reentrancy guard is active.
		auto hideSubscription = bus->Subscribe<RendererEvents::Viewport::HideSelectionRequested>(
			[&](const auto &) { renderer->ChangeSceneVisibility(window.windowId, false); });
		auto showSubscription = bus->Subscribe<RendererEvents::Viewport::ShowAllRequested>(
			[&](const auto &) { renderer->ChangeSceneVisibility(window.windowId, true); });
		ASSERT_TRUE(registry.Execute(CommandID{"renderer.selection.hide"}).HasValue());
		ASSERT_EQ(stack->GetUndoDepth(), 1u);
		EXPECT_TRUE(window.structure.vacancies[0].hidden);
		EXPECT_FALSE(window.structure.vacancies[1].hidden);
		EXPECT_TRUE(window.selectedVacancies.empty());
		EXPECT_FALSE(window.structure.atoms[0].visible);
		EXPECT_FALSE(window.freeLabels[0].visible);
		ASSERT_TRUE(stack->Undo().HasValue());
		EXPECT_FALSE(window.structure.vacancies[0].hidden);
		EXPECT_TRUE(window.structure.atoms[0].visible);
		EXPECT_TRUE(window.freeLabels[0].visible);
		ASSERT_TRUE(stack->Redo().HasValue());
		EXPECT_TRUE(window.structure.vacancies[0].hidden);
		EXPECT_FALSE(window.structure.atoms[0].visible);
		EXPECT_FALSE(window.freeLabels[0].visible);

		ASSERT_TRUE(registry.Execute(CommandID{"renderer.selection.show_all"}).HasValue());
		ASSERT_EQ(stack->GetUndoDepth(), 2u);
		EXPECT_FALSE(window.structure.vacancies[0].hidden);
		EXPECT_TRUE(window.structure.atoms[0].visible);
		EXPECT_TRUE(window.freeLabels[0].visible);
		ASSERT_TRUE(stack->Undo().HasValue());
		EXPECT_TRUE(window.structure.vacancies[0].hidden);
		EXPECT_FALSE(window.structure.atoms[0].visible);
		EXPECT_FALSE(window.freeLabels[0].visible);
	}

	TEST_F(Task78VisibilityTests, CreateDeleteUndoRedoSwitchOrientationAndEditsKeepManualChoice)
	{
		SetFrame(TurnedFrame());
		EXPECT_EQ(Window().transformOrientation, TransformOrientation::Defect);
		Window().transformOrientation = TransformOrientation::Global;
		SetFrame(TurnedFrame()); // Updating existing axes must respect the manual toggle.
		EXPECT_EQ(Window().transformOrientation, TransformOrientation::Global);
		SetFrame(std::nullopt);
		EXPECT_EQ(Window().transformOrientation, TransformOrientation::Global);
		ASSERT_TRUE(stack->Undo().HasValue());
		EXPECT_EQ(Window().transformOrientation, TransformOrientation::Defect);
		ASSERT_TRUE(stack->Redo().HasValue());
		EXPECT_EQ(Window().transformOrientation, TransformOrientation::Global);
	}

	TEST(Task78SelectionTests, AllKindsVisibleOnlyRepeatTogglesAndExplicitClearIgnoresMode)
	{
		auto window = AllKinds();
		SelectAllVisibleSceneObjects(window);
		auto atoms = window.selectedAtomIndices;
		std::sort(atoms.begin(), atoms.end()); // the ECS sync does not keep index order
		EXPECT_EQ(atoms, (std::vector<std::size_t>{0,1}));
		EXPECT_EQ(window.selectedBondIndices, (std::vector<std::size_t>{0}));
		EXPECT_EQ(window.selectedVacancies, (std::vector<std::size_t>{0}));
		EXPECT_EQ(window.selectedFreeLabels.size(), 1u);
		EXPECT_EQ(window.selectedPinnedMeasurements.size(), 1u);
		EXPECT_EQ(window.selectedSceneOrbitals.size(), 1u);
		EXPECT_EQ(window.selectedScenePlanes.size(), 1u);
		EXPECT_EQ(window.selectedScenePaths.size(), 1u);
		EXPECT_TRUE(window.defectFrameSelected);
		SelectAllVisibleSceneObjects(window);
		EXPECT_TRUE(window.selectedAtomIndices.empty());
		EXPECT_TRUE(window.selectedBondIndices.empty());
		EXPECT_TRUE(window.selectedVacancies.empty());
		EXPECT_TRUE(window.selectedSceneOrbitals.empty());
		EXPECT_TRUE(window.selectedScenePlanes.empty());
		EXPECT_TRUE(window.selectedScenePaths.empty());
		EXPECT_FALSE(window.defectFrameSelected);
		window.pickAtoms = window.pickLabels = false;
		window.pickBonds = true;
		SelectAllVisibleSceneObjects(window);
		EXPECT_TRUE(window.selectedAtomIndices.empty());
		EXPECT_TRUE(window.selectedSceneOrbitals.empty());
		EXPECT_EQ(window.selectedBondIndices.size(), 1u);
		SelectAllVisibleSceneObjects(window, true);
		EXPECT_TRUE(window.selectedBondIndices.empty());
	}

	TEST(Task78SelectionTests, RectangleAndCircleIncludeSurfacesWithCentresOutsideTheRegion)
	{
		auto window = AllKinds();
		const auto vp = window.camera->ProjectionMatrix() * window.camera->ViewMatrix();
		const auto &mesh = CachedSceneOrbitalMesh(window, window.sceneOrbitals[0], window.structure);
		ASSERT_FALSE(mesh.empty());
		// Region around a drawn vertex on a lobe, not around the orbital's centre.
		const auto screen = SelectionHitTest::ProjectToScreen(vp, window.viewportSize, mesh[0].position);
		ASSERT_TRUE(screen);
		ApplySceneDrawingRegionSelection(window, *screen - glm::vec2(2.0f), *screen + glm::vec2(2.0f), 0.0f, true, false);
		EXPECT_EQ(window.selectedSceneOrbitals, (std::vector<SceneObjectId>{window.sceneOrbitals[0].id}));
		ApplySceneDrawingRegionSelection(window, *screen, *screen, 3.0f, false, true);
		EXPECT_TRUE(window.selectedSceneOrbitals.empty());
		const auto point = SelectionHitTest::ProjectToScreen(vp, window.viewportSize,
			ScenePlaneCorners(window.scenePlanes[0])[0]);
		ASSERT_TRUE(point);
		ApplySceneDrawingRegionSelection(window, *point - glm::vec2(2.0f), *point + glm::vec2(2.0f), 0.0f, true, false);
		EXPECT_EQ(window.selectedScenePlanes.size(), 1u);
		window.pickAtoms = window.pickLabels = false;
		ApplySceneDrawingRegionSelection(window, glm::vec2(-10000.0f), glm::vec2(10000.0f), 0.0f, true, false);
		EXPECT_TRUE(window.selectedSceneOrbitals.empty());
		EXPECT_TRUE(window.selectedScenePlanes.empty());
		EXPECT_TRUE(window.selectedVacancies.empty());
	}

	TEST(Task78TransformTests, NumericDefectXMovesEachKindAndAnchoredPlaneStaysMoved)
	{
		for (int kind = 0; kind < 6; ++kind)
		{
			auto window = AllKinds();
			window.transformOrientation = TransformOrientation::Defect;
			SceneSystem::ClearStructureSelection(window.sceneRegistry, window);
			if (kind == 0) window.selectedSceneOrbitals = {window.sceneOrbitals[0].id};
			if (kind == 1)
			{
				window.selectedScenePlanes = {window.scenePlanes[0].id};
				window.scenePlanes[0].anchorAtoms = {0, 1};
			}
			if (kind == 2)
				for (const auto id : window.paths->Store().Ids())
					if (window.paths->Store().Find(id)->visible) window.selectedScenePaths.push_back(id);
			if (kind == 3) window.selectedFreeLabels = {window.freeLabels[0].id};
			if (kind == 4) window.selectedVacancies = {0};
			if (kind == 5) window.selectedAtomIndices = {0};
			const auto snapshot = CaptureSceneTransformSelection(window);
			const auto bases = SceneTransformBases(window, snapshot);
			auto session = BeginModalTransform(ModalTransformOp::Translate,
				SceneTransformOrientation(window.transformOrientation, snapshot), bases, glm::vec3(0.0f), glm::vec2(50.0f));
			session.constraint = CycleConstraint({}, 0, false, session.orientation, bases);
			session.numericText = "2";
			ModalTransformView view;
			view.view = glm::lookAt(glm::vec3(0,0,10), glm::vec3(0), glm::vec3(0,1,0));
			view.projection = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 100.0f);
			view.viewportSize = glm::vec2(100.0f);
			SceneTransformDelta delta;
			delta.spatial = EvaluateModalTransform(session, view, glm::vec2(80.0f), SnapMode::Off, {});
			ApplySceneTransformSelection(window, snapshot, delta, ModalTransformOp::Translate,
				TransformPivotMode::Median, glm::vec3(0.0f));
			const glm::vec3 expected(0,2,0);
			if (kind == 0) ExpectPosition(window.sceneOrbitals[0].centerA, expected);
			if (kind == 1)
			{
				ResolveAnchoredScenePlanes(window);
				ExpectPosition(window.scenePlanes[0].center, expected);
				EXPECT_TRUE(window.scenePlanes[0].anchorAtoms.empty());
			}
			if (kind == 2) ExpectPosition(window.paths->Store().Find(snapshot.paths[0].id)->transform.position, expected);
			if (kind == 3) ExpectPosition(window.freeLabels[0].worldPosition, expected);
			if (kind == 4) ExpectPosition(window.structure.vacancies[0].cartesianPosition, expected);
			if (kind == 5) ExpectPosition(window.structure.atoms[0].cartesianPosition, expected);
			RestoreSceneTransformSelection(window, snapshot);
			if (kind == 1) EXPECT_EQ(window.scenePlanes[0].anchorAtoms, (std::vector<std::size_t>{0,1}));
		}
	}

	TEST(Task78TransformTests, LcaoTranslationDetachesAndCancelRestoresItsAnchor)
	{
		auto window = AllKinds();
		auto &orbital = window.sceneOrbitals[0];
		RendererWindowState::SceneOrbital::LcaoComponent component;
		component.anchorAtom = 0;
		orbital.lcaoComponents = {component};
		window.selectedSceneOrbitals = {orbital.id};
		const auto snapshot = CaptureSceneTransformSelection(window);
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(0,2,0);
		ApplySceneTransformSelection(window, snapshot, delta, ModalTransformOp::Translate,
			TransformPivotMode::Median, glm::vec3(0.0f));
		ResolveAnchoredOrbitals(window);
		ExpectPosition(orbital.lcaoComponents[0].center, glm::vec3(0,2,0));
		EXPECT_EQ(orbital.lcaoComponents[0].anchorAtom, std::numeric_limits<std::size_t>::max());
		RestoreSceneTransformSelection(window, snapshot);
		EXPECT_EQ(orbital.lcaoComponents[0].anchorAtom, 0u);
	}

	TEST(Task78TransformTests, ConstrainedScaleStretchesAnOrbitalAlongDefectX)
	{
		auto window = AllKinds();
		window.transformOrientation = TransformOrientation::Defect;
		window.selectedSceneOrbitals = {window.sceneOrbitals[0].id};
		const auto snapshot = CaptureSceneTransformSelection(window);
		const auto bases = SceneTransformBases(window, snapshot);
		SceneTransformDelta delta;
		delta.scaleFactor = 2.0f;
		delta.spatial.linear = ConstrainedScaleMatrix(2.0f,
			{ConstraintKind::Axis, 0, TransformOrientation::Defect}, bases);
		ApplySceneTransformSelection(window, snapshot, delta, ModalTransformOp::Scale,
			TransformPivotMode::Median, glm::vec3(0.0f));
		ExpectPosition(window.sceneOrbitals[0].stretch, glm::vec3(1,2,1));
		RestoreSceneTransformSelection(window, snapshot);
		ExpectPosition(window.sceneOrbitals[0].stretch, glm::vec3(1.0f));
	}

	TEST(Task78SelectionTests, GlobalDrawFlagsExcludeAtomsBondsVacanciesAndAxes)
	{
		auto window = AllKinds();
		window.showAtoms = window.showBonds = window.showVacancies = window.showDefectFrame = false;
		SelectAllVisibleSceneObjects(window);
		EXPECT_TRUE(window.selectedAtomIndices.empty());
		EXPECT_TRUE(window.selectedBondIndices.empty());
		EXPECT_TRUE(window.selectedVacancies.empty());
		EXPECT_FALSE(window.defectFrameSelected);
		EXPECT_EQ(window.selectedSceneOrbitals.size(), 1u);
	}

	TEST_F(Task78VisibilityTests, HideAxesClearsSelectionAndUndoRestoresTheirVisibility)
	{
		SetFrame(TurnedFrame());
		Window().defectFrameSelected = true;
		renderer->ChangeSceneVisibility(Window().windowId, false);
		EXPECT_FALSE(Window().showDefectFrame);
		EXPECT_FALSE(Window().defectFrameSelected);
		ASSERT_TRUE(stack->Undo().HasValue());
		EXPECT_TRUE(Window().showDefectFrame);
		ASSERT_TRUE(stack->Redo().HasValue());
		EXPECT_FALSE(Window().showDefectFrame);
	}
}
