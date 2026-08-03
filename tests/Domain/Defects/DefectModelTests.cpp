#include <gtest/gtest.h>

#include "Domain/Defects/DefectModel.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] CrystalStructure BuildTwoAtomStructure()
		{
			CrystalStructure structure;
			structure.atoms = {
				AtomSite{"C", glm::vec3(0.0f), glm::vec3(0.0f), 0},
				AtomSite{"O", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f), 1}};
			structure.bonds.push_back(Bond{0u, 1u, 1.0f, BondOrigin::Manual, true});
			return structure;
		}
	} // namespace

	TEST(DefectModelTests, VacancyProducesDefectedSnapshotWithoutMutatingPristine)
	{
		const CrystalStructure pristine = BuildTwoAtomStructure();
		DefectConfiguration configuration;
		configuration.operations.push_back(PointDefectOperation{PointDefectType::Vacancy, 0u});

		const Result<CrystalStructure> result = BuildDefectedStructure(pristine, configuration);

		ASSERT_TRUE(result);
		EXPECT_EQ(pristine.atoms.size(), 2u);
		ASSERT_EQ(result->atoms.size(), 1u);
		EXPECT_EQ(result->atoms[0].species, "O");
		ASSERT_EQ(result->vacancies.size(), 1u);
		EXPECT_EQ(result->vacancies[0].sourceSpecies, "C");
		EXPECT_EQ(result->vacancies[0].GetLabel(), "V_C");
		EXPECT_TRUE(result->bonds.empty());
	}

	TEST(DefectModelTests, InterstitialAddsAtomToDefectedSnapshot)
	{
		const CrystalStructure pristine = BuildTwoAtomStructure();
		DefectConfiguration configuration;
		PointDefectOperation operation;
		operation.type = PointDefectType::Interstitial;
		operation.atom = AtomSite{"H", glm::vec3(0.5f), glm::vec3(0.25f), 0};
		configuration.operations.push_back(operation);

		const Result<CrystalStructure> result = BuildDefectedStructure(pristine, configuration);

		ASSERT_TRUE(result);
		ASSERT_EQ(result->atoms.size(), 3u);
		EXPECT_EQ(result->atoms[2].species, "H");
		EXPECT_EQ(result->atoms[2].index, 2);
	}

	TEST(DefectModelTests, ReplacementChangesSpeciesInDefectedSnapshot)
	{
		const CrystalStructure pristine = BuildTwoAtomStructure();
		DefectConfiguration configuration;
		PointDefectOperation operation;
		operation.type = PointDefectType::SubstitutionalDopant;
		operation.atomIndex = 1u;
		operation.replacementSpecies = "N";
		configuration.operations.push_back(operation);

		const Result<CrystalStructure> result = BuildDefectedStructure(pristine, configuration);

		ASSERT_TRUE(result);
		ASSERT_EQ(result->atoms.size(), 2u);
		EXPECT_EQ(pristine.atoms[1].species, "O");
		EXPECT_EQ(result->atoms[1].species, "N");
	}

	TEST(DefectModelTests, InvalidAtomIndexReturnsValidationError)
	{
		const CrystalStructure pristine = BuildTwoAtomStructure();
		DefectConfiguration configuration;
		configuration.operations.push_back(PointDefectOperation{PointDefectType::Vacancy, 5u});

		const Result<CrystalStructure> result = BuildDefectedStructure(pristine, configuration);

		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "domain.defect.invalid_atom_index");
	}
} // namespace DefectStudio::Tests
