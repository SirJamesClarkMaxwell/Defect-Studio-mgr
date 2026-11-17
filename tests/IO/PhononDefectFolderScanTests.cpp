#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononDefectFolderScan.hpp"

namespace DefectStudio::Tests
{
	class PhononDefectFolderScanTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_defect_scan_" + ToString(GenerateUuid())));
		void SetUp() override { std::filesystem::create_directories(root.Native()); }
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		void File(const Path &file)
		{
			std::filesystem::create_directories(file.parent_path().Native());
			std::ofstream(file.Native()) << "x\n";
		}
		void Run(const Path &directory) { File(directory / "OUTCAR"); File(directory / "CONTCAR"); }
	};

	// The user's server layout: P:/PL-spectra/dimer/CH0/R2SCAN/9x9x1
	TEST_F(PhononDefectFolderScanTests, ServerDimerLayout)
	{
		const Path system = root / "dimer/CH0/R2SCAN/9x9x1";
		Run(system / "relax");
		Run(system / "excited/try_1_singlet");
		Run(system / "excited/try_1_tryplet");
		Run(system / "dispersion");
		File(system / "dispersion/dephonopy_disp.yaml");
		Run(system / "dispersion/disp-001");                   // not descended into
		Run(system / "relax/run_computations/old");            // not descended into
		File(system / "spectral-function/12x12x1/displacements/hr_factors.dat");
		File(system / "spectral-function/9x9x1/displacements/hr_factors.dat");

		const auto proposals = ScanPhononDefectFolder(root, {});
		ASSERT_EQ(proposals.size(), 1u);
		const auto &p = proposals[0];
		EXPECT_EQ(p.systemDirectory, system);
		EXPECT_EQ(p.name, "dimer/CH0/R2SCAN/9x9x1");
		EXPECT_EQ(p.groundStateDirectory, system / "relax");
		EXPECT_EQ(p.excitedStateDirectory, system / "excited/try_1_singlet");
		EXPECT_EQ(p.tripletDirectory, system / "excited/try_1_tryplet");
		EXPECT_EQ(p.forceConstantDirectory, system / "dispersion");
		EXPECT_TRUE(p.groundAtExcitedDirectory.Empty());
		EXPECT_EQ(p.serverResults.size(), 2u);
		EXPECT_EQ(p.candidates.size(), 4u); // relax, singlet, tryplet, dispersion (FC; its OUTCAR matches no run role)
	}

	// hBN/V2CBCN/dimer: ground_state/{singlet_HSE, excited_geometry_HSE}, exc_ms/{singlet_HSE, triplet_HSE}
	TEST_F(PhononDefectFolderScanTests, V2cbcnLayoutWithSinglePointAtExcitedGeometry)
	{
		const Path system = root / "V2CBCN/dimer";
		Run(system / "ground_state/singlet_HSE");
		Run(system / "ground_state/excited_geometry_HSE");
		Run(system / "exc_ms/singlet_HSE");
		Run(system / "exc_ms/triplet_HSE");
		Run(system / "CH1/HSE"); // charge state run: matches no role
		const auto proposals = ScanPhononDefectFolder(root / "V2CBCN", {});
		ASSERT_EQ(proposals.size(), 1u);
		const auto &p = proposals[0];
		EXPECT_EQ(p.name, "dimer");
		EXPECT_EQ(p.groundStateDirectory, system / "ground_state/singlet_HSE");
		EXPECT_EQ(p.excitedStateDirectory, system / "exc_ms/singlet_HSE");
		EXPECT_EQ(p.tripletDirectory, system / "exc_ms/triplet_HSE");
		EXPECT_EQ(p.groundAtExcitedDirectory, system / "ground_state/excited_geometry_HSE");
		EXPECT_TRUE(p.forceConstantDirectory.Empty());
	}

	TEST_F(PhononDefectFolderScanTests, SeveralSystemsGiveSeveralProposalsAndAppDatasetsJoin)
	{
		Run(root / "a/relax");
		Run(root / "a/excited/singlet");
		Run(root / "b/gs");
		Run(root / "b/es");
		File(root / "b/defectstudio/phonons/summary.json");
		std::ofstream((root / "b/defectstudio/phonons/summary.json").Native()) << R"({"schema":"ds.phonon_vibronic.v1","label":"B"})";
		const auto proposals = ScanPhononDefectFolder(root, {});
		ASSERT_EQ(proposals.size(), 2u);
		EXPECT_EQ(proposals[0].systemDirectory, root / "a");
		EXPECT_EQ(proposals[1].systemDirectory, root / "b");
		EXPECT_EQ(proposals[1].name, "b");
		ASSERT_EQ(proposals[1].datasets.size(), 1u);
		EXPECT_EQ(proposals[1].datasets[0].label, "B");
		EXPECT_TRUE(proposals[0].datasets.empty());
	}

	TEST_F(PhononDefectFolderScanTests, NoPairNoProposalAndCancelStops)
	{
		Run(root / "only/relax");
		EXPECT_TRUE(ScanPhononDefectFolder(root, {}).empty());
		Run(root / "only/excited");
		EXPECT_EQ(ScanPhononDefectFolder(root / "only", {}).at(0).name, "only");
		EXPECT_TRUE(ScanPhononDefectFolder(root, [] { return true; }).empty());
	}
} // namespace DefectStudio::Tests
