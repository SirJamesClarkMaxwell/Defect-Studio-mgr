#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/VacancyBond.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStartupConfig DeleteTestConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}

		ScenePath VacancyBond(std::size_t atom, std::size_t vacancy)
		{
			ScenePath path;
			path.nodes = {{PathElementId{1}, {}, PathBinding{PathBinding::CopyPosition{atom, {}, 0.9f}}},
				{PathElementId{2}, {}, PathBinding{PathBinding::CopyVacancy{vacancy, {}, 1.0f}}}};
			path.segments = {{PathElementId{3}, LineSegmentData{}}};
			path.nextElementId = 4;
			path.style.gradient.enabled = true;
			path.style.gradient.stops = {{0.0f, glm::vec3(1), 1.0f}, {1.0f, glm::vec3(0), 1.0f}};
			return path;
		}

		std::string SceneBytes(RendererWindowState &window)
		{
			SceneObjectsFile file;
			file.projectObjects = ExtractPersistedSceneObjects(window);
			return SceneObjectsIO::Serialize(file);
		}
	}

	class RendererAtomDeleteCommandTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			CrystalStructure structure;
			structure.isPeriodic = false;
			structure.atoms = {AtomSite{"C", glm::vec3(1, 0, 0), {}, 0},
				AtomSite{"Si", glm::vec3(2, 0, 0), {}, 1}, AtomSite{"O", glm::vec3(3, 0, 0), {}, 2}};
			VacancySite vacancy;
			vacancy.position = glm::vec3(-1, 0, 0);
			vacancy.sourceSpecies = "O";
			structure.vacancies.push_back(vacancy);
			Bond bond;
			bond.firstAtomIndex = 0;
			bond.secondAtomIndex = 1;
			bond.lengthAngstrom = 1.0f;
			bond.origin = BondOrigin::Manual;
			structure.bonds.push_back(bond);
			record = domain->Workspace().Structures().Add(std::move(structure));
			RendererWindowState window;
			window.windowId = "atom-delete-test";
			window.structure = BuildRendererStructureData(record->structure, {}, "test", styles, ToString(record->id));
			SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
			renderer->AddWindow(std::move(window));
			(void)AddVacancyLabels(Window(), {0});
		}
		void TearDown() override { renderer->OnDetach(); }
		RendererWindowState &Window() { return renderer->GetWindows().front(); }
		Unique<ICommand> DeleteCommand()
		{
			return CreateDeleteSelectedAtomsCommand(domain, renderer, styles, {}, Window().windowId);
		}
		void SetVacancies(std::vector<VacancySite> vacancies)
		{
			auto target = ResolveAtomEditTarget(*renderer, *domain, Window().windowId);
			ASSERT_TRUE(target.HasValue());
			target->record->structure.vacancies = std::move(vacancies);
			RebuildAndSync(Window(), *target->record, styles, {});
		}
		Ref<DomainLayer> domain = CreateRef<DomainLayer>();
		Ref<RendererLayer> renderer = CreateRef<RendererLayer>(DeleteTestConfig());
		Ref<const StructureRecord> record;
		AtomStyleTable styles;
	};

	TEST_F(RendererAtomDeleteCommandTests, DeleteCreatesSpeciesLabelsWithSitesAsOneUndoStep)
	{
		const SceneObjectId originalLabel = Window().freeLabels.front().id;
		Window().selectedAtomIndices = {0, 1, 1};
		auto command = DeleteCommand();
		CommandContext context;
		ASSERT_TRUE(command->Execute(context).HasValue());
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(std::move(command)));
		EXPECT_EQ(stack.GetUndoDepth(), 1u);
		ASSERT_EQ(record->structure.atoms.size(), 1u);
		ASSERT_EQ(record->structure.vacancies.size(), 3u);
		ASSERT_EQ(Window().freeLabels.size(), 3u);
		EXPECT_EQ(record->structure.vacancies[1].sourceSpecies, "Si");
		EXPECT_EQ(record->structure.vacancies[1].position, glm::vec3(2, 0, 0));
		EXPECT_EQ(record->structure.vacancies[2].sourceSpecies, "C");
		EXPECT_EQ(record->structure.vacancies[2].position, glm::vec3(1, 0, 0));
		EXPECT_EQ(Window().freeLabels[1].text, "V_Si");
		EXPECT_EQ(Window().freeLabels[2].text, "V_C");
		for (std::size_t index = 1; index < 3; ++index)
		{
			EXPECT_EQ(Window().freeLabels[index].anchorVacancy, index);
			EXPECT_EQ(Window().freeLabels[index].worldPosition, Window().structure.vacancies[index].cartesianPosition);
			EXPECT_TRUE(IsFreeLabelVisible(Window().freeLabels[index], Window().structure, Window().sceneHideVolumes));
			EXPECT_TRUE(Window().sceneRegistry.FindObject(Window().freeLabels[index].id));
		}
		ASSERT_TRUE(stack.Undo().HasValue());
		EXPECT_EQ(stack.GetUndoDepth(), 0u);
		EXPECT_EQ(record->structure.atoms.size(), 3u);
		EXPECT_EQ(record->structure.bonds.size(), 1u);
		EXPECT_EQ(record->structure.vacancies.size(), 1u);
		ASSERT_EQ(Window().freeLabels.size(), 1u);
		EXPECT_EQ(Window().freeLabels.front().id, originalLabel);
		ASSERT_TRUE(stack.Redo().HasValue());
		EXPECT_EQ(stack.GetUndoDepth(), 1u);
		EXPECT_EQ(record->structure.atoms.size(), 1u);
		ASSERT_EQ(Window().freeLabels.size(), 3u);
		EXPECT_EQ(Window().freeLabels[1].text, "V_Si");
		EXPECT_EQ(Window().freeLabels[2].text, "V_C");
	}

	TEST_F(RendererAtomDeleteCommandTests, InvalidAtomSelectionDoesNotPartiallyDeleteAtomsOrBonds)
	{
		Window().selectedAtomIndices = {0, 99};
		Window().selectedBondIndices = {0};
		auto command = DeleteCommand();
		CommandContext context;
		EXPECT_FALSE(command->Execute(context).HasValue());
		EXPECT_EQ(record->structure.atoms.size(), 3u);
		EXPECT_EQ(record->structure.bonds.size(), 1u);
		EXPECT_EQ(record->structure.vacancies.size(), 1u);
		EXPECT_EQ(Window().freeLabels.size(), 1u);
	}

	TEST_F(RendererAtomDeleteCommandTests, BondOnlyDeleteKeepsExistingVacanciesAndLabels)
	{
		Window().selectedBondIndices = {0};
		auto command = DeleteCommand();
		CommandContext context;
		ASSERT_TRUE(command->Execute(context).HasValue());
		EXPECT_TRUE(record->structure.bonds.empty());
		EXPECT_EQ(record->structure.atoms.size(), 3u);
		EXPECT_EQ(record->structure.vacancies.size(), 1u);
		EXPECT_EQ(Window().freeLabels.size(), 1u);
	}

	TEST_F(RendererAtomDeleteCommandTests, RemovingEarlierAtomKeepsVacancyBondOnSamePhysicalAtom)
	{
		auto target = ResolveAtomEditTarget(*renderer, *domain, Window().windowId);
		ASSERT_TRUE(target.HasValue());
		target->record->structure.atoms.push_back(AtomSite{"Ge", glm::vec3(4, 0, 0), {}, 3});
		RebuildAndSync(Window(), *target->record, styles, {});
		const auto id = SceneSystem::AppendScenePath(Window(), VacancyBond(3, 0));
		const auto position = Window().structure.atoms[3].cartesianPosition;
		const auto before = ResolveNodePositions(*Window().paths->Store().Find(id),
			SceneSystem::MakePathBindingContext(Window())).positions;
		Window().selectedAtomIndices = {1};
		auto command = DeleteCommand();
		CommandContext context;
		ASSERT_TRUE(command->Execute(context).HasValue());
		const auto pair = GeneratedVacancyBondPair(*Window().paths->Store().Find(id));
		ASSERT_TRUE(pair.has_value());
		EXPECT_FALSE(pair->vacancyPair);
		EXPECT_EQ(pair->first, 2u);
		EXPECT_EQ(Window().structure.atoms[pair->first].cartesianPosition, position);
		EXPECT_EQ(ResolveNodePositions(*Window().paths->Store().Find(id),
			SceneSystem::MakePathBindingContext(Window())).positions, before);
	}

	TEST_F(RendererAtomDeleteCommandTests, DeletedAtomEndpointBecomesVacancyPairWithZeroBuffers)
	{
		const auto id = SceneSystem::AppendScenePath(Window(), VacancyBond(1, 0));
		Window().selectedAtomIndices = {1};
		auto command = DeleteCommand();
		CommandContext context;
		ASSERT_TRUE(command->Execute(context).HasValue());
		const auto &path = *Window().paths->Store().Find(id);
		const auto pair = GeneratedVacancyBondPair(path);
		ASSERT_TRUE(pair.has_value());
		EXPECT_EQ(*pair, (VacancyBondPair{true, 0, 1}));
		for (const auto &node : path.nodes)
			EXPECT_FLOAT_EQ(std::get<PathBinding::CopyVacancy>(node.binding.value).buffer, 0.0f);
		const auto resolved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(Window()));
		EXPECT_EQ(resolved.positions.front(), glm::vec3(2, 0, 0));
		EXPECT_EQ(resolved.positions.back(), glm::vec3(-1, 0, 0));
	}

	TEST_F(RendererAtomDeleteCommandTests, DeletedAtomLabelFollowsVacancyAndPinFreezesPreDeleteAtoms)
	{
		RendererWindowState::FreeLabel label;
		label.anchorAtom = 1;
		label.anchorOffset = glm::vec3(0, 2, 0);
		Window().freeLabels.push_back(label);
		RendererWindowState::PinnedMeasurement pin;
		pin.atomIndices = {1, 2};
		Window().pinnedMeasurements.push_back(pin);
		Window().selectedAtomIndices = {1};
		auto command = DeleteCommand();
		CommandContext context;
		ASSERT_TRUE(command->Execute(context).HasValue());
		const auto &moved = Window().freeLabels[1];
		EXPECT_FALSE(moved.anchorAtom.has_value());
		EXPECT_EQ(moved.anchorVacancy, 1u);
		EXPECT_EQ(moved.anchorOffset, label.anchorOffset);
		EXPECT_EQ(moved.worldPosition, glm::vec3(2, 2, 0));
		const auto &broken = Window().pinnedMeasurements.front();
		EXPECT_TRUE(broken.linkBroken);
		EXPECT_EQ(broken.atomIndices, pin.atomIndices);
		EXPECT_EQ(broken.frozenAtomPositions, (std::vector<glm::vec3>{glm::vec3(2, 0, 0), glm::vec3(3, 0, 0)}));
		EXPECT_EQ(broken.frozenAtomElements, (std::vector<std::string>{"Si", "O"}));
	}

	TEST_F(RendererAtomDeleteCommandTests, UndoAndRedoRestoreSceneBytesIdsAndPathRevisions)
	{
		const auto pathId = SceneSystem::AppendScenePath(Window(), VacancyBond(1, 0));
		auto collapsed = VacancyBond(1, 0);
		collapsed.nodes.back().binding.value = PathBinding::CopyPosition{1, {}, 0};
		const auto collapsedId = SceneSystem::AppendScenePath(Window(), collapsed);
		Window().defectFrameChildren.paths = {collapsedId};
		Window().freeLabels.front().anchorVacancy.reset();
		Window().freeLabels.front().anchorAtom = 1;
		Window().freeLabels.front().anchorOffset = glm::vec3(0, 1, 0);
		RendererWindowState::PinnedMeasurement pin;
		pin.atomIndices = {0, 1};
		pin.worldOffset = glm::vec3(1, 2, 3);
		pin.flipped = true;
		pin.style.strokeWidth = 2.0f;
		Window().pinnedMeasurements.push_back(pin);
		Window().displacementComparison.emplace();
		Window().displacementComparison->result.matches = {AtomDisplacement{1, 9}, AtomDisplacement{2, 7}};
		RefreshAnchoredFreeLabels(Window());
		SceneSystem::SyncLabelEntities(Window().sceneRegistry, Window());
		const auto before = CaptureSceneObjectsSnapshot(Window());
		const auto bytesBefore = SceneBytes(Window());
		Window().selectedAtomIndices = {1};
		auto command = DeleteCommand();
		CommandContext context;
		ASSERT_TRUE(command->Execute(context).HasValue());
		const auto after = CaptureSceneObjectsSnapshot(Window());
		const auto bytesAfter = SceneBytes(Window());
		UndoStack stack;
		ASSERT_TRUE(stack.PushExecuted(std::move(command)));
		for (int cycle = 0; cycle < 2; ++cycle)
		{
			ASSERT_TRUE(stack.Undo().HasValue());
			EXPECT_EQ(SceneBytes(Window()), bytesBefore);
			EXPECT_EQ(Window().freeLabels.front().id, before.freeLabels.front().id);
			EXPECT_EQ(Window().pinnedMeasurements.front().id, before.pinnedMeasurements.front().id);
			EXPECT_EQ(Window().pinnedMeasurements.front().frozenAtomPositions, before.pinnedMeasurements.front().frozenAtomPositions);
			EXPECT_EQ(Window().paths->Store().RevisionsFor(pathId), before.paths.RevisionsFor(pathId));
			EXPECT_EQ(Window().paths->Store().Find(pathId)->nodes.front().id, before.paths.Find(pathId)->nodes.front().id);
			EXPECT_TRUE(Window().sceneRegistry.FindObject(before.freeLabels.front().id));
			EXPECT_TRUE(Window().paths->Store().Contains(collapsedId));
			EXPECT_EQ(Window().defectFrameChildren.paths, (std::vector<SceneObjectId>{collapsedId}));
			ASSERT_EQ(Window().displacementComparison->result.matches.size(), 2u);
			EXPECT_EQ(Window().displacementComparison->result.matches[0].referenceAtomIndex, 1u);
			EXPECT_EQ(Window().displacementComparison->result.matches[1].referenceAtomIndex, 2u);
			ASSERT_TRUE(stack.Redo().HasValue());
			EXPECT_EQ(SceneBytes(Window()), bytesAfter);
			ASSERT_EQ(Window().freeLabels.size(), after.freeLabels.size());
			for (std::size_t i = 0; i < after.freeLabels.size(); ++i)
				EXPECT_EQ(Window().freeLabels[i].id, after.freeLabels[i].id);
			EXPECT_EQ(Window().paths->Store().RevisionsFor(pathId), after.paths.RevisionsFor(pathId));
			EXPECT_FALSE(Window().paths->Store().Contains(collapsedId));
			EXPECT_TRUE(Window().defectFrameChildren.paths.empty());
			ASSERT_EQ(Window().displacementComparison->result.matches.size(), 1u);
			EXPECT_EQ(Window().displacementComparison->result.matches.front().referenceAtomIndex, 1u);
			EXPECT_EQ(Window().displacementComparison->result.matches.front().comparisonAtomIndex, 7u);
		}
	}

	TEST_F(RendererAtomDeleteCommandTests, VacancyRemovalRemapsLabelsAndPathsAndRestoresEveryWindow)
	{
		auto vacancies = record->structure.vacancies;
		vacancies.push_back(vacancies.front());
		vacancies.back().position = glm::vec3(5, 0, 0);
		vacancies.push_back(vacancies.front());
		vacancies.back().position = glm::vec3(9, 0, 0);
		SetVacancies(vacancies);
		Window().freeLabels.front().anchorVacancy = 2;
		Window().freeLabels.front().anchorOffset = glm::vec3(0, 1, 0);
		Window().defectFrameChildren.vacancies = {0, 2};
		const auto deletedBond = SceneSystem::AppendScenePath(Window(), VacancyBond(0, 0));
		const auto survivingBond = SceneSystem::AppendScenePath(Window(), VacancyBond(0, 2));
		auto authored = VacancyBond(0, 0);
		authored.style.gradient.enabled = false;
		authored.transform.position = glm::vec3(2, 3, 4);
		authored.transform.scale = glm::vec3(2);
		const auto frozenPath = SceneSystem::AppendScenePath(Window(), authored);
		const auto oldPosition = ResolveNodePositions(*Window().paths->Store().Find(frozenPath),
			SceneSystem::MakePathBindingContext(Window())).positions.back();
		RefreshAnchoredFreeLabels(Window());
		SceneSystem::SyncLabelEntities(Window().sceneRegistry, Window());
		const auto before = CaptureSceneObjectsSnapshot(Window());
		const auto bytesBefore = SceneBytes(Window());
		RendererWindowState second;
		second.windowId = "second";
		second.structure = Window().structure;
		SceneSystem::SyncSceneWithStructure(second.sceneRegistry, second.structure);
		RestoreSceneObjectsSnapshot(second, before);
		renderer->AddWindow(std::move(second));
		SetVacanciesPayload payload{Window().windowId, vacancies, "Remove vacancy"};
		payload.vacancies.erase(payload.vacancies.begin());
		auto command = CreateSetVacanciesCommand(domain, renderer, styles, std::move(payload));
		CommandContext context;
		ASSERT_TRUE(command->Execute(context).HasValue());
		for (auto &window : renderer->GetWindows())
		{
			RefreshAnchoredFreeLabels(window);
			EXPECT_EQ(window.freeLabels.front().anchorVacancy, 1u);
			EXPECT_EQ(window.freeLabels.front().worldPosition, glm::vec3(9, 1, 0));
			EXPECT_FALSE(window.paths->Store().Contains(deletedBond));
			EXPECT_EQ(GeneratedVacancyBondPair(*window.paths->Store().Find(survivingBond))->second, 1u);
			const auto &path = *window.paths->Store().Find(frozenPath);
			EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(path.nodes.back().binding.value));
			EXPECT_LT(glm::distance(ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window)).positions.back(),
				oldPosition), 1e-5f); // world -> path-local -> world round trip
		}
		EXPECT_EQ(Window().defectFrameChildren.vacancies, (std::vector<std::size_t>{1}));
		const auto bytesAfter = SceneBytes(Window());
		ASSERT_TRUE(command->Undo(context).HasValue());
		for (auto &window : renderer->GetWindows()) EXPECT_EQ(SceneBytes(window), bytesBefore);
		EXPECT_EQ(Window().defectFrameChildren.vacancies, (std::vector<std::size_t>{0, 2}));
		ASSERT_TRUE(command->Redo(context).HasValue());
		for (auto &window : renderer->GetWindows()) EXPECT_EQ(SceneBytes(window), bytesAfter);
	}

	TEST_F(RendererAtomDeleteCommandTests, RemovedVacancyLabelKeepsResolvedPositionAndOffset)
	{
		Window().freeLabels.front().anchorOffset = glm::vec3(0, 2, 0);
		Window().freeLabels.front().worldPosition = glm::vec3(99);
		auto command = CreateSetVacanciesCommand(domain, renderer, styles,
			SetVacanciesPayload{Window().windowId, {}, "Remove vacancy"});
		CommandContext context;
		ASSERT_TRUE(command->Execute(context).HasValue());
		EXPECT_FALSE(Window().freeLabels.front().anchorVacancy.has_value());
		EXPECT_EQ(Window().freeLabels.front().worldPosition, glm::vec3(-1, 2, 0));
		EXPECT_EQ(Window().freeLabels.front().anchorOffset, glm::vec3(0, 2, 0));
	}
} // namespace DefectStudio::Tests
