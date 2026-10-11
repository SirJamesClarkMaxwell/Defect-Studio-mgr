#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "IO/EnergeticsIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		const Path kProject = Path::FromUtf8("D:/projekty/diament");

		VaspRunRecord GeVMinus()
		{
			VaspRunRecord run;
			run.directory = Path::FromUtf8("O:/GeV/Stan_1-/HSE/PrecFock_Normal");
			run.species = {"C", "Ge"};
			run.ions = {510, 1};
			run.valence = {4.0, 14.0};
			run.electrons = 2055.0;
			run.totalEnergy = -5365.90561526123;
			run.finished = true;
			run.relaxation = true;
			run.ionicConverged = true;
			run.method.hybrid = true;
			run.method.hfscreen = 0.2;
			run.method.aexx = 0.25;
			run.method.precFock = "normal";
			run.method.encut = 600.0;
			run.method.prec = "normal";
			run.method.ispin = 2;
			run.method.potcars = {"PAW_PBE C 08Apr2002", "PAW_PBE Ge_d 03Jul2007"};
			run.hasLocpot = true;
			run.outcarSize = 331284;
			run.outcarWriteTime = 1738972726;
			return run;
		}

		EnergeticsFile Sample()
		{
			EnergeticsFile file;
			file.root = Path::FromUtf8("O:/");
			VaspRunRecord unread;
			unread.directory = kProject / Path::FromUtf8("data/GeV/q0");
			unread.error = "brak OUTCAR";
			file.runs = {GeVMinus(), unread};
			file.bulkRun = Path::FromUtf8("O:/Idealna-Super-Komórka");
			EnergeticsChoice minus;
			minus.defect = "GeV";
			minus.charge = -1;
			minus.energyRun = Path::FromUtf8("O:/GeV/Stan_1-/HSE/PrecFock_Normal");
			EnergeticsChoice genMinus;
			genMinus.defect = "GeN";
			genMinus.charge = -1;
			genMinus.locpot = kProject / Path::FromUtf8("data/GeN/q-1/LOCPOT_1-");
			genMinus.included = false;
			file.choices = {minus, genMinus};
			file.references = {{"Ge", Path::FromUtf8("O:/Potencjaly_chemiczne/Ge/Lattice_Constant_Convergance/first")},
				{"C", Path::FromUtf8("O:/Idealna-Super-Komórka")}};
			file.competing = {{"SiC", {{"Si", 1}, {"C", 1}}, Path::FromUtf8("E:/fazy/SiC")}};
			file.muSets = {{"N-poor", {{"N", -1.25}}}};
			file.dielectric = 5.7;
			file.dielectricSource = "promotor (diament)";
			file.gap = 5.47;
			file.gapSource = "eksperyment";
			return file;
		}
	} // namespace

	TEST(EnergeticsIOTests, EverythingSurvivesARoundTripWithProjectRelativePaths)
	{
		const EnergeticsFile file = Sample();
		const std::string text = EnergeticsIO::Serialize(file, kProject);
		EXPECT_NE(text.find("data/GeN/q-1/LOCPOT_1-"), std::string::npos); // relative inside the project
		EXPECT_EQ(text.find("D:/projekty/diament/data"), std::string::npos);

		EnergeticsFile parsed;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(EnergeticsIO::Parse(text, kProject, parsed, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		EXPECT_EQ(parsed.root, file.root);
		ASSERT_EQ(parsed.runs.size(), 2u);
		const VaspRunRecord &run = parsed.runs[0];
		const VaspRunRecord expected = GeVMinus();
		EXPECT_EQ(run.directory, expected.directory);
		EXPECT_EQ(run.species, expected.species);
		EXPECT_EQ(run.ions, expected.ions);
		EXPECT_EQ(run.valence, expected.valence);
		EXPECT_EQ(run.electrons, expected.electrons);
		EXPECT_EQ(run.totalEnergy, expected.totalEnergy); // full double precision
		EXPECT_TRUE(run.finished && run.relaxation && run.ionicConverged && run.hasLocpot);
		EXPECT_FALSE(run.hasContcar);
		EXPECT_EQ(run.method.precFock, "normal");
		EXPECT_EQ(run.method.potcars, expected.method.potcars);
		EXPECT_DOUBLE_EQ(run.method.hfscreen, 0.2);
		EXPECT_EQ(run.method.ispin, 2);
		EXPECT_EQ(run.outcarSize, expected.outcarSize);
		EXPECT_EQ(run.outcarWriteTime, expected.outcarWriteTime);
		EXPECT_EQ(parsed.runs[1].directory, kProject / Path::FromUtf8("data/GeV/q0"));
		EXPECT_EQ(parsed.runs[1].error, "brak OUTCAR");

		EXPECT_EQ(parsed.bulkRun, file.bulkRun);
		ASSERT_EQ(parsed.choices.size(), 2u);
		EXPECT_EQ(parsed.choices[0].defect, "GeV");
		EXPECT_EQ(parsed.choices[0].charge, -1);
		EXPECT_EQ(parsed.choices[0].energyRun, file.choices[0].energyRun);
		EXPECT_FALSE(parsed.choices[0].locpot.has_value());
		EXPECT_TRUE(parsed.choices[0].included);
		EXPECT_FALSE(parsed.choices[1].energyRun.has_value());
		EXPECT_EQ(parsed.choices[1].locpot, file.choices[1].locpot);
		EXPECT_FALSE(parsed.choices[1].included);
		EXPECT_EQ(parsed.references, file.references);
		ASSERT_EQ(parsed.competing.size(), 1u);
		EXPECT_EQ(parsed.competing[0].name, "SiC");
		EXPECT_EQ(parsed.competing[0].formula, (SpeciesCounts{{"Si", 1}, {"C", 1}}));
		EXPECT_EQ(parsed.competing[0].run, file.competing[0].run);
		ASSERT_EQ(parsed.muSets.size(), 1u);
		EXPECT_EQ(parsed.muSets[0].name, "N-poor");
		EXPECT_EQ(parsed.muSets[0].deltaMu, (SpeciesEnergies{{"N", -1.25}}));
		EXPECT_DOUBLE_EQ(parsed.dielectric, 5.7);
		EXPECT_EQ(parsed.dielectricSource, "promotor (diament)");
		EXPECT_EQ(parsed.gap, std::optional<double>(5.47));
		EXPECT_EQ(parsed.gapSource, "eksperyment");
	}

	TEST(EnergeticsIOTests, ABadEntryIsDroppedWithAWarningAndANewerFileIsRefused)
	{
		const std::string text =
			"formatVersion: 1\n"
			"choices:\n"
			"  - {defect: GeV, charge: -1}\n"
			"  - {defect: GeN, charge: minus one}\n"
			"muSets:\n"
			"  - {name: rich, deltaMu: {N: 0}}\n"
			"  - {name: broken, deltaMu: [1, 2]}\n";
		EnergeticsFile parsed;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(EnergeticsIO::Parse(text, kProject, parsed, warnings, error)) << error;
		EXPECT_EQ(parsed.choices.size(), 1u);
		EXPECT_EQ(parsed.muSets.size(), 1u);
		EXPECT_EQ(warnings.size(), 2u);

		EnergeticsFile empty;
		EXPECT_TRUE(EnergeticsIO::Parse("", kProject, empty, warnings, error));
		EXPECT_FALSE(EnergeticsIO::Parse("formatVersion: 2\n", kProject, empty, warnings, error));
		EXPECT_FALSE(error.empty());
		EXPECT_FALSE(EnergeticsIO::Parse("- not a map\n", kProject, empty, warnings, error));
		EXPECT_EQ(EnergeticsIO::FilePath(kProject), kProject / Path::FromUtf8("energetics.yaml"));
	}
} // namespace DefectStudio::Tests
