#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "IO/VaspRunRecordIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Lines as VASP 5.4.4 writes them (the thesis GeV q-1 HSE run), trimmed.
		const std::string kHead =
			" vasp.5.4.4.18Apr17-6-g9f103f2a35 (build Jan 28 2019 12:48:14) gamma-only\n"
			" POTCAR:    PAW_PBE C 08Apr2002\n"
			" POTCAR:    PAW_PBE Ge_d 03Jul2007\n"
			" POTCAR:    PAW_PBE C 08Apr2002\n"
			"   VRHFIN =C: s2p2\n"
			"   TITEL  = PAW_PBE C 08Apr2002\n"
			"   POMASS =   12.011; ZVAL   =    4.000    mass and valenz\n"
			"   VRHFIN =Ge: 3d4s4p\n"
			"   TITEL  = PAW_PBE Ge_d 03Jul2007\n"
			"   POMASS =   72.610; ZVAL   =   14.000    mass and valenz\n"
			"   number of dos      NEDOS =    301   number of ions     NIONS =    511\n"
			"   ions per type =             510   1\n"
			"   PREC   = normal    normal or accurate (medium, high low for compatibility)\n"
			"   ISPIN  =      2    spin polarized calculation?\n"
			"   ENCUT  =  600.0 eV  44.10 Ry    6.64 a.u.  28.33 28.33 28.33*2*pi/ulx,y,z\n"
			"   NELM   =     60;   NELMIN=  2; NELMDL=  0     # of ELM steps \n"
			"   EDIFF  = 0.1E-03   stopping-criterion for ELM\n"
			"   EDIFFG = -.1E-01   stopping-criterion for IOM\n"
			"   NSW    =    200    number of steps for IOM\n"
			"   IBRION =      1    ionic relax: 0-MD 1-quasi-New 2-CG\n"
			"   ZVAL   =   4.00 14.00\n"
			"   NELECT =    2055.0000    total number of electrons\n"
			"   LHFCALC =     T    Hartree Fock is set to\n"
			"   PRECFOCK=normal    Normal, Fast or Accurate (Low or Medium for compatibility)\n"
			"   AEXX    =    0.2500 exact exchange contribution\n"
			"   HFSCREEN=    0.2000 screening length (either q_TF or 0.3 A-1)\n"
			"   HFSCREENC=   0.3000 screening length for correlation (either q_TF or 0.3 A-1)\n";

		const std::string kTail =
			"  free  energy   TOTEN  =     -5365.90561123 eV\n"
			"  free  energy   TOTEN  =     -5365.90561526 eV\n"
			"  energy  without entropy=    -5365.90561526  energy(sigma->0) =    -5365.90561526\n"
			" reached required accuracy - stopping structural energy minimisation\n"
			" General timing and accounting informations for this job:\n"
			"                         Elapsed time (sec):     7436.895\n";
	} // namespace

	TEST(VaspRunRecordIOTests, TheHeadAndTheTailGiveEverythingTheEnergeticsNeed)
	{
		VaspRunRecord record;
		VaspRunRecordIO::ParseOutcar(kHead, kTail, record);
		EXPECT_TRUE(record.error.empty()) << record.error;
		EXPECT_EQ(record.species, (std::vector<std::string>{"C", "Ge"}));
		EXPECT_EQ(record.ions, (std::vector<int>{510, 1}));
		EXPECT_EQ(record.valence, (std::vector<double>{4.0, 14.0}));
		ASSERT_TRUE(record.electrons.has_value());
		EXPECT_DOUBLE_EQ(*record.electrons, 2055.0);
		ASSERT_TRUE(record.totalEnergy.has_value());
		EXPECT_DOUBLE_EQ(*record.totalEnergy, -5365.90561526); // the last TOTEN, not the first
		EXPECT_TRUE(record.finished);
		EXPECT_TRUE(record.relaxation);
		EXPECT_TRUE(record.ionicConverged);
		EXPECT_TRUE(record.method.hybrid);
		EXPECT_DOUBLE_EQ(record.method.hfscreen, 0.2); // not HFSCREENC
		EXPECT_DOUBLE_EQ(record.method.aexx, 0.25);
		EXPECT_EQ(record.method.precFock, "normal");
		EXPECT_DOUBLE_EQ(record.method.encut, 600.0);
		EXPECT_EQ(record.method.prec, "normal");
		EXPECT_EQ(record.method.ispin, 2);
		EXPECT_EQ(record.method.potcars, (std::vector<std::string>{"PAW_PBE C 08Apr2002", "PAW_PBE Ge_d 03Jul2007"}));
		EXPECT_EQ(CalculationCharge(record), std::optional<int>(-1));
	}

	TEST(VaspRunRecordIOTests, AnUnfinishedPbeRunAndAFileThatIsNoOutcar)
	{
		std::string head = kHead;
		head.replace(head.find("LHFCALC =     T"), 15, "LHFCALC =     F");
		head.replace(head.find("IBRION =      1"), 15, "IBRION =     -1");
		VaspRunRecord pbe;
		// Cut off inside the next SCF: its per-iteration "free energy    TOTEN" is not an energy.
		VaspRunRecordIO::ParseOutcar(head, "  free  energy   TOTEN  =     -5000.5 eV\n"
			"  free energy    TOTEN  =     -5000.9 eV\n", pbe);
		EXPECT_TRUE(pbe.error.empty());
		EXPECT_FALSE(pbe.method.hybrid);
		EXPECT_TRUE(pbe.method.precFock.empty()); // PRECFOCK means nothing without HF
		EXPECT_FALSE(pbe.relaxation);             // IBRION = -1: static
		EXPECT_FALSE(pbe.finished);
		EXPECT_FALSE(pbe.ionicConverged);
		EXPECT_DOUBLE_EQ(*pbe.totalEnergy, -5000.5);

		VaspRunRecord junk;
		VaspRunRecordIO::ParseOutcar("hello\n", "world\n", junk);
		EXPECT_FALSE(junk.error.empty());
	}

	TEST(VaspRunRecordIOTests, PoscarVolumeFromTheLatticeOrANegativeScale)
	{
		// 512-atom diamond: 4x4x4 conventional cells of a = 3.567 A.
		const auto volume = VaspRunRecordIO::ParsePoscarVolume(
			"diamond\n 3.567\n 4 0 0\n 0 4 0\n 0 0 4\n C\n 512\nDirect\n");
		ASSERT_TRUE(volume.has_value());
		EXPECT_NEAR(*volume, 64.0 * 3.567 * 3.567 * 3.567, 1e-9);
		EXPECT_NEAR(512.0 / *volume * 1e24, 1.76e23, 0.01e23); // atoms per cm^3
		EXPECT_DOUBLE_EQ(*VaspRunRecordIO::ParsePoscarVolume("x\n-100\n1 0 0\n0 1 0\n0 0 1\n"), 100.0);
		EXPECT_FALSE(VaspRunRecordIO::ParsePoscarVolume("x\n1.0\n1 0 0\n").has_value());
	}

	TEST(VaspRunRecordIOTests, RunsAreFoundAtAnyDepthAndReadFromHeadAndTail)
	{
		const Path root = Path::FromResolved(FileSystem::TempDirectoryPath()) / "ds-vasp-runs-test";
		FileSystem::RemoveAll(root.Native());
		const Path relax = root / "GeV" / "Stan_1-";
		const Path hse = relax / "HSE" / "PrecFock_Normal";
		ASSERT_TRUE(FileSystem::CreateDirectories(hse.Native()));
		ASSERT_TRUE(FileSystem::CreateDirectories((root / "GeV" / "backup").Native()));
		std::ofstream((root / "GeV" / "backup" / "INCAR").Native()) << "ENCUT = 600\n";
		std::ofstream((relax / "OUTCAR").Native()) << kHead << kTail;
		{
			// Bigger than head + tail, so the middle is never read.
			std::ofstream outcar((hse / "OUTCAR").Native(), std::ios::binary);
			outcar << kHead << std::string(VaspRunRecordIO::kHeadBytes + VaspRunRecordIO::kTailBytes, ' ') << kTail;
		}
		std::ofstream((hse / "LOCPOT").Native()) << "x";

		const std::vector<Path> runs = VaspRunRecordIO::FindRuns(root);
		ASSERT_EQ(runs.size(), 2u);
		EXPECT_EQ(runs[0], relax);
		EXPECT_EQ(runs[1], hse);
		EXPECT_TRUE(VaspRunRecordIO::FindRuns(root, [] { return true; }).size() < 2u);

		const VaspRunRecord record = VaspRunRecordIO::Read(hse);
		EXPECT_TRUE(record.error.empty()) << record.error;
		EXPECT_EQ(record.directory, hse);
		EXPECT_TRUE(record.hasLocpot);
		EXPECT_FALSE(record.hasContcar);
		EXPECT_GT(record.outcarSize, VaspRunRecordIO::kHeadBytes + VaspRunRecordIO::kTailBytes);
		EXPECT_NE(record.outcarWriteTime, 0);
		EXPECT_DOUBLE_EQ(*record.totalEnergy, -5365.90561526);
		EXPECT_TRUE(record.finished);

		EXPECT_TRUE(VaspRunRecordIO::IsUpToDate(record));
		std::ofstream((hse / "OUTCAR").Native(), std::ios::app) << "more\n";
		EXPECT_FALSE(VaspRunRecordIO::IsUpToDate(record));

		EXPECT_FALSE(VaspRunRecordIO::Read(root / "GeV" / "backup").error.empty());
		FileSystem::RemoveAll(root.Native());
	}
} // namespace DefectStudio::Tests
