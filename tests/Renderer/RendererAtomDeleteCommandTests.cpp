#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
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
} // namespace DefectStudio::Tests
