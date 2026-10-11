#include <gtest/gtest.h>

#include <vector>

#include "Domain/Defects/CalculationCatalogue.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		CalculationMethod Hse(const char *precFock = "normal")
		{
			CalculationMethod method;
			method.hybrid = true;
			method.hfscreen = 0.2;
			method.aexx = 0.25;
			method.precFock = precFock;
			method.encut = 600.0;
			method.prec = "normal";
			method.ispin = 2;
			method.potcars = {"PAW_PBE C 08Apr2002", "PAW_PBE Ge_d 03Jul2007"};
			return method;
		}

		VaspRunRecord Run(const char *path, std::vector<std::string> species, std::vector<int> ions,
			double electrons, CalculationMethod method, bool locpot = true)
		{
			VaspRunRecord record;
			record.directory = Path::FromUtf8(path);
			record.species = std::move(species);
			record.ions = std::move(ions);
			for (const auto &symbol : record.species)
				record.valence.push_back(symbol == "C" ? 4.0 : symbol == "Ge" ? 14.0 : 5.0);
			record.electrons = electrons;
			record.totalEnergy = -5000.0;
			record.finished = true;
			record.relaxation = true;
			record.ionicConverged = true;
			record.method = std::move(method);
			record.hasLocpot = locpot;
			return record;
		}

		// O: as it is: bulk, GeV q0 and q-1 (PBE relaxation with HSE runs inside), Ge_C, references.
		std::vector<VaspRunRecord> Okeanos()
		{
			CalculationMethod pbe = Hse();
			pbe.hybrid = false;
			pbe.precFock.clear();
			return {
				Run("O:/GeV/Stan_0/HSE/PrecFoc_Normal", {"C", "Ge"}, {510, 1}, 2054, Hse()),       // 0
				Run("O:/GeV/Stan_1-", {"C", "Ge", "C"}, {255, 1, 255}, 2055, pbe),                 // 1
				Run("O:/GeV/Stan_1-/HSE/PrecFock_Fast", {"C", "Ge"}, {510, 1}, 2055, Hse("fast")), // 2
				Run("O:/GeV/Stan_1-/HSE/PrecFock_Normal", {"C", "Ge"}, {510, 1}, 2055, Hse()),     // 3
				Run("O:/Ge_C/Stan_0/HSE_Normal", {"C", "Ge"}, {511, 1}, 2058, Hse()),              // 4
				Run("O:/Idealna-Super-Komórka", {"C"}, {512}, 2048, Hse()),                        // 5
				Run("O:/Potencjaly_chemiczne/Ge/Lattice_Constant_Convergance/first", {"Ge"}, {8}, 112, Hse()), // 6
				Run("O:/Potencjaly_chemiczne/N/first", {"N"}, {2}, 10, Hse()),                     // 7
			};
		}
	} // namespace

	TEST(CalculationCatalogueTests, ChargeAndCompositionComeFromTheOutcarNumbers)
	{
		const auto runs = Okeanos();
		EXPECT_EQ(CalculationCharge(runs[3]), std::optional<int>(-1)); // 510*4 + 14 - 2055
		EXPECT_EQ(CalculationCharge(runs[0]), std::optional<int>(0));
		EXPECT_EQ(CalculationCharge(runs[1]), std::optional<int>(-1)); // C listed twice in POTCAR
		EXPECT_EQ(CalculationComposition(runs[1]), (SpeciesCounts{{"C", 510}, {"Ge", 1}}));

		VaspRunRecord fractional = runs[3];
		fractional.electrons = 2054.5;
		EXPECT_FALSE(CalculationCharge(fractional).has_value());
		VaspRunRecord unknown = runs[3];
		unknown.electrons.reset();
		EXPECT_FALSE(CalculationCharge(unknown).has_value());
	}

	TEST(CalculationCatalogueTests, UsableMeansFinishedConvergedAndWithAnEnergy)
	{
		auto runs = Okeanos();
		EXPECT_TRUE(CalculationUsable(runs[3]));
		runs[3].ionicConverged = false;
		EXPECT_FALSE(CalculationUsable(runs[3]));
		runs[3].relaxation = false; // a static run has no ionic convergence to reach
		EXPECT_TRUE(CalculationUsable(runs[3]));
		runs[3].finished = false;
		EXPECT_FALSE(CalculationUsable(runs[3]));
		runs[0].totalEnergy.reset();
		EXPECT_FALSE(CalculationUsable(runs[0]));
		runs[6].error = "no POTCAR titles";
		EXPECT_FALSE(CalculationUsable(runs[6]));
	}

	TEST(CalculationCatalogueTests, MethodMismatchesNameEverySetting)
	{
		EXPECT_TRUE(MethodMismatches(Hse(), Hse()).empty());
		CalculationMethod other = Hse("fast");
		other.encut = 520.0;
		other.hybrid = false;
		EXPECT_EQ(MethodMismatches(Hse(), other).size(), 3u); // hybrid, PRECFOCK, ENCUT
		CalculationMethod otherPaw = Hse();
		otherPaw.potcars = {"PAW_PBE C_h 06Feb2004"};
		EXPECT_EQ(MethodMismatches(Hse(), otherPaw).size(), 1u);
		CalculationMethod nitrogen = Hse();
		nitrogen.potcars = {"PAW_PBE N 08Apr2002"}; // no species in common: nothing to compare
		EXPECT_TRUE(MethodMismatches(Hse(), nitrogen).empty());
		CalculationMethod spin = Hse();
		spin.ispin = 1; // a closed-shell bulk next to a spin-polarised defect is fine
		EXPECT_TRUE(MethodMismatches(Hse(), spin).empty());
	}

	TEST(CalculationCatalogueTests, TheBulkIsTheLargestCellWithOnlyTheHostElements)
	{
		EXPECT_EQ(BulkCandidates(Okeanos()), (std::vector<std::size_t>{5}));
	}

	TEST(CalculationCatalogueTests, DefectRunsAreGroupedByFolderAndCharge)
	{
		const auto runs = Okeanos();
		const auto groups = GroupDefectRuns(runs, 5, Path::FromUtf8("O:/"));
		ASSERT_EQ(groups.size(), 3u);
		EXPECT_EQ(groups[0].defect, "GeV");
		EXPECT_EQ(groups[0].charge, 0);
		EXPECT_EQ(groups[0].runs, (std::vector<std::size_t>{0}));
		EXPECT_EQ(groups[0].addedAtoms, (SpeciesCounts{{"C", -2}, {"Ge", 1}}));
		EXPECT_EQ(groups[1].defect, "GeV");
		EXPECT_EQ(groups[1].charge, -1);
		EXPECT_EQ(groups[1].runs, (std::vector<std::size_t>{1, 2, 3}));
		EXPECT_EQ(groups[2].defect, "Ge_C");
		EXPECT_EQ(groups[2].addedAtoms, (SpeciesCounts{{"C", -1}, {"Ge", 1}}));

		// Scanning one defect folder: its charge folders are below it, so the defect is its name.
		const auto one = GroupDefectRuns(runs, 5, Path::FromUtf8("O:/GeV"));
		ASSERT_EQ(one.size(), 2u); // Ge_C is outside
		EXPECT_EQ(one[0].defect, "GeV");
		EXPECT_EQ(one[1].charge, -1);

		EXPECT_EQ(ChargeFromFolderName("Stan_1-"), std::optional<int>(-1));
		EXPECT_EQ(ChargeFromFolderName("stan_+1"), std::optional<int>(1));
		EXPECT_EQ(ChargeFromFolderName("q-2"), std::optional<int>(-2));
		EXPECT_FALSE(ChargeFromFolderName("HSE_Normal").has_value());
	}

	TEST(CalculationCatalogueTests, TheProposalIsTheConvergedHybridRunLikeTheThesis)
	{
		const auto runs = Okeanos();
		const auto groups = GroupDefectRuns(runs, 5, Path::FromUtf8("O:/"));
		const RunProposal minus = ProposeEnergyRun(runs, groups[1], 5);
		EXPECT_EQ(minus.run, std::optional<std::size_t>(3)); // HSE/PrecFock_Normal
		EXPECT_FALSE(minus.reason.empty());

		// Two equally good runs: the user chooses.
		auto twins = runs;
		twins[2].method.precFock = "normal";
		const RunProposal tie = ProposeEnergyRun(twins, groups[1], 5);
		EXPECT_FALSE(tie.run.has_value());
		EXPECT_FALSE(tie.reason.empty());

		// Only an unfinished run: nothing to propose.
		auto broken = runs;
		broken[0].finished = false;
		EXPECT_FALSE(ProposeEnergyRun(broken, groups[0], 5).run.has_value());
	}
} // namespace DefectStudio::Tests
