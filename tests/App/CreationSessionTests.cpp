#include <gtest/gtest.h>

#include "App/CreationSession.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] CreationSession BccIron()
		{
			CreationSession session;
			session.draftStructure.cell.vectors = {
				glm::vec3(2.87f, 0, 0), glm::vec3(0, 2.87f, 0), glm::vec3(0, 0, 2.87f)};
			session.draftStructure.atoms = {
				AtomSite{"Fe", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0},
				AtomSite{"Fe", glm::vec3(1.435f, 1.435f, 1.435f), glm::vec3(0.5f, 0.5f, 0.5f), 1}};
			return session;
		}
	} // namespace

	TEST(CreationSessionTests, ExportStructureIsTheUnitCellWhenTheSupercellIsOneByOneByOne)
	{
		const CreationSession session = BccIron();
		EXPECT_EQ(BuildSessionExportStructure(session).atoms.size(), 2u);
	}

	// The bug this guards: Add to Project wrote draftStructure and ignored the supercell counts
	// entirely, so a 2x2x2 request produced the 2-atom conventional cell shown in the middle pane.
	TEST(CreationSessionTests, ExportStructureExpandsToTheRequestedSupercell)
	{
		CreationSession session = BccIron();
		session.supercellCounts = glm::ivec3(2, 2, 2);

		const CrystalStructure exported = BuildSessionExportStructure(session);

		EXPECT_EQ(exported.atoms.size(), 16u);
		EXPECT_NEAR(glm::length(exported.cell.vectors[0]), 5.74f, 1e-4f);
	}

	TEST(CreationSessionTests, ExportStructureOfAnEmptyDraftStaysEmpty)
	{
		CreationSession session;
		session.supercellCounts = glm::ivec3(3, 3, 3);
		EXPECT_TRUE(BuildSessionExportStructure(session).atoms.empty());
	}
} // namespace DefectStudio::Tests
