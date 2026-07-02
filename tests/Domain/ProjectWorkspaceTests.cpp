#include <gtest/gtest.h>

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
		EXPECT_EQ(record.id, "structure-1");
		EXPECT_EQ(record.displayName, "Silicon");
		EXPECT_EQ(record.sourcePath.String(), "POSCAR");
		ASSERT_NE(workspace.Structures().Find(record.id), nullptr);
		EXPECT_EQ(workspace.Structures().Find(record.id)->structure.name, "Si");
	}
} // namespace DefectStudio::Tests
