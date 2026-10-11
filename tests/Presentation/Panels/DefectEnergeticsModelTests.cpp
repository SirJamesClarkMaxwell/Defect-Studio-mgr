#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <map>
#include <sstream>
#include <string>

#include "Presentation/Panels/DefectEnergeticsModel.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		VaspRunRecord Run(const char *path, std::vector<std::string> species, std::vector<int> ions,
			std::vector<double> valence, double electrons, double energy, const char *precFock = "normal")
		{
			VaspRunRecord run;
			run.directory = Path::FromUtf8(path);
			run.species = std::move(species);
			run.ions = std::move(ions);
			run.valence = std::move(valence);
			run.electrons = electrons;
			run.totalEnergy = energy;
			run.finished = true;
			run.relaxation = true;
			run.ionicConverged = true;
			run.method.hybrid = true;
			run.method.hfscreen = 0.2;
			run.method.aexx = 0.25;
			run.method.precFock = precFock;
			run.method.encut = 600.0;
			run.method.prec = "normal";
			run.method.potcars = {"PAW_PBE C 08Apr2002", "PAW_PBE Ge_d 03Jul2007"};
			run.hasLocpot = true;
			return run;
		}

		// The thesis GeV numbers (path A, test-directory/fnv-diamond/analysis/formation/A_sx_thesisC).
		EnergeticsFile Diamond()
		{
			EnergeticsFile file;
			file.root = Path::FromUtf8("O:/");
			file.runs = {
				Run("O:/GeV/Stan_0/HSE/PrecFoc_Normal", {"C", "Ge"}, {510, 1}, {4, 14}, 2054, -5377.00332106),        // 0
				Run("O:/GeV/Stan_1-/HSE/PrecFock_Fast", {"C", "Ge"}, {510, 1}, {4, 14}, 2055, -5366.07, "fast"),     // 1
				Run("O:/GeV/Stan_1-/HSE/PrecFock_Normal", {"C", "Ge"}, {510, 1}, {4, 14}, 2055, -5365.90561526),      // 2
				Run("O:/Idealna-Super-komórka-dwa", {"C"}, {512}, {4}, 2048, -5400.79969151),                         // 3
				Run("O:/Potencjaly_chemiczne/Ge/Lattice_Constant_Convergance/first", {"Ge"}, {8}, {14}, 112, -42.88579995), // 4
			};
			file.runs[3].method.potcars = {"PAW_PBE C 08Apr2002"};
			file.runs[4].method.potcars = {"PAW_PBE Ge_d 03Jul2007"};
			return file;
		}

		// A correction whose residual dV - V_model is 0.0025 eV everywhere, E_lat 0.252685: E_corr =
		// 0.252685 - 0.0025 = 0.250185 for q = -1, as in the thesis.
		FnvCorrectionsFile Fnv()
		{
			PersistedFnvCorrection entry;
			entry.defectLocpot = Path::FromUtf8("O:/GeV/Stan_1-/HSE/PrecFock_Normal/LOCPOT");
			entry.bulkLocpot = Path::FromUtf8("O:/Idealna-Super-komórka-dwa/LOCPOT");
			entry.charge = -1;
			entry.centreFractional = glm::dvec3(0.468756, 0.531242, 0.468762);
			entry.convention = FnvModelConvention::Pymatgen;
			FnvProfiles profiles;
			profiles.charge = -1;
			profiles.dielectric = 5.7;
			profiles.latticeEnergy = 0.252685;
			for (int a = 0; a < 3; ++a)
			{
				auto &axis = profiles.axes[a];
				axis.length = 14.18;
				for (int k = 0; k < 20; ++k)
				{
					axis.position.push_back(k * axis.length / 20.0);
					axis.modelPotential.push_back(-0.01 * k);
					axis.deltaV.push_back(-0.01 * k + 0.0025);
				}
				entry.windows[a] = {axis.length / 2.0, 4.0};
			}
			entry.profiles = profiles;
			FnvCorrectionsFile fnv;
			fnv.corrections = {entry};
			return fnv;
		}

		const VaspRunRecordIO::BandEdges kEdges{9.3518, 14.699255};

		std::vector<std::string> Lines(const std::string &text)
		{
			std::vector<std::string> lines;
			std::istringstream stream(text);
			for (std::string line; std::getline(stream, line);)
				lines.push_back(line);
			return lines;
		}
	} // namespace

	TEST(DefectEnergeticsModelTests, TheThesisGeVFromTheProposalsAndTheSavedFnv)
	{
		const EnergeticsFile file = Diamond();
		const EnergeticsModel model = BuildEnergeticsModel(file, Fnv(), kEdges);
		EXPECT_EQ(model.bulk, std::optional<std::size_t>(3));
		EXPECT_DOUBLE_EQ(model.reference.bulkEnergy, -5400.79969151);
		EXPECT_DOUBLE_EQ(model.reference.vbm, 9.3518);
		EXPECT_NEAR(model.reference.gap, 5.347455, 1e-9);
		EXPECT_TRUE(model.chemicalPotentialError.empty()) << model.chemicalPotentialError;
		EXPECT_NEAR(model.chemicalPotentials.references.at("C"), -5400.79969151 / 512.0, 1e-12);
		EXPECT_NEAR(model.chemicalPotentials.references.at("Ge"), -42.88579995 / 8.0, 1e-12);
		ASSERT_EQ(model.muSets.size(), 1u); // elemental limits only: one rich corner
		EXPECT_TRUE(model.muSets[0].vertex);
		EXPECT_EQ(model.muSets[0].name, "Ge");

		ASSERT_EQ(model.rows.size(), 2u);
		const EnergeticsRow &neutral = model.rows[0];
		const EnergeticsRow &minus = model.rows[1];
		EXPECT_EQ(neutral.charge, 0);
		EXPECT_EQ(minus.charge, -1);
		EXPECT_EQ(minus.candidates, (std::vector<std::size_t>{1, 2}));
		EXPECT_EQ(minus.energyRun, std::optional<std::size_t>(2));
		EXPECT_FALSE(minus.userChoice);
		EXPECT_FALSE(minus.proposalReason.empty());
		EXPECT_EQ(minus.locpot, Path::FromUtf8("O:/GeV/Stan_1-/HSE/PrecFock_Normal/LOCPOT"));
		EXPECT_EQ(minus.fnvEntry, std::optional<std::size_t>(0));
		ASSERT_TRUE(minus.correction.has_value());
		EXPECT_NEAR(minus.correction->correction, 0.250185, 1e-9);
		EXPECT_NEAR(minus.alignments[1].alignment, 0.0025, 1e-12);
		EXPECT_TRUE(minus.problems.empty());

		const SpeciesEnergies mu = AbsoluteChemicalPotentials(model.chemicalPotentials, model.muSets[0].deltaMu);
		const auto minusState = EnergeticsRowState(file, minus);
		ASSERT_TRUE(minusState.has_value());
		EXPECT_NEAR(*FormationEnergy(*minusState, model.reference, mu, 0.0), 10.056312, 2e-6);
		const auto neutralState = EnergeticsRowState(file, neutral);
		ASSERT_TRUE(neutralState.has_value());
		EXPECT_NEAR(*FormationEnergy(*neutralState, model.reference, mu, 0.0), 8.060222, 2e-6);
	}

	TEST(DefectEnergeticsModelTests, TheUsersChoicesWinAndTheirProblemsAreNamed)
	{
		EnergeticsFile file = Diamond();
		EnergeticsChoice fast;
		fast.defect = "GeV";
		fast.charge = -1;
		fast.energyRun = Path::FromUtf8("O:/GeV/Stan_1-/HSE/PrecFock_Fast");
		fast.included = false;
		file.choices = {fast};
		file.gap = 5.47;
		file.muSets = {{"Ge za bogate", {{"Ge", 0.5}}}};
		const EnergeticsModel model = BuildEnergeticsModel(file, Fnv(), kEdges);
		EXPECT_DOUBLE_EQ(model.reference.gap, 5.47);
		const EnergeticsRow &minus = model.rows[1];
		EXPECT_EQ(minus.energyRun, std::optional<std::size_t>(1));
		EXPECT_TRUE(minus.userChoice);
		EXPECT_FALSE(minus.included);
		EXPECT_EQ(minus.locpot, Path::FromUtf8("O:/GeV/Stan_1-/HSE/PrecFock_Fast/LOCPOT"));
		EXPECT_FALSE(minus.fnvEntry.has_value());
		EXPECT_FALSE(minus.correction.has_value());
		EXPECT_FALSE(minus.problems.empty()); // no E_corr, PRECFOCK differs from the bulk
		EXPECT_FALSE(EnergeticsRowState(file, minus).has_value());

		ASSERT_EQ(model.muSets.size(), 2u);
		EXPECT_FALSE(model.muSets[1].vertex);
		EXPECT_EQ(model.muSets[1].name, "Ge za bogate");
		EXPECT_EQ(model.muSets[1].violated, (std::vector<std::string>{"Ge"}));

		// The user's LOCPOT goes to FNV even when the energy comes from another run.
		EnergeticsFile split = Diamond();
		EnergeticsChoice locpot;
		locpot.defect = "GeV";
		locpot.charge = -1;
		locpot.energyRun = Path::FromUtf8("O:/GeV/Stan_1-/HSE/PrecFock_Fast");
		locpot.locpot = Path::FromUtf8("O:/GeV/Stan_1-/HSE/PrecFock_Normal/LOCPOT");
		split.choices = {locpot};
		const EnergeticsModel splitModel = BuildEnergeticsModel(split, Fnv(), kEdges);
		EXPECT_EQ(splitModel.rows[1].fnvEntry, std::optional<std::size_t>(0));

		// A missing mu reference is reported, not guessed.
		EnergeticsFile noGe = Diamond();
		noGe.runs.pop_back();
		EXPECT_FALSE(BuildEnergeticsModel(noGe, Fnv(), kEdges).chemicalPotentialError.empty());
		// ...unless every defect with Ge is left out.
		for (const auto &row : BuildEnergeticsModel(noGe, Fnv(), kEdges).rows)
			noGe.choices.push_back({.defect = row.defect, .charge = row.charge, .included = false});
		EXPECT_TRUE(BuildEnergeticsModel(noGe, Fnv(), kEdges).chemicalPotentialError.empty());
	}

	TEST(DefectEnergeticsModelTests, TheTsvExportHasEveryFileAndColumn)
	{
		EnergeticsFile file = Diamond();
		file.muSets = {{"N-poor (ręcznie)", {}}};
		const FnvCorrectionsFile fnv = Fnv();
		const EnergeticsModel model = BuildEnergeticsModel(file, fnv, kEdges);
		const auto files = ExportEnergeticsTsv(model, file, fnv, 0.5);
		std::vector<std::string> names;
		for (const auto &[name, text] : files)
			names.push_back(name);
		EXPECT_EQ(names, (std::vector<std::string>{"states.tsv", "transitions.tsv", "mu_sets.tsv",
			"formation_1_Ge.tsv", "formation_2_N-poor__r_cznie_.tsv"}));

		const auto text = [&](const std::string &name) {
			return std::find_if(files.begin(), files.end(), [&](const auto &entry) { return entry.first == name; })->second;
		};
		const auto states = Lines(text("states.tsv"));
		ASSERT_EQ(states.size(), 1u + 2u * 2u); // header + 2 rows x 2 mu sets
		for (const char *column : {"mu_set", "E_corr (eV)", "C_a1 (eV)", "sigma_a3 (eV)", "window_width_a2 (A)",
				 "centre_f1", "E_f(E_F=0) (eV)", "problems"})
			EXPECT_NE(states[0].find(column), std::string::npos) << column;
		EXPECT_NE(states[2].find("0.250185"), std::string::npos); // GeV q-1 under the first set
		EXPECT_NE(states[2].find("10.05631"), std::string::npos);

		const auto transitions = Lines(text("transitions.tsv"));
		ASSERT_EQ(transitions.size(), 2u);
		EXPECT_EQ(transitions[1].substr(0, 10), "GeV\t0\t-1\t1");
		EXPECT_NE(transitions[1].find("1.99609"), std::string::npos);

		const auto formation = Lines(text("formation_1_Ge.tsv"));
		EXPECT_EQ(formation[0], "E_F (eV)\tGeV q=0 (eV)\tGeV q=-1 (eV)\tGeV envelope (eV)");
		// 0, 0.5, ..., 5.0 and the gap itself.
		ASSERT_EQ(formation.size(), 1u + 11u + 1u);
		EXPECT_EQ(formation[1].substr(0, 2), "0\t");
		EXPECT_EQ(formation.back().substr(0, 9), "5.347455\t");

		const auto mu = Lines(text("mu_sets.tsv"));
		EXPECT_EQ(mu[0], "mu_set\tvertex\tspecies\tmu_ref (eV)\tdelta_mu (eV)\tmu (eV)\tviolated");
		EXPECT_EQ(mu.size(), 1u + 2u * 2u); // two sets x (C, Ge)
	}

	TEST(DefectEnergeticsModelTests, ExtraRootsUseTheDeepestRootAndMergeMatchingGroupsOnce)
	{
		EnergeticsFile file = Diamond();
		file.runs.push_back(Tests::Run("X:/other/wrapper/GeSiV/q0", {"C", "Ge"}, {510, 1}, {4, 14}, 2054, -5377));
		file.runs.push_back(Tests::Run("X:/other/GeV/q0", {"C", "Ge"}, {510, 1}, {4, 14}, 2054, -5376));
		file.runs.push_back(Tests::Run("X:/other/Vacancy/q0", {"C"}, {511}, {4}, 2044, -5380));
		file.runs.push_back(Tests::Run("X:/outside/Excluded/q0", {"C"}, {511}, {4}, 2044, -5380));
		file.extraRoots = {Path::FromUtf8("X:/other"), Path::FromUtf8("X:/other/wrapper/GeSiV/"),
			Path::FromUtf8("X:/other"), Path::FromUtf8("O:/GeV")};
		const auto check = [&](const EnergeticsModel &model) {
			ASSERT_EQ(model.rows.size(), 4u);
			EXPECT_EQ(model.rows[0].defect, "GeSiV");
			EXPECT_EQ(model.rows[0].candidates, (std::vector<std::size_t>{5}));
			EXPECT_EQ(model.rows[1].defect, "GeV");
			EXPECT_EQ(model.rows[1].charge, 0);
			EXPECT_EQ(model.rows[1].candidates, (std::vector<std::size_t>{0, 6}));
			EXPECT_EQ(model.rows[2].charge, -1);
			EXPECT_EQ(model.rows[2].candidates, (std::vector<std::size_t>{1, 2}));
			EXPECT_EQ(model.rows[3].defect, "Vacancy");
			EXPECT_EQ(model.rows[3].candidates, (std::vector<std::size_t>{7}));
		};
		check(BuildEnergeticsModel(file, Fnv(), kEdges));
		std::reverse(file.extraRoots.begin(), file.extraRoots.end());
		check(BuildEnergeticsModel(file, Fnv(), kEdges));
		file.extraRoots.clear();
		EXPECT_EQ(BuildEnergeticsModel(file, Fnv(), kEdges).rows.size(), 2u);
		file.root = {};
		file.extraRoots = {Path::FromUtf8("X:/other/wrapper/GeSiV")};
		const auto extraOnly = BuildEnergeticsModel(file, Fnv(), kEdges);
		ASSERT_EQ(extraOnly.rows.size(), 1u);
		EXPECT_EQ(extraOnly.rows[0].defect, "GeSiV");
	}

	TEST(DefectEnergeticsModelTests, HiddenDefectsStayListedOutOfTheAnalysisRemovedOnesAreGone)
	{
		EnergeticsFile file = Diamond();
		file.choices.push_back({"GeV", -1, file.runs[1].directory, {}, true});
		file.hiddenDefects = {"GeV"};
		const auto hidden = BuildEnergeticsModel(file, Fnv(), kEdges);
		ASSERT_EQ(hidden.rows.size(), 2u);
		for (const auto &row : hidden.rows)
		{
			EXPECT_TRUE(row.hidden);
			EXPECT_FALSE(row.included);
		}
		for (const auto &[name, text] : ExportEnergeticsTsv(hidden, file, Fnv()))
			EXPECT_EQ(text.find("GeV"), std::string::npos) << name;
		file.hiddenDefects.clear();
		file.removedDefects = {"GeV"};
		EXPECT_TRUE(BuildEnergeticsModel(file, Fnv(), kEdges).rows.empty());
		file.removedDefects.clear();
		const auto restored = BuildEnergeticsModel(file, Fnv(), kEdges);
		ASSERT_EQ(restored.rows.size(), 2u);
		EXPECT_EQ(restored.rows[1].energyRun, std::optional<std::size_t>(1));
		EXPECT_TRUE(restored.rows[1].userChoice);
	}

	// Opt-in, real data: DS_ENERGETICS_DATA = test-directory/fnv-diamond/okeanos/data (local copies of
	// the thesis runs, <defect>/q<n>/ and bulk/), DS_ENERGETICS_MU = the folder with Ge/, Si/, N/
	// references (O:/Potencjaly_chemiczne). Every neutral state against the reproduction table
	// (analysis/formation/A_sx_thesisC/formation_table.csv), through the real reader and the scan.
	TEST(DefectEnergeticsModelTests, TheThesisRunsGiveTheReproducedNeutralFormationEnergies)
	{
		const char *data = std::getenv("DS_ENERGETICS_DATA");
		const char *muRoot = std::getenv("DS_ENERGETICS_MU");
		if (data == nullptr || muRoot == nullptr)
			GTEST_SKIP() << "DS_ENERGETICS_DATA / DS_ENERGETICS_MU not set";
		EnergeticsFile file;
		file.root = Path::FromUtf8(data);
		for (const Path &run : VaspRunRecordIO::FindRuns(file.root))
			file.runs.push_back(VaspRunRecordIO::Read(run));
		const Path mu = Path::FromUtf8(muRoot);
		for (const char *reference : {"Ge/Lattice_Constant_Convergance/first", "Si/Lattice_Constant_Convergance/first", "N/first"})
		{
			file.runs.push_back(VaspRunRecordIO::Read(mu / Path::FromUtf8(reference)));
			ASSERT_TRUE(file.runs.back().error.empty()) << reference << ": " << file.runs.back().error;
			file.references[file.runs.back().species.front()] = file.runs.back().directory;
		}
		const auto edges = VaspRunRecordIO::ReadBandEdges(file.root / Path::FromUtf8("bulk"));
		ASSERT_TRUE(edges.has_value());
		// The thesis took SiN q0 from a relaxation that was cut off (no timing footer): nothing is
		// proposed there, and the user picks it, as the thesis did. Nowhere else.
		std::vector<std::string> picked;
		for (const auto &row : BuildEnergeticsModel(file, {}, edges).rows)
			if (!row.energyRun && row.candidates.size() == 1)
			{
				file.choices.push_back({row.defect, row.charge, file.runs[row.candidates.front()].directory, {}, true});
				picked.push_back(std::format("{} {}", row.defect, row.charge));
			}
		EXPECT_EQ(picked, (std::vector<std::string>{"SiN 0"}));
		const EnergeticsModel model = BuildEnergeticsModel(file, {}, edges);
		ASSERT_TRUE(model.bulk.has_value());
		ASSERT_TRUE(model.chemicalPotentialError.empty()) << model.chemicalPotentialError;
		ASSERT_FALSE(model.muSets.empty());
		EXPECT_NEAR(model.reference.vbm, 9.3518, 1e-9);
		EXPECT_NEAR(model.reference.gap, 5.347455, 1e-6);
		const SpeciesEnergies potentials = AbsoluteChemicalPotentials(model.chemicalPotentials, model.muSets[0].deltaMu);
		const std::map<std::string, double> expected{{"Ge_C", 7.128443}, {"GeN", 10.112146}, {"GeN2V", 6.974915},
			{"GeNV", 7.519654}, {"GeV", 8.060222}, {"N_C", 4.004517}, {"Si_C", 4.479806}, {"SiN", 7.996314},
			{"SiN2V", 4.649975}, {"SiNV", 5.476539}};
		std::size_t checked = 0, charged = 0;
		for (const auto &row : model.rows)
		{
			if (row.charge != 0)
			{
				++charged;
				continue;
			}
			const auto state = EnergeticsRowState(file, row);
			ASSERT_TRUE(state.has_value()) << row.defect;
			EXPECT_NEAR(*FormationEnergy(*state, model.reference, potentials, 0.0), expected.at(row.defect), 2e-6) << row.defect;
			++checked;
		}
		EXPECT_EQ(checked, expected.size());
		EXPECT_EQ(charged, 21u);
	}
} // namespace DefectStudio::Tests
