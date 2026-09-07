#include <gtest/gtest.h>

#include <regex>
#include <utility>

#include "Domain/ProjectWorkspace.hpp"

namespace DefectStudio::Tests
{
	TEST(ProjectWorkspaceTests, RegistersStructuresWithStableIds)
	{
		ProjectWorkspace workspace;
		CrystalStructure structure;
		structure.name = "Si";

		const Ref<const StructureRecord> record = workspace.Structures().Add(
			std::move(structure),
			Path("POSCAR"),
			"Silicon");

		ASSERT_EQ(workspace.Structures().Records().size(), 1u);
		EXPECT_FALSE(record->id.is_nil());
		EXPECT_TRUE(std::regex_match(ToString(record->id), std::regex(
			R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})")));
		EXPECT_EQ(record->displayName, "Silicon");
		EXPECT_EQ(record->sourcePath.String(), "POSCAR");
		const Ref<const StructureRecord> found = workspace.Structures().Find(record->id).lock();
		ASSERT_NE(found, nullptr);
		EXPECT_EQ(found->structure.name, "Si");
	}

	TEST(ProjectWorkspaceTests, RegistersStructuresWithUniqueUuidIds)
	{
		ProjectWorkspace workspace;

		const Ref<const StructureRecord> first = workspace.Structures().Add(CrystalStructure{});
		const Ref<const StructureRecord> second = workspace.Structures().Add(CrystalStructure{});

		EXPECT_NE(first->id, second->id);
	}

	TEST(ProjectWorkspaceTests, RegistersDefectsConfigurationsAndCalculations)
	{
		ProjectWorkspace workspace;
		const Ref<const StructureRecord> structure = workspace.Structures().Add(CrystalStructure{});

		DefectConcept defectConcept;
		defectConcept.displayName = "V_C";
		defectConcept.type = PointDefectType::Vacancy;
		const Ref<const DefectConceptRecord> defect = workspace.Defects().Add(std::move(defectConcept));

		DefectConfiguration configuration;
		configuration.defectId = defect->id;
		configuration.pristineStructureId = structure->id;
		configuration.displayName = "neutral vacancy";
		const Ref<const DefectConfigurationRecord> configurationRecord =
			workspace.DefectConfigurations().Add(std::move(configuration));

		CalculationRecord calculation;
		calculation.inputStructureId = structure->id;
		calculation.defectConfigurationId = configurationRecord->id;
		calculation.displayName = "single point";
		const Ref<const CalculationRecord> calculationRecord = workspace.Calculations().Add(std::move(calculation));

		EXPECT_FALSE(defect->id.is_nil());
		EXPECT_FALSE(configurationRecord->id.is_nil());
		EXPECT_FALSE(calculationRecord->id.is_nil());
		EXPECT_EQ(workspace.Defects().Find(defect->id).lock(), defect);
		EXPECT_EQ(workspace.DefectConfigurations().Find(configurationRecord->id).lock(), configurationRecord);
		EXPECT_EQ(workspace.Calculations().Find(calculationRecord->id).lock(), calculationRecord);
	}

	TEST(ProjectWorkspaceTests, RegistersProjectMemberWithSourcePath)
	{
		ProjectWorkspace workspace;
		CrystalStructure structure;
		structure.name = "GaAs";

		const Ref<const StructureRecord> record = workspace.Structures().RegisterAsProjectMember(
			std::move(structure),
			Path("project/structures/GaAs/POSCAR"),
			"GaAs bulk");

		ASSERT_NE(record, nullptr);
		EXPECT_FALSE(record->id.is_nil());
		EXPECT_EQ(record->displayName, "GaAs bulk");
		EXPECT_EQ(record->sourcePath.String(), "project/structures/GaAs/POSCAR");
		EXPECT_EQ(record->revision, 0);
		EXPECT_EQ(record->savedRevision, 0);

		const Ref<const StructureRecord> found = workspace.Structures().Find(record->id).lock();
		ASSERT_NE(found, nullptr);
		EXPECT_EQ(found->structure.name, "GaAs");
	}

	TEST(ProjectWorkspaceTests, UpdateSourcePathOnExistingStructure)
	{
		ProjectWorkspace workspace;
		const Ref<const StructureRecord> record = workspace.Structures().RegisterAsProjectMember(
			CrystalStructure{},
			Path("old/path/POSCAR"),
			"structure");

		EXPECT_TRUE(workspace.Structures().UpdateSourcePath(record->id, Path("new/path/POSCAR")));

		const Ref<const StructureRecord> updated = workspace.Structures().Find(record->id).lock();
		ASSERT_NE(updated, nullptr);
		EXPECT_EQ(updated->sourcePath.String(), "new/path/POSCAR");
	}

	TEST(ProjectWorkspaceTests, UpdateSourcePathReturnsFalseForNonExistentId)
	{
		ProjectWorkspace workspace;
		const StructureId nonExistentId = GenerateUuid();

		EXPECT_FALSE(workspace.Structures().UpdateSourcePath(nonExistentId, Path("some/path")));
	}

	TEST(ProjectWorkspaceTests, UpdateSavedRevisionOnExistingStructure)
	{
		ProjectWorkspace workspace;
		const Ref<const StructureRecord> record = workspace.Structures().RegisterAsProjectMember(
			CrystalStructure{},
			Path("POSCAR"),
			"structure");

		EXPECT_TRUE(workspace.Structures().UpdateSavedRevision(record->id, 5));

		const Ref<const StructureRecord> updated = workspace.Structures().Find(record->id).lock();
		ASSERT_NE(updated, nullptr);
		EXPECT_EQ(updated->savedRevision, 5);
	}

	TEST(ProjectWorkspaceTests, UpdateSavedRevisionReturnsFalseForNonExistentId)
	{
		ProjectWorkspace workspace;
		const StructureId nonExistentId = GenerateUuid();

		EXPECT_FALSE(workspace.Structures().UpdateSavedRevision(nonExistentId, 5));
	}
} // namespace DefectStudio::Tests
