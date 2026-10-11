#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/SceneDensityEditor.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStartupConfig ClipboardTestConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}

		ScenePath ClipboardTestPath()
		{
			ScenePath path;
			PathNode first, second;
			first.id = AllocateElementId(path);
			second.id = AllocateElementId(path);
			first.binding = PathBinding{PathBinding::CopyPosition{0, {}, 0.0f}};
			second.binding = PathBinding{PathBinding::CopyPosition{1, {}, 0.0f}};
			second.position = glm::vec3(2, 0, 0);
			path.nodes = {first, second};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			path.segments = {segment};
			path.persistKey = "source-path";
			return path;
		}
	}

	class SceneObjectClipboardTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			GetScenePathClipboard().clear();
			GetScenePlaneClipboard().clear();
			GetSceneOrbitalClipboard().clear();
			GetPersistedSceneFreeLabelClipboard().clear();
			GetScenePinnedMeasurementClipboard().clear();
			GetSceneDensityClipboard().clear();
			source.windowId = "clipboard-source";
			source.structure.domainStructureId = "source-structure";
			source.structure.atoms.resize(3);
			source.structure.atoms[0].element = "C";
			source.structure.atoms[1].element = "Si";
			source.structure.atoms[1].cartesianPosition = glm::vec3(2, 0, 0);
			source.structure.atoms[2].element = "O";
			source.structure.atoms[2].cartesianPosition = glm::vec3(0, 2, 0);
			SceneSystem::SyncSceneWithStructure(source.sceneRegistry, source.structure);
			RendererWindowState target;
			target.windowId = "clipboard-target";
			target.structure = source.structure;
			target.structure.domainStructureId = "target-structure";
			target.structure.atoms = {source.structure.atoms[2], source.structure.atoms[0], source.structure.atoms[1]};
			SceneSystem::SyncSceneWithStructure(target.sceneRegistry, target.structure);
			RendererWindowState::FreeLabel resident;
			resident.id = target.sceneRegistry.AllocateObjectId();
			resident.persistKey = "resident-label";
			resident.text = "Istniejąca etykieta";
			target.freeLabels.push_back(resident);
			SceneSystem::SyncLabelEntities(target.sceneRegistry, target);
			renderer.BindUndoStack(stack);
			renderer.AddWindow(std::move(target));
		}
		void TearDown() override { renderer.OnDetach(); }
		RendererWindowState &Target() { return renderer.GetWindows().front(); }
		void CopyPaste(const SceneObjectEditKind kind)
		{
			ASSERT_TRUE(ExecuteSceneObjectEditAction(source, kind, SceneObjectEditAction::Copy));
			EXPECT_EQ(stack->GetUndoDepth(), 0u);
			ASSERT_TRUE(ExecuteSceneObjectEditAction(Target(), kind, SceneObjectEditAction::Paste));
		}
		void CheckSingleUndo()
		{
			ASSERT_EQ(stack->GetUndoDepth(), 1u);
			ASSERT_TRUE(stack->Undo().HasValue());
			EXPECT_EQ(stack->GetUndoDepth(), 0u);
			EXPECT_TRUE(Target().pinnedMeasurements.empty());
			EXPECT_TRUE(Target().scenePlanes.empty());
			EXPECT_TRUE(Target().sceneOrbitals.empty());
			EXPECT_TRUE(Target().sceneDensities.empty());
			ASSERT_NE(Target().paths, nullptr);
			EXPECT_EQ(Target().paths->Store().Size(), 0u);
			ASSERT_EQ(Target().freeLabels.size(), 1u);
			EXPECT_EQ(Target().freeLabels.front().persistKey, "resident-label");
			ASSERT_TRUE(stack->Redo().HasValue());
			EXPECT_EQ(stack->GetUndoDepth(), 1u);
		}
		template <typename Object>
		void AddPair(std::vector<Object> RendererWindowState::*member,
			std::vector<SceneObjectId> &selection, Object object)
		{
			for (int index = 0; index < 2; ++index)
			{
				object.id = source.sceneRegistry.AllocateObjectId();
				object.persistKey = "source-" + std::to_string(index);
				selection.push_back(object.id);
				(source.*member).push_back(object);
			}
		}
		RendererWindowState source;
		RendererLayer renderer{ClipboardTestConfig()};
		Ref<UndoStack> stack = CreateRef<UndoStack>();
	};

	TEST_F(SceneObjectClipboardTests, PinnedMeasurementsRebindOrFreezeWithOneBatchUndo)
	{
		RendererWindowState::PinnedMeasurement pin;
		pin.atomIndices = {0, 1};
		pin.worldOffset = glm::vec3(0, 0, 1);
		pin.renderable = false;
		AddPair(&RendererWindowState::pinnedMeasurements, source.selectedPinnedMeasurements, pin);
		CopyPaste(SceneObjectEditKind::PinnedMeasurement);
		ASSERT_EQ(Target().pinnedMeasurements.size(), 2u);
		const auto &copy = Target().pinnedMeasurements.front();
		EXPECT_EQ(copy.atomIndices, (std::vector<std::size_t>{1, 2}));
		EXPECT_FALSE(copy.linkBroken);
		EXPECT_FALSE(copy.renderable);
		EXPECT_EQ(copy.worldOffset, pin.worldOffset);
		EXPECT_NE(copy.persistKey, source.pinnedMeasurements.front().persistKey);
		CheckSingleUndo();
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.atoms[2].element = "N";
		PasteScenePinnedMeasurementsFromClipboard(Target());
		ASSERT_EQ(Target().pinnedMeasurements.size(), 2u);
		EXPECT_TRUE(Target().pinnedMeasurements.front().linkBroken);
		EXPECT_EQ(Target().pinnedMeasurements.front().frozenAtomPositions,
			(std::vector<glm::vec3>{glm::vec3(0), glm::vec3(2, 0, 0)}));
		EXPECT_EQ(Target().pinnedMeasurements.front().frozenAtomElements, (std::vector<std::string>{"C", "Si"}));
	}

	TEST_F(SceneObjectClipboardTests, FreeLabelsRebindAtomsAndFreezeMissingAnchors)
	{
		RendererWindowState::FreeLabel label;
		label.text = "Kotwica";
		label.anchorAtom = 1;
		label.anchorOffset = glm::vec3(0, 1, 0);
		label.renderable = false;
		AddPair(&RendererWindowState::freeLabels, source.selectedFreeLabels, label);
		CopyPaste(SceneObjectEditKind::FreeLabel);
		ASSERT_EQ(Target().freeLabels.size(), 3u);
		EXPECT_EQ(Target().freeLabels[1].anchorAtom, 2u);
		EXPECT_EQ(Target().freeLabels[1].worldPosition, glm::vec3(2, 1, 0));
		EXPECT_EQ(Target().freeLabels[1].text, "Kotwica");
		EXPECT_FALSE(Target().freeLabels[1].renderable);
		CheckSingleUndo();
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.atoms[2].cartesianPosition = glm::vec3(99);
		PastePersistedSceneFreeLabelsFromClipboard(Target());
		ASSERT_EQ(Target().freeLabels.size(), 3u);
		EXPECT_FALSE(Target().freeLabels[1].anchorAtom.has_value());
		EXPECT_EQ(Target().freeLabels[1].worldPosition, glm::vec3(2, 1, 0));
	}

	TEST_F(SceneObjectClipboardTests, PlanesRebindEveryAnchorAndDetachTheWholeLinkIfOneIsMissing)
	{
		RendererWindowState::ScenePlane plane;
		plane.anchorAtoms = {0, 1, 2};
		plane.alpha = 0.25f;
		plane.renderable = false;
		AddPair(&RendererWindowState::scenePlanes, source.selectedScenePlanes, plane);
		CopyPaste(SceneObjectEditKind::Plane);
		ASSERT_EQ(Target().scenePlanes.size(), 2u);
		EXPECT_EQ(Target().scenePlanes.front().anchorAtoms, (std::vector<std::size_t>{1, 2, 0}));
		EXPECT_FLOAT_EQ(Target().scenePlanes.front().alpha, 0.25f);
		EXPECT_FALSE(Target().scenePlanes.front().renderable);
		const glm::vec3 frozen = Target().scenePlanes.front().center;
		CheckSingleUndo();
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.atoms[0].element = "N";
		PasteScenePlanesFromClipboard(Target());
		ASSERT_EQ(Target().scenePlanes.size(), 2u);
		EXPECT_TRUE(Target().scenePlanes.front().anchorAtoms.empty());
		EXPECT_EQ(Target().scenePlanes.front().center, frozen);
	}

	TEST_F(SceneObjectClipboardTests, OrbitalsAndLcaoComponentsRebindAndKeepFrozenCentres)
	{
		RendererWindowState::SceneOrbital orbital;
		orbital.anchorAtoms = {0, 1};
		orbital.renderable = false;
		RendererWindowState::SceneOrbital::LcaoComponent component;
		component.anchorAtom = 1;
		component.coefficient = -0.5f;
		orbital.lcaoComponents.push_back(component);
		AddPair(&RendererWindowState::sceneOrbitals, source.selectedSceneOrbitals, orbital);
		source.sceneOrbitals[1].lcaoComponents.clear();
		CopyPaste(SceneObjectEditKind::Orbital);
		ASSERT_EQ(Target().sceneOrbitals.size(), 2u);
		EXPECT_EQ(Target().sceneOrbitals.front().anchorAtoms, (std::vector<std::size_t>{1, 2}));
		ASSERT_EQ(Target().sceneOrbitals.front().lcaoComponents.size(), 1u);
		EXPECT_EQ(Target().sceneOrbitals.front().lcaoComponents.front().anchorAtom, 2u);
		EXPECT_EQ(Target().sceneOrbitals.front().lcaoComponents.front().center, glm::vec3(2, 0, 0));
		EXPECT_FLOAT_EQ(Target().sceneOrbitals.front().lcaoComponents.front().coefficient, -0.5f);
		EXPECT_FALSE(Target().sceneOrbitals.front().renderable);
		EXPECT_EQ(Target().sceneOrbitals[1].centerA, glm::vec3(0));
		EXPECT_EQ(Target().sceneOrbitals[1].centerB, glm::vec3(2, 0, 0));
		CheckSingleUndo();
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.atoms[2].element = "N";
		PasteSceneOrbitalsFromClipboard(Target());
		ASSERT_EQ(Target().sceneOrbitals.size(), 2u);
		EXPECT_TRUE(Target().sceneOrbitals.front().anchorAtoms.empty());
		EXPECT_GE(Target().sceneOrbitals.front().lcaoComponents.front().anchorAtom, Target().structure.atoms.size());
		EXPECT_EQ(Target().sceneOrbitals.front().lcaoComponents.front().center, glm::vec3(2, 0, 0));
		EXPECT_EQ(Target().sceneOrbitals[1].centerB, glm::vec3(2, 0, 0));
	}

	TEST_F(SceneObjectClipboardTests, DensitiesPastePersistedSourcesAndSettingsPendingReloadInOneUndo)
	{
		RendererWindowState::SceneDensity density;
		density.displayName = "Gęstość";
		density.chgcarPath = Path::FromUtf8("D:/calculations/CHGCAR");
		density.referencePath = Path::FromUtf8("D:/reference/CHGCAR");
		density.component = DensityComponent::Total;
		density.isoValue = 0.08f;
		density.alpha = 0.3f;
		density.renderable = false;
		density.analysisCentre = glm::vec3(1, 2, 3);
		density.loadState = RendererWindowState::SceneDensity::LoadState::Ready;
		AddPair(&RendererWindowState::sceneDensities, source.selectedSceneDensities, density);
		CopyPaste(SceneObjectEditKind::Density);
		ASSERT_EQ(Target().sceneDensities.size(), 2u);
		const auto &copy = Target().sceneDensities.front();
		EXPECT_EQ(copy.chgcarPath, density.chgcarPath);
		EXPECT_EQ(copy.referencePath, density.referencePath);
		EXPECT_EQ(copy.component, density.component);
		EXPECT_EQ(copy.analysisCentre, density.analysisCentre);
		EXPECT_FLOAT_EQ(copy.isoValue, 0.08f);
		EXPECT_FALSE(copy.renderable);
		EXPECT_EQ(copy.loadState, RendererWindowState::SceneDensity::LoadState::Pending);
		EXPECT_EQ(copy.data, nullptr);
		CheckSingleUndo();
	}

	TEST_F(SceneObjectClipboardTests, PathsAndArrowsRebindAtomsAndKeepSavedGeometryOnMissingAnchors)
	{
		Target().selectedSceneDensities = {SceneObjectId{999}};
		Target().selectedSceneHideVolumes = {SceneObjectId{998}};
		for (bool arrow : {false, true})
		{
			ScenePath path = ClipboardTestPath();
			path.style.endDecoration.kind = arrow ? PathDecorationKind::Arrow : PathDecorationKind::None;
			path.renderable = false;
			source.selectedScenePaths.push_back(SceneSystem::AppendScenePath(source, std::move(path)));
		}
		CopyPaste(SceneObjectEditKind::Path);
		ASSERT_EQ(Target().paths->Store().Size(), 2u);
		EXPECT_TRUE(Target().selectedSceneDensities.empty());
		EXPECT_TRUE(Target().selectedSceneHideVolumes.empty());
		const auto ids = Target().selectedScenePaths;
		const ScenePath &copy = *Target().paths->Store().Find(ids.front());
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(copy.nodes[0].binding.value).atomIndex, 1u);
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(copy.nodes[1].binding.value).atomIndex, 2u);
		EXPECT_FALSE(copy.renderable);
		EXPECT_EQ(Target().paths->Store().Find(ids.back())->style.endDecoration.kind, PathDecorationKind::Arrow);
		CheckSingleUndo();
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.atoms[2].element = "N";
		PasteScenePathsFromClipboard(Target());
		ASSERT_EQ(Target().selectedScenePaths.size(), 2u);
		const ScenePath &broken = *Target().paths->Store().Find(Target().selectedScenePaths.front());
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(broken.nodes[1].binding.value));
		const auto resolved = ResolveNodePositions(broken, SceneSystem::MakePathBindingContext(Target()));
		EXPECT_EQ(resolved.positions[1], glm::vec3(2, 0, 0));
	}

	TEST_F(SceneObjectClipboardTests, BondFramePathsFreezeTheirLiveTransformWhenTheTargetCannotBind)
	{
		ScenePath path = ClipboardTestPath();
		path.nodes[0].binding = PathBinding{};
		path.nodes[1].binding = PathBinding{};
		path.nodes[0].position = glm::vec3(0, -1, 0);
		path.nodes[1].position = glm::vec3(0, 1, 0);
		path.segments.front().data = CircularArcSegmentData{glm::vec3(0, 0, 1), 3.14159265f};
		path.transformBinding.value = PathTransformBinding::BondFrame{0, 1, 0.0f};
		const auto original = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(source));
		source.selectedScenePaths = {SceneSystem::AppendScenePath(source, path)};
		CopyPaste(SceneObjectEditKind::Path);
		ASSERT_EQ(Target().selectedScenePaths.size(), 1u);
		const auto &copy = *Target().paths->Store().Find(Target().selectedScenePaths.front());
		EXPECT_EQ(std::get<PathTransformBinding::BondFrame>(copy.transformBinding.value).atomA, 1u);
		EXPECT_EQ(std::get<PathTransformBinding::BondFrame>(copy.transformBinding.value).atomB, 2u);
		CheckSingleUndo();
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.atoms[2].element = "N";
		PasteScenePathsFromClipboard(Target());
		const auto &broken = *Target().paths->Store().Find(Target().selectedScenePaths.front());
		EXPECT_TRUE(std::holds_alternative<PathTransformBinding::Free>(broken.transformBinding.value));
		const auto frozen = ResolveNodePositions(broken, SceneSystem::MakePathBindingContext(Target()));
		ASSERT_EQ(frozen.positions.size(), original.positions.size());
		for (std::size_t index = 0; index < frozen.positions.size(); ++index)
			EXPECT_NEAR(glm::distance(frozen.positions[index], original.positions[index]), 0.0f, 1e-5f);
		EXPECT_NEAR(glm::distance(std::get<CircularArcSegmentData>(broken.segments.front().data).planeNormal,
			std::get<CircularArcSegmentData>(path.segments.front().data).planeNormal), 0.0f, 1e-5f);
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.atoms[2] = source.structure.atoms[1];
		Target().structure.domainStructureId = source.structure.domainStructureId;
		PasteScenePathsFromClipboard(Target());
		const auto &detached = *Target().paths->Store().Find(Target().selectedScenePaths.front());
		EXPECT_TRUE(std::holds_alternative<PathTransformBinding::Free>(detached.transformBinding.value));
		EXPECT_NEAR(glm::distance(std::get<CircularArcSegmentData>(detached.segments.front().data).planeNormal,
			std::get<CircularArcSegmentData>(path.segments.front().data).planeNormal), 0.0f, 1e-5f);
		const auto offset = ResolveNodePositions(detached, SceneSystem::MakePathBindingContext(Target()));
		for (std::size_t index = 0; index < offset.positions.size(); ++index)
			EXPECT_NEAR(glm::distance(offset.positions[index], original.positions[index] + glm::vec3(0.5f, 0, 0)), 0.0f, 1e-5f);
	}

	TEST_F(SceneObjectClipboardTests, PathObjectOriginsResolveDestinationIdsAndFreezeMissingTargets)
	{
		RendererWindowState::ScenePlane anchor;
		anchor.id = source.sceneRegistry.AllocateObjectId();
		anchor.persistKey = "plane-anchor";
		anchor.center = glm::vec3(6, 0, 0);
		source.scenePlanes.push_back(anchor);
		anchor.id = Target().sceneRegistry.AllocateObjectId();
		Target().scenePlanes.push_back(anchor);
		const SceneObjectId targetId = anchor.id;
		ScenePath path = ClipboardTestPath();
		path.nodes.front().binding = PathBinding{PathBinding::ObjectOrigin{source.scenePlanes.front().id, {}}};
		source.selectedScenePaths = {SceneSystem::AppendScenePath(source, path)};
		CopyPaste(SceneObjectEditKind::Path);
		const auto &copy = *Target().paths->Store().Find(Target().selectedScenePaths.front());
		ASSERT_TRUE(std::holds_alternative<PathBinding::ObjectOrigin>(copy.nodes.front().binding.value));
		EXPECT_EQ(std::get<PathBinding::ObjectOrigin>(copy.nodes.front().binding.value).object, targetId);
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().scenePlanes.clear();
		PasteScenePathsFromClipboard(Target());
		const auto &broken = *Target().paths->Store().Find(Target().selectedScenePaths.front());
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(broken.nodes.front().binding.value));
		const auto frozen = ResolveNodePositions(broken, SceneSystem::MakePathBindingContext(Target()));
		EXPECT_EQ(frozen.positions.front(), anchor.center);
	}

	TEST_F(SceneObjectClipboardTests, SameStructureLabelPasteKeepsItsOffsetAfterAnchorRefresh)
	{
		Target().structure.domainStructureId = source.structure.domainStructureId;
		RendererWindowState::FreeLabel label;
		label.anchorAtom = 1;
		label.anchorOffset = glm::vec3(0, 1, 0);
		AddPair(&RendererWindowState::freeLabels, source.selectedFreeLabels, label);
		CopyPaste(SceneObjectEditKind::FreeLabel);
		RefreshAnchoredFreeLabels(Target());
		ASSERT_EQ(Target().freeLabels.size(), 3u);
		EXPECT_EQ(Target().freeLabels[1].anchorAtom, 2u);
		EXPECT_EQ(Target().freeLabels[1].worldPosition, glm::vec3(2.5f, 1, 0));
		EXPECT_EQ(Target().freeLabels[1].anchorOffset, glm::vec3(0.5f, 1, 0));
		CheckSingleUndo();
	}

	TEST_F(SceneObjectClipboardTests, SameStructureLcaoPasteOffsetsAndDetachesComponents)
	{
		Target().structure.domainStructureId = source.structure.domainStructureId;
		RendererWindowState::SceneOrbital orbital;
		RendererWindowState::SceneOrbital::LcaoComponent component;
		component.anchorAtom = 1;
		orbital.lcaoComponents.push_back(component);
		AddPair(&RendererWindowState::sceneOrbitals, source.selectedSceneOrbitals, orbital);
		CopyPaste(SceneObjectEditKind::Orbital);
		ResolveAnchoredOrbitals(Target());
		ASSERT_EQ(Target().sceneOrbitals.size(), 2u);
		const auto &copy = Target().sceneOrbitals.front().lcaoComponents.front();
		EXPECT_EQ(copy.center, glm::vec3(2.5f, 0, 0));
		EXPECT_GE(copy.anchorAtom, Target().structure.atoms.size());
		CheckSingleUndo();
	}

	TEST_F(SceneObjectClipboardTests, VacancyBondsAndLabelsResolveVacanciesBySpeciesAndPosition)
	{
		RendererVacancyData vacancy;
		vacancy.sourceSpecies = "C";
		vacancy.cartesianPosition = glm::vec3(4, 0, 0);
		source.structure.vacancies.push_back(vacancy);
		RendererVacancyData other = vacancy;
		other.sourceSpecies = "Si";
		other.cartesianPosition = glm::vec3(8, 0, 0);
		Target().structure.vacancies = {other, vacancy};
		ScenePath path = ClipboardTestPath();
		path.nodes[0].binding = PathBinding{PathBinding::CopyVacancy{0, {}, 0.0f}};
		source.selectedScenePaths = {SceneSystem::AppendScenePath(source, path)};
		CopyPaste(SceneObjectEditKind::Path);
		ASSERT_EQ(Target().selectedScenePaths.size(), 1u);
		const auto &copy = *Target().paths->Store().Find(Target().selectedScenePaths.front());
		EXPECT_TRUE(IsSceneOutlinerVacancyBond(copy));
		EXPECT_EQ(std::get<PathBinding::CopyVacancy>(copy.nodes.front().binding.value).vacancyIndex, 1u);
		CheckSingleUndo();
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.vacancies = {other};
		PasteScenePathsFromClipboard(Target());
		const auto &broken = *Target().paths->Store().Find(Target().selectedScenePaths.front());
		EXPECT_TRUE(IsSceneOutlinerVacancyBond(broken));
		const auto resolved = ResolveNodePositions(broken, SceneSystem::MakePathBindingContext(Target()));
		EXPECT_EQ(resolved.positions.front(), vacancy.cartesianPosition);
		EXPECT_FALSE(resolved.diagnostics.empty());
		ASSERT_TRUE(stack->Undo().HasValue());
		RendererWindowState::FreeLabel label;
		label.anchorVacancy = 0;
		AddPair(&RendererWindowState::freeLabels, source.selectedFreeLabels, label);
		Target().structure.vacancies = {other, vacancy};
		CopyPaste(SceneObjectEditKind::FreeLabel);
		ASSERT_EQ(Target().freeLabels.size(), 3u);
		EXPECT_EQ(Target().freeLabels[1].anchorVacancy, 1u);
		ASSERT_TRUE(stack->Undo().HasValue());
		Target().structure.vacancies = {other};
		PastePersistedSceneFreeLabelsFromClipboard(Target());
		EXPECT_FALSE(Target().freeLabels[1].anchorVacancy.has_value());
		EXPECT_EQ(Target().freeLabels[1].worldPosition, vacancy.cartesianPosition);
	}
} // namespace DefectStudio::Tests
