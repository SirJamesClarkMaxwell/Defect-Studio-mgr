#include "Core/dspch.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#include "Domain/Crystal/CellEditing.hpp"
#include "Domain/Crystal/LatticeBasisExpansion.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		CrystalStructure Zincblende()
		{
			CrystalStructure structure;
			structure.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{5.43f});
			AtomSite zinc;
			zinc.species = "Zn";
			AtomSite sulfur;
			sulfur.species = "S";
			sulfur.fractional = glm::vec3(0.25f);
			structure.atoms = ExpandBasisOverLattice(std::array{zinc, sulfur}, BravaisCenteringPreset::FaceCentered);
			for (auto &atom : structure.atoms)
				atom.position = structure.FractionalToCartesian(atom.fractional);
			return structure;
		}

		void ExpectVectorNear(const glm::vec3 &actual, const glm::vec3 &expected)
		{
			for (int axis = 0; axis < 3; ++axis)
				EXPECT_NEAR(actual[axis], expected[axis], 1e-4f);
		}
	}

	TEST(CellEditingTests, ZincblendeCellChangeKeepsFractionalOrCartesianPositions)
	{
		const auto structure = Zincblende();
		auto cell = structure.cell;
		cell.vectors[2] *= 2.0f;
		const auto fractional = WithCell(structure, cell, CellEditPositions::KeepFractional);
		const auto cartesian = WithCell(structure, cell, CellEditPositions::KeepCartesian);
		ASSERT_TRUE(fractional);
		ASSERT_TRUE(cartesian);
		ASSERT_EQ(fractional->atoms.size(), 8u);
		for (std::size_t index = 0; index < structure.atoms.size(); ++index)
		{
			const auto &atom = structure.atoms[index];
			EXPECT_EQ(fractional->atoms[index].fractional, atom.fractional);
			EXPECT_NEAR(fractional->atoms[index].position.z, 2.0f * atom.position.z, 1e-4f);
			EXPECT_EQ(cartesian->atoms[index].position, atom.position);
			ExpectVectorNear(cartesian->atoms[index].fractional, atom.fractional * glm::vec3(1, 1, 0.5f));
			ExpectVectorNear(fractional->FractionalToCartesian(fractional->atoms[index].fractional), fractional->atoms[index].position);
			ExpectVectorNear(cartesian->FractionalToCartesian(cartesian->atoms[index].fractional), cartesian->atoms[index].position);
		}
	}

	TEST(CellEditingTests, SingularAndNonFiniteCellsAreRejectedWithoutChangingInput)
	{
		const auto structure = Zincblende();
		auto cell = structure.cell;
		cell.vectors[2] = cell.vectors[1];
		EXPECT_FALSE(WithCell(structure, cell, CellEditPositions::KeepFractional));
		cell.vectors[2].z = std::numeric_limits<float>::infinity();
		EXPECT_FALSE(WithCell(structure, cell, CellEditPositions::KeepCartesian));
		cell.vectors[2].z = std::numeric_limits<float>::quiet_NaN();
		EXPECT_FALSE(WithCell(structure, cell, CellEditPositions::KeepFractional));
		auto invalidAtom = structure;
		invalidAtom.atoms[0].fractional.x = std::numeric_limits<float>::quiet_NaN();
		EXPECT_FALSE(WithCell(invalidAtom, structure.cell, CellEditPositions::KeepCartesian));
		EXPECT_EQ(structure.atoms.size(), 8u);
		EXPECT_FLOAT_EQ(structure.cell.vectors[2].z, 5.43f);
	}

	TEST(CellEditingTests, VacanciesAndDefectOriginFollowThePositionPolicyAndKeepMetadata)
	{
		auto structure = Zincblende();
		VacancySite vacancy;
		vacancy.fractional = glm::vec3(0.1f, 0.2f, 0.3f);
		vacancy.position = structure.FractionalToCartesian(vacancy.fractional);
		vacancy.sourceSpecies = "Zn";
		vacancy.label = "V_Zn";
		vacancy.hidden = true;
		vacancy.color = glm::vec3(0.2f, 0.4f, 0.6f);
		structure.vacancies.push_back(vacancy);
		structure.defectFrame = DefectFrame{};
		structure.defectFrame->origin = vacancy.position;
		auto cell = structure.cell;
		cell.vectors[2] *= 2.0f;
		for (const auto policy : {CellEditPositions::KeepFractional, CellEditPositions::KeepCartesian})
		{
			const auto changed = WithCell(structure, cell, policy);
			ASSERT_TRUE(changed);
			ASSERT_EQ(changed->vacancies.size(), 1u);
			ASSERT_TRUE(changed->defectFrame);
			const auto &site = changed->vacancies.front();
			EXPECT_EQ(site.label, vacancy.label);
			EXPECT_EQ(site.sourceSpecies, vacancy.sourceSpecies);
			EXPECT_EQ(site.color, vacancy.color);
			EXPECT_TRUE(site.hidden);
			ExpectVectorNear(site.position, changed->FractionalToCartesian(site.fractional));
			ExpectVectorNear(changed->defectFrame->origin, site.position);
			EXPECT_EQ(changed->defectFrame->x, structure.defectFrame->x);
			if (policy == CellEditPositions::KeepFractional)
			{
				EXPECT_EQ(site.fractional, vacancy.fractional);
				EXPECT_NEAR(site.position.z, 2.0f * vacancy.position.z, 1e-4f);
			}
			else
				EXPECT_EQ(site.position, vacancy.position);
		}
	}

	TEST(CellEditingTests, ParametersRoundTripRotatedHexagonalTriclinicAndLeftHandedCells)
	{
		const glm::mat3 rotation(glm::rotate(glm::mat4(1.0f), 0.73f, glm::normalize(glm::vec3(1, 2, 3))));
		for (const auto parameters : {LatticeParameters{2.7f, 2.7f, 8.0f, 90, 90, 120},
			LatticeParameters{3.1f, 4.2f, 5.3f, 73, 82, 67}})
		{
			for (const float handedness : {1.0f, -1.0f})
			{
				auto cell = BuildLatticeCell(CrystalSystem::Triclinic, parameters);
				cell.vectors[2].z *= handedness;
				for (auto &vector : cell.vectors)
					vector = rotation * vector;
				const auto rebuilt = CellFromParameters(cell, ParametersOf(cell));
				for (int axis = 0; axis < 3; ++axis)
					ExpectVectorNear(rebuilt.vectors[axis], cell.vectors[axis]);
				auto edited = ParametersOf(cell);
				edited.c *= 2.0f;
				const auto stretched = CellFromParameters(cell, edited);
				EXPECT_EQ(stretched.vectors[0], cell.vectors[0]);
				EXPECT_EQ(stretched.vectors[1], cell.vectors[1]);
				ExpectVectorNear(stretched.vectors[2], cell.vectors[2] * 2.0f);
			}
		}
	}

	TEST(CellEditingTests, InvalidParametersAreRejectedByWithCell)
	{
		const auto structure = Zincblende();
		for (const auto parameters : {LatticeParameters{0.0f}, LatticeParameters{-1.0f},
			LatticeParameters{1, 1, 1, 0, 90, 90}, LatticeParameters{1, 1, 1, 90, 90, 180},
			LatticeParameters{1, 1, 1, 10, 10, 170}, LatticeParameters{std::numeric_limits<float>::quiet_NaN()}})
		{
			const auto cell = CellFromParameters(structure.cell, parameters);
			EXPECT_FALSE(WithCell(structure, cell, CellEditPositions::KeepFractional));
		}
	}

	TEST(CellEditingTests, WrapReportsCoincidentAtomsAndPreservesSitesAndVacancyMetadata)
	{
		auto structure = Zincblende();
		structure.atoms.resize(2);
		structure.atoms[0].fractional = glm::vec3(1.25f, -0.5f, 0.999999f);
		structure.atoms[1].fractional = glm::vec3(0.25f, 0.5f, 0.999999f);
		structure.atoms[0].label = "outside";
		for (auto &atom : structure.atoms)
			atom.position = structure.FractionalToCartesian(atom.fractional);
		VacancySite vacancy;
		vacancy.fractional = glm::vec3(2, -1, -1e-8f);
		vacancy.position = structure.FractionalToCartesian(vacancy.fractional);
		vacancy.label = "vacancy";
		structure.vacancies.push_back(vacancy);
		structure.defectFrame = DefectFrame{};
		structure.defectFrame->origin = glm::vec3(12, 13, 14);
		std::vector<std::size_t> coincident = {99};
		const auto wrapped = WrapIntoCell(structure, coincident);
		ASSERT_EQ(wrapped.atoms.size(), 2u);
		EXPECT_EQ(coincident, (std::vector<std::size_t>{0, 1}));
		EXPECT_NEAR(wrapped.atoms[0].fractional.x, 0.25f, 1e-6f);
		EXPECT_NEAR(wrapped.atoms[0].fractional.y, 0.5f, 1e-6f);
		EXPECT_NEAR(wrapped.atoms[0].fractional.z, 0.999999f, 1e-6f);
		EXPECT_EQ(wrapped.atoms[0].label, "outside");
		ASSERT_EQ(wrapped.vacancies.size(), 1u);
		EXPECT_EQ(wrapped.vacancies[0].label, "vacancy");
		EXPECT_EQ(wrapped.vacancies[0].fractional, glm::vec3(0));
		EXPECT_EQ(wrapped.defectFrame->origin, structure.defectFrame->origin);
		for (const auto &atom : wrapped.atoms)
			ExpectVectorNear(atom.position, wrapped.FractionalToCartesian(atom.fractional));
		EXPECT_EQ(structure.atoms[0].fractional.x, 1.25f);
	}

	TEST(CellEditingTests, WrapReplacesAnOldCoincidenceReportWithAnEmptyOne)
	{
		std::vector<std::size_t> coincident = {0, 1};
		const auto wrapped = WrapIntoCell(Zincblende(), coincident);
		EXPECT_EQ(wrapped.atoms.size(), 8u);
		EXPECT_TRUE(coincident.empty());
	}

	TEST(CellEditingTests, TwoFccSublatticesInAnEmptyCubicCellGiveZincblende)
	{
		const CrystalStructure empty = EmptyCell(BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{5.43f}));
		EXPECT_TRUE(empty.atoms.empty());
		EXPECT_TRUE(empty.isPeriodic);
		std::vector<std::size_t> coincident;
		const CrystalStructure cations = AddSublattice(empty, "Zn", BravaisCenteringPreset::FaceCentered, glm::vec3(0.0f), coincident);
		ASSERT_EQ(cations.atoms.size(), 4u);
		EXPECT_TRUE(coincident.empty());
		const CrystalStructure built = AddSublattice(cations, "S", BravaisCenteringPreset::FaceCentered, glm::vec3(0.25f), coincident);
		EXPECT_TRUE(coincident.empty());
		const CrystalStructure reference = Zincblende();
		ASSERT_EQ(built.atoms.size(), reference.atoms.size());
		for (const AtomSite &expected : reference.atoms)
		{
			const auto match = std::find_if(built.atoms.begin(), built.atoms.end(), [&](const AtomSite &atom) {
				return atom.species == expected.species && glm::length(atom.fractional - expected.fractional) < 1e-5f;
			});
			EXPECT_NE(match, built.atoms.end()) << expected.species << " at " << expected.fractional.x << " "
				<< expected.fractional.y << " " << expected.fractional.z;
		}
		for (std::size_t i = 0; i < built.atoms.size(); ++i)
		{
			EXPECT_EQ(built.atoms[i].index, static_cast<int>(i));
			ExpectVectorNear(built.atoms[i].position, built.FractionalToCartesian(built.atoms[i].fractional));
		}
	}

	TEST(CellEditingTests, SublatticeOnOccupiedSitesIsReportedAndKept)
	{
		std::vector<std::size_t> coincident;
		const CrystalStructure result = AddSublattice(Zincblende(), "Zn", BravaisCenteringPreset::Primitive, glm::vec3(1.0f, 0.0f, 0.0f), coincident);
		EXPECT_EQ(result.atoms.size(), 9u);
		EXPECT_EQ(coincident.size(), 2u); // the new atom and the Zn already at the origin
		EXPECT_NE(std::find(coincident.begin(), coincident.end(), std::size_t{8}), coincident.end());
	}

	TEST(CellEditingTests, SublatticeOffsetIsWrappedIntoTheCell)
	{
		std::vector<std::size_t> coincident;
		const CrystalStructure empty = EmptyCell(BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{4.0f}));
		const CrystalStructure result = AddSublattice(empty, "C", BravaisCenteringPreset::Primitive, glm::vec3(1.25f, -0.25f, 0.0f), coincident);
		ASSERT_EQ(result.atoms.size(), 1u);
		ExpectVectorNear(result.atoms[0].fractional, glm::vec3(0.25f, 0.75f, 0.0f));
	}
}
