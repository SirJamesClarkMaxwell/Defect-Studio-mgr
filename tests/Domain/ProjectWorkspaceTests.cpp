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

		const StructureRecord &record = workspace.Structures().Add(
			std::move(structure),
			Path("POSCAR"),
			"Silicon");

		ASSERT_EQ(workspace.Structures().Records().size(), 1u);
		EXPECT_FALSE(record.id.is_nil());
		EXPECT_TRUE(std::regex_match(ToString(record.id), std::regex(
			R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})")));
		EXPECT_EQ(record.displayName, "Silicon");
		EXPECT_EQ(record.sourcePath.String(), "POSCAR");
		ASSERT_NE(workspace.Structures().Find(record.id), nullptr);
		EXPECT_EQ(workspace.Structures().Find(record.id)->structure.name, "Si");
	}

	TEST(ProjectWorkspaceTests, RegistersStructuresWithUniqueUuidIds)
	{
		ProjectWorkspace workspace;

		const StructureRecord &first = workspace.Structures().Add(CrystalStructure{});
		const StructureRecord &second = workspace.Structures().Add(CrystalStructure{});

		EXPECT_NE(first.id, second.id);
	}

	TEST(ProjectWorkspaceTests, RegistersDefectsConfigurationsAndCalculations)
	{
		ProjectWorkspace workspace;
		const StructureRecord &structure = workspace.Structures().Add(CrystalStructure{});

		DefectConcept defectConcept;
		defectConcept.displayName = "V_C";
		defectConcept.type = PointDefectType::Vacancy;
		const DefectConceptRecord &defect = workspace.Defects().Add(std::move(defectConcept));

		DefectConfiguration configuration;
		configuration.defectId = defect.id;
		configuration.pristineStructureId = structure.id;
		configuration.displayName = "neutral vacancy";
		const DefectConfigurationRecord &configurationRecord =
			workspace.DefectConfigurations().Add(std::move(configuration));

		CalculationRecord calculation;
		calculation.inputStructureId = structure.id;
		calculation.defectConfigurationId = configurationRecord.id;
		calculation.displayName = "single point";
		const CalculationRecord &calculationRecord = workspace.Calculations().Add(std::move(calculation));

		EXPECT_FALSE(defect.id.is_nil());
		EXPECT_FALSE(configurationRecord.id.is_nil());
		EXPECT_FALSE(calculationRecord.id.is_nil());
		EXPECT_EQ(workspace.Defects().Find(defect.id), &defect);
		EXPECT_EQ(workspace.DefectConfigurations().Find(configurationRecord.id), &configurationRecord);
		EXPECT_EQ(workspace.Calculations().Find(calculationRecord.id), &calculationRecord);
	}
} // namespace DefectStudio::Tests
