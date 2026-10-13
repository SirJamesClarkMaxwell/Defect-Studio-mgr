#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/Crystal/CellEditing.hpp"
#include "Domain/Crystal/LatticeBasisExpansion.hpp"
#include "Domain/Crystal/Supercell.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/Commands/RendererCellEditCommand.hpp"
#include "Renderer/Commands/RendererCommandRegistration.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStartupConfig CellTestConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}
	}

	class RendererCellEditCommandTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			CrystalStructure structure;
			structure.name = "zincblende";
			structure.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{5.43f});
			AtomSite zinc;
			zinc.species = "Zn";
			zinc.label = "zinc";
			zinc.charge = 2.0f;
			AtomSite sulfur;
			sulfur.species = "S";
			sulfur.fractional = glm::vec3(0.25f);
			structure.atoms = ExpandBasisOverLattice(std::array{zinc, sulfur}, BravaisCenteringPreset::FaceCentered);
			for (auto &atom : structure.atoms)
				atom.position = structure.FractionalToCartesian(atom.fractional);
			VacancySite vacancy;
			vacancy.fractional = glm::vec3(0.1f, 0.2f, 0.3f);
			vacancy.position = structure.FractionalToCartesian(vacancy.fractional);
			vacancy.sourceSpecies = "Zn";
			structure.vacancies.push_back(vacancy);
			structure.defectFrame = DefectFrame{};
			structure.defectFrame->origin = vacancy.position;
			Bond manual;
			manual.firstAtomIndex = 0;
			manual.secondAtomIndex = 4;
			manual.lengthAngstrom = 2.35f;
			manual.origin = BondOrigin::Manual;
			structure.bonds.push_back(manual);
			structure.bondSettings.perPairCutoffOverride["S-Zn"] = 2.0f;
			original = structure;
			record = domain->Workspace().Structures().Add(std::move(structure));
			AddWindow("cell-test");
			registry.SetUndoStack(stack);
			RegisterRendererCommands(registry, CreateRef<EventBus>(), domain, renderer, {}, {});
		}

		void TearDown() override { renderer->OnDetach(); }

		void AddWindow(std::string id)
		{
			RendererWindowState window;
			window.windowId = std::move(id);
			window.structure = BuildRendererStructureData(record->structure, {}, "test", {}, ToString(record->id));
			SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
			renderer->AddWindow(std::move(window));
		}

		Result<CommandOutcome> Replace(CrystalStructure structure, bool changesSiteIndices = false)
		{
			CommandContext context;
			context.Set<ReplaceStructurePayload>(kReplaceStructurePayloadKey,
				ReplaceStructurePayload{"cell-test", std::move(structure), "Cell test", changesSiteIndices});
			return registry.Execute(CommandID{kReplaceStructureCommandId}, std::move(context));
		}

		void ExpectOriginal()
		{
			const auto &structure = record->structure;
			EXPECT_EQ(structure.cell.vectors, original.cell.vectors);
			ASSERT_EQ(structure.atoms.size(), original.atoms.size());
			for (std::size_t index = 0; index < original.atoms.size(); ++index)
			{
				EXPECT_EQ(structure.atoms[index].position, original.atoms[index].position);
				EXPECT_EQ(structure.atoms[index].fractional, original.atoms[index].fractional);
				EXPECT_EQ(structure.atoms[index].species, original.atoms[index].species);
				EXPECT_EQ(structure.atoms[index].label, original.atoms[index].label);
				EXPECT_EQ(structure.atoms[index].charge, original.atoms[index].charge);
			}
			ASSERT_EQ(structure.vacancies.size(), 1u);
			EXPECT_EQ(structure.vacancies[0].position, original.vacancies[0].position);
			EXPECT_EQ(structure.vacancies[0].fractional, original.vacancies[0].fractional);
			EXPECT_EQ(structure.vacancies[0].sourceSpecies, original.vacancies[0].sourceSpecies);
			ASSERT_TRUE(structure.defectFrame);
			EXPECT_EQ(structure.defectFrame->origin, original.defectFrame->origin);
			ASSERT_EQ(structure.bonds.size(), 1u);
			EXPECT_EQ(structure.bonds[0].origin, BondOrigin::Manual);
			EXPECT_EQ(structure.bonds[0].lengthAngstrom, original.bonds[0].lengthAngstrom);
			EXPECT_EQ(structure.bondSettings.perPairCutoffOverride, original.bondSettings.perPairCutoffOverride);
			EXPECT_EQ(structure.name, original.name);
		}

		Ref<DomainLayer> domain = CreateRef<DomainLayer>();
		Ref<RendererLayer> renderer = CreateRef<RendererLayer>(CellTestConfig());
		Ref<UndoStack> stack = CreateRef<UndoStack>();
		CommandRegistry registry;
		Ref<const StructureRecord> record;
		CrystalStructure original;
	};

	TEST_F(RendererCellEditCommandTests, RepeatOneOneTwoIsOneUndoStepAndRedoRestoresSixteenAtoms)
	{
		AddWindow("cell-test-second");
		for (auto &window : renderer->GetWindows())
		{
			window.selectedAtomIndices = {0, 1};
			window.selectedBondIndices = {0};
			window.selectedVacancies = {0};
			window.defectFrameSelected = true;
			window.transformOrientation = TransformOrientation::Defect;
			(void)AddVacancyLabels(window, {0});
		}
		const auto repeated = BuildSupercell(record->structure, SupercellMatrix::Diagonal(1, 1, 2));
		ASSERT_TRUE(repeated);
		ASSERT_EQ(repeated->atoms.size(), 16u);
		EXPECT_TRUE(repeated->vacancies.empty());
		EXPECT_FALSE(repeated->defectFrame);
		const auto result = Replace(*repeated, true);
		ASSERT_TRUE(result);
		EXPECT_TRUE(result->pushedToUndoStack);
		EXPECT_EQ(stack->GetUndoDepth(), 1u);
		EXPECT_TRUE(record->structureFileDirty);
		EXPECT_EQ(record->structure.cell.vectors[2], original.cell.vectors[2] * 2.0f);
		EXPECT_EQ(record->structure.atoms.size(), 16u);
		EXPECT_FALSE(record->structure.bonds.empty());
		const auto after = record->structure;
		for (const auto &window : renderer->GetWindows())
		{
			EXPECT_EQ(window.structure.atoms.size(), 16u);
			EXPECT_TRUE(window.selectedAtomIndices.empty());
			EXPECT_TRUE(window.selectedBondIndices.empty());
			EXPECT_TRUE(window.selectedVacancies.empty());
			EXPECT_FALSE(window.defectFrameSelected);
			EXPECT_EQ(window.transformOrientation, TransformOrientation::Global);
			ASSERT_EQ(window.freeLabels.size(), 1u);
			EXPECT_FALSE(window.freeLabels[0].anchorVacancy);
		}
		ASSERT_TRUE(stack->Undo());
		ExpectOriginal();
		for (const auto &window : renderer->GetWindows())
		{
			EXPECT_EQ(window.structure.atoms.size(), 8u);
			EXPECT_EQ(window.transformOrientation, TransformOrientation::Defect);
			ASSERT_EQ(window.freeLabels.size(), 1u);
			EXPECT_EQ(window.freeLabels[0].anchorVacancy, 0u);
		}
		ASSERT_TRUE(stack->Redo());
		EXPECT_EQ(stack->GetUndoDepth(), 1u);
		EXPECT_EQ(record->structure.atoms.size(), 16u);
		EXPECT_EQ(record->structure.cell.vectors, after.cell.vectors);
		for (std::size_t index = 0; index < after.atoms.size(); ++index)
			EXPECT_EQ(record->structure.atoms[index].position, after.atoms[index].position);
	}

	TEST_F(RendererCellEditCommandTests, CellEditAndWrapUseTheSameCommandAndUndoExactly)
	{
		auto cell = original.cell;
		cell.vectors[2] *= 2.0f;
		const auto edited = WithCell(original, cell, CellEditPositions::KeepFractional);
		ASSERT_TRUE(edited);
		ASSERT_TRUE(Replace(*edited));
		EXPECT_EQ(record->structure.atoms[4].fractional, original.atoms[4].fractional);
		EXPECT_FLOAT_EQ(record->structure.atoms[4].position.z, 2.0f * original.atoms[4].position.z);
		ASSERT_TRUE(stack->Undo());
		ExpectOriginal();

		auto outside = original;
		outside.atoms[0].fractional.x += 1.0f;
		outside.atoms[0].position = outside.FractionalToCartesian(outside.atoms[0].fractional);
		ASSERT_TRUE(Replace(outside));
		std::vector<std::size_t> coincident;
		ASSERT_TRUE(Replace(WrapIntoCell(record->structure, coincident)));
		EXPECT_EQ(record->structure.atoms.size(), 8u);
		EXPECT_EQ(record->structure.atoms[0].fractional.x, 0.0f);
		ASSERT_TRUE(stack->Undo());
		EXPECT_EQ(record->structure.atoms[0].position, outside.atoms[0].position);
		EXPECT_EQ(record->structure.atoms[0].fractional, outside.atoms[0].fractional);
		ASSERT_TRUE(stack->Redo());
		EXPECT_EQ(record->structure.atoms[0].fractional.x, 0.0f);
	}

	TEST_F(RendererCellEditCommandTests, InvalidReplacementDoesNotMutateTheStructureOrHistory)
	{
		auto invalid = original;
		invalid.cell.vectors[2] = invalid.cell.vectors[1];
		EXPECT_FALSE(Replace(invalid));
		invalid = original;
		invalid.isPeriodic = false;
		EXPECT_FALSE(Replace(invalid));
		ExpectOriginal();
		EXPECT_EQ(stack->GetUndoDepth(), 0u);
		EXPECT_EQ(record->revision, 0);
		EXPECT_FALSE(record->structureFileDirty);
	}

	TEST_F(RendererCellEditCommandTests, UndoRejectsAReusedWindowWithoutOverwritingItsNewStructure)
	{
		auto cell = original.cell;
		cell.vectors[2] *= 2.0f;
		const auto edited = WithCell(original, cell, CellEditPositions::KeepFractional);
		ASSERT_TRUE(edited);
		ASSERT_TRUE(Replace(*edited));
		auto other = original;
		other.name = "another structure";
		const auto otherRecord = domain->Workspace().Structures().Add(other);
		renderer->GetWindows().front().structure = BuildRendererStructureData(other, {}, "other", {}, ToString(otherRecord->id));
		EXPECT_FALSE(stack->Undo());
		EXPECT_EQ(stack->GetUndoDepth(), 1u);
		EXPECT_EQ(otherRecord->structure.name, "another structure");
		EXPECT_EQ(otherRecord->structure.cell.vectors, original.cell.vectors);
	}
} // namespace DefectStudio::Tests
