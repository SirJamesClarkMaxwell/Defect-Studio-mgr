#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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
		structure.atoms.push_back(AtomSite{"Si", glm::vec3(0.0f), glm::vec3(0.0f), 0});

		const Result<StructureId> result = workspace.Structures().RegisterAsProjectMember(
			std::move(structure),
			Path("POSCAR"),
			"Silicon");

		ASSERT_TRUE(result);
		const StructureId id = result.Value();
		const Ref<const StructureRecord> record = workspace.Structures().Find(id).lock();

		ASSERT_EQ(workspace.Structures().Records().size(), 1u);
		EXPECT_FALSE(id.is_nil());
		EXPECT_TRUE(std::regex_match(ToString(id), std::regex(
			R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})")));
		EXPECT_EQ(record->displayName, "Silicon");
		EXPECT_EQ(record->sourcePath.String(), "POSCAR");
		ASSERT_NE(record, nullptr);
		EXPECT_EQ(record->structure.name, "Si");
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
		structure.atoms.push_back(AtomSite{"Ga", glm::vec3(0.0f), glm::vec3(0.0f), 0});

		const Result<StructureId> result = workspace.Structures().RegisterAsProjectMember(
			std::move(structure),
			Path("project/structures/GaAs/POSCAR"),
			"GaAs bulk");

		ASSERT_TRUE(result);
		const StructureId id = result.Value();
		EXPECT_FALSE(id.is_nil());

		const Ref<const StructureRecord> record = workspace.Structures().Find(id).lock();
		ASSERT_NE(record, nullptr);
		EXPECT_EQ(record->displayName, "GaAs bulk");
		EXPECT_EQ(record->sourcePath.String(), "project/structures/GaAs/POSCAR");
		EXPECT_EQ(record->revision, 0);
		EXPECT_EQ(record->savedRevision, 0);
		EXPECT_EQ(record->structure.name, "GaAs");
	}

	TEST(ProjectWorkspaceTests, UpdateSourcePathOnExistingStructure)
	{
		ProjectWorkspace workspace;
		CrystalStructure structure;
		structure.atoms.push_back(AtomSite{"Si", glm::vec3(0.0f), glm::vec3(0.0f), 0});
		const Result<StructureId> result = workspace.Structures().RegisterAsProjectMember(
			std::move(structure),
			Path("old/path/POSCAR"),
			"structure");

		ASSERT_TRUE(result);
		const StructureId id = result.Value();

		EXPECT_TRUE(workspace.Structures().UpdateSourcePath(id, Path("new/path/POSCAR")));

		const Ref<const StructureRecord> updated = workspace.Structures().Find(id).lock();
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
		CrystalStructure structure;
		structure.atoms.push_back(AtomSite{"Si", glm::vec3(0.0f), glm::vec3(0.0f), 0});
		const Result<StructureId> result = workspace.Structures().RegisterAsProjectMember(
			std::move(structure),
			Path("POSCAR"),
			"structure");

		ASSERT_TRUE(result);
		const StructureId id = result.Value();

		EXPECT_TRUE(workspace.Structures().UpdateSavedRevision(id, 5));

		const Ref<const StructureRecord> updated = workspace.Structures().Find(id).lock();
		ASSERT_NE(updated, nullptr);
		EXPECT_EQ(updated->savedRevision, 5);
	}

	TEST(ProjectWorkspaceTests, UpdateSavedRevisionReturnsFalseForNonExistentId)
	{
		ProjectWorkspace workspace;
		const StructureId nonExistentId = GenerateUuid();

		EXPECT_FALSE(workspace.Structures().UpdateSavedRevision(nonExistentId, 5));
	}
	TEST(ProjectWorkspaceTests, RejectsASecondRegistrationOfAFileThatIsStillOnDisk)
	{
		const FilePath directory = std::filesystem::temp_directory_path() / ("ds_workspace_dup_" + ToString(GenerateUuid()));
		std::filesystem::create_directories(directory);
		const FilePath poscar = directory / "POSCAR";
		std::ofstream(poscar) << "placeholder";

		ProjectWorkspace workspace;
		CrystalStructure structure;
		structure.name = "Si";
		structure.atoms.push_back(AtomSite{"Si", glm::vec3(0.0f), glm::vec3(0.0f), 0});

		EXPECT_TRUE(workspace.Structures().RegisterAsProjectMember(structure, Path::FromResolved(poscar), "Si"));
		const Result<StructureId> second =
			workspace.Structures().RegisterAsProjectMember(structure, Path::FromResolved(poscar), "Si again");

		EXPECT_FALSE(second);
		EXPECT_EQ(workspace.Structures().Records().size(), 1u);

		std::error_code error;
		std::filesystem::remove_all(directory, error);
	}

	// What StructureLifecycleCoordinator's pre-flight does with a record whose file the user has
	// deleted from the Project Tree: forget it, so the same path can be registered again.
	TEST(ProjectWorkspaceTests, RemovedRecordStopsBlockingItsSourcePath)
	{
		const FilePath directory = std::filesystem::temp_directory_path() / ("ds_workspace_stale_" + ToString(GenerateUuid()));
		std::filesystem::create_directories(directory);
		const FilePath poscar = directory / "POSCAR";
		std::ofstream(poscar) << "placeholder";

		ProjectWorkspace workspace;
		CrystalStructure structure;
		structure.name = "Si";
		structure.atoms.push_back(AtomSite{"Si", glm::vec3(0.0f), glm::vec3(0.0f), 0});

		const Result<StructureId> first =
			workspace.Structures().RegisterAsProjectMember(structure, Path::FromResolved(poscar), "Si");
		ASSERT_TRUE(first);
		ASSERT_FALSE(workspace.Structures().RegisterAsProjectMember(structure, Path::FromResolved(poscar), "again"));

		EXPECT_TRUE(workspace.Structures().Remove(first.Value()));
		EXPECT_FALSE(workspace.Structures().Remove(first.Value())); // idempotent-ish: unknown id is false
		EXPECT_TRUE(workspace.Structures().Find(first.Value()).expired());

		const Result<StructureId> second =
			workspace.Structures().RegisterAsProjectMember(structure, Path::FromResolved(poscar), "Si re-added");
		ASSERT_TRUE(second);
		EXPECT_NE(second.Value(), first.Value());
		EXPECT_EQ(workspace.Structures().Records().size(), 1u);

		std::error_code error;
		std::filesystem::remove_all(directory, error);
	}
} // namespace DefectStudio::Tests
