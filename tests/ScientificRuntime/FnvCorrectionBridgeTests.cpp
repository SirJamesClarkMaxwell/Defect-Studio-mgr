#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#include "ScientificRuntime/Python/FnvCorrectionBridge.hpp"
#include "ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	TEST(FnvCorrectionBridgeTests, NothingToCorrectForANeutralDefect)
	{
		FnvProfileRequest request;
		request.defectLocpot = ScientificRuntimeFixturePath("fnv/NO_SUCH_LOCPOT");
		request.bulkLocpot = ScientificRuntimeFixturePath("fnv/NO_SUCH_LOCPOT");
		request.charge = 0;
		request.dielectric = 5.7;
		const Result<FnvProfiles> result = FnvCorrectionBridge{}.ComputeProfiles(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.fnv.request_invalid");
	}

	TEST(FnvCorrectionBridgeTests, ANonPositiveDielectricIsRejected)
	{
		FnvProfileRequest request;
		request.defectLocpot = ScientificRuntimeFixturePath("fnv/NO_SUCH_LOCPOT");
		request.bulkLocpot = ScientificRuntimeFixturePath("fnv/NO_SUCH_LOCPOT");
		request.charge = -1;
		request.dielectric = 0.0;
		const Result<FnvProfiles> result = FnvCorrectionBridge{}.ComputeProfiles(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.fnv.request_invalid");
	}

	TEST(FnvCorrectionBridgeTests, AMissingLocpotIsAnError)
	{
		FnvProfileRequest request;
		request.defectLocpot = ScientificRuntimeFixturePath("fnv/NO_SUCH_LOCPOT");
		request.bulkLocpot = ScientificRuntimeFixturePath("fnv/NO_SUCH_LOCPOT");
		request.charge = -1;
		request.dielectric = 5.7;
		EXPECT_FALSE(FnvCorrectionBridge{}.ComputeProfiles(request));
	}

	// The acceptance test against the BSc thesis, off by default (two 180^3 LOCPOTs, ~1 min):
	//   set DS_FNV_GOLDEN_DIR=<folder with LOCPOT_1- and LOCPOT-bulk of GeN q = -1>
	// (test-directory/fnv-diamond/GeN/q-1 on the author's machine). sxdefectalign there gives
	// E_lat 0.252685 eV and, with the automatic plateau, E_corr 0.229874 eV.
	TEST(FnvCorrectionBridgeTests, GeNMatchesSxdefectalignWithinAMilliElectronVolt)
	{
		const char *golden = std::getenv("DS_FNV_GOLDEN_DIR");
		if (golden == nullptr || *golden == '\0')
			GTEST_SKIP() << "DS_FNV_GOLDEN_DIR not set";

		FnvProfileRequest request;
		request.defectLocpot = Path::FromUtf8(golden) / "LOCPOT_1-";
		request.bulkLocpot = Path::FromUtf8(golden) / "LOCPOT-bulk";
		request.charge = -1;
		request.dielectric = 5.7;
		request.centreFractional = glm::dvec3(0.46124, 0.559967, 0.461206);
		request.energyCutoff = 44.11 * 13.605693122994; // the thesis --ecut 44.11 Ry
		const Result<FnvProfiles> result = FnvCorrectionBridge{}.ComputeProfiles(request);
		ASSERT_TRUE(result) << result.Error().technicalDetails;

		const FnvProfiles &profiles = *result;
		EXPECT_EQ(profiles.charge, -1);
		EXPECT_NEAR(profiles.latticeEnergy, 0.252685, 1e-3);
		EXPECT_NEAR(profiles.modelPotentialMean, -0.00078, 2e-4);
		for (int a = 0; a < 3; ++a)
		{
			SCOPED_TRACE(a);
			const FnvAxisProfile &axis = profiles.axes[a];
			ASSERT_EQ(axis.position.size(), 180u);
			ASSERT_EQ(axis.deltaV.size(), 180u);
			ASSERT_EQ(axis.modelPotential.size(), 180u);
			EXPECT_NEAR(axis.length, 14.183163, 1e-4);
			EXPECT_NEAR(axis.position[0], 0.0, 1e-9);
			EXPECT_NEAR(axis.position[1], axis.length / 180.0, 1e-6);
		}

		std::array<FnvAxisAlignment, 3> alignments;
		for (int a = 0; a < 3; ++a)
			alignments[a] = ProposeFnvAlignment(
				profiles.axes[a], FnvModelPotential(profiles, a, FnvModelConvention::Sxdefectalign));
		// The defect along x peaks where sxdefectalign put it (6.540 A).
		EXPECT_NEAR(alignments[0].defectPosition, 6.540, 0.1);
		EXPECT_NEAR(CombineFnvCorrection(profiles, alignments).correction, 0.229874, 1e-3);

		for (int a = 0; a < 3; ++a)
			alignments[a] = ProposeFnvAlignment(
				profiles.axes[a], FnvModelPotential(profiles, a, FnvModelConvention::Pymatgen));
		EXPECT_NEAR(CombineFnvCorrection(profiles, alignments).correction, 0.23324, 1e-3);
	}

	// Path B of the thesis formation-energy reproduction (skill defect-formation-energy), off by
	// default: the FNV panel's default calculation (cutoff from INCAR, sxdefectalign convention,
	// automatic plateau, all axes) for every case, headless. ~1 min per case.
	//   set DS_FNV_BATCH_CASES=<TSV: tag, q, fx,fy,fz, defect LOCPOT, bulk LOCPOT>
	//   set DS_FNV_BATCH_OUT=<existing folder>  -> ecorr_app.csv + <tag>_profiles.csv
	TEST(FnvCorrectionBridgeTests, DiamondBatchForFormationEnergies)
	{
		const char *cases = std::getenv("DS_FNV_BATCH_CASES");
		const char *out = std::getenv("DS_FNV_BATCH_OUT");
		if (cases == nullptr || *cases == '\0' || out == nullptr || *out == '\0')
			GTEST_SKIP() << "DS_FNV_BATCH_CASES / DS_FNV_BATCH_OUT not set";

		std::ifstream input(Path::FromUtf8(cases).Native());
		ASSERT_TRUE(input) << cases;
		std::ofstream summary((Path::FromUtf8(out) / "ecorr_app.csv").Native());
		summary << "tag,q,E_lat_eV,C_eV,C_axis_spread_eV,E_corr_eV,energyCutoff_eV,modelPotentialMean_eV,"
				   "C_a0,C_a1,C_a2,sigma_a0,sigma_a1,sigma_a2,flat_a0,flat_a1,flat_a2,slope_a0,slope_a1,slope_a2\n";

		std::string line;
		while (std::getline(input, line))
		{
			if (line.empty() || line.front() == '#')
				continue;
			std::istringstream fields(line);
			std::string tag, charge, centre, defect, bulk;
			std::getline(fields, tag, '\t');
			std::getline(fields, charge, '\t');
			std::getline(fields, centre, '\t');
			std::getline(fields, defect, '\t');
			std::getline(fields, bulk, '\t');
			SCOPED_TRACE(tag);

			FnvProfileRequest request;
			request.defectLocpot = Path::FromUtf8(defect);
			request.bulkLocpot = Path::FromUtf8(bulk);
			request.charge = std::stoi(charge);
			request.dielectric = 5.7;
			std::replace(centre.begin(), centre.end(), ',', ' ');
			std::istringstream(centre) >> request.centreFractional.x >> request.centreFractional.y >> request.centreFractional.z;
			const Result<FnvProfiles> result = FnvCorrectionBridge{}.ComputeProfiles(request);
			ASSERT_TRUE(result) << result.Error().technicalDetails;
			const FnvProfiles &profiles = *result;

			std::array<FnvAxisAlignment, 3> alignments;
			std::array<std::vector<double>, 3> models;
			for (int a = 0; a < 3; ++a)
			{
				models[a] = FnvModelPotential(profiles, a, FnvModelConvention::Sxdefectalign);
				alignments[a] = ProposeFnvAlignment(profiles.axes[a], models[a]);
			}
			const FnvCorrection correction = CombineFnvCorrection(profiles, alignments);
			summary << std::format("{},{},{:.9f},{:.9f},{:.9f},{:.9f},{:.6f},{:.9f}", tag, profiles.charge,
				correction.latticeEnergy, correction.alignment, correction.alignmentSpread, correction.correction,
				profiles.energyCutoff, profiles.modelPotentialMean);
			for (const auto &al : alignments) summary << std::format(",{:.9f}", al.alignment);
			for (const auto &al : alignments) summary << std::format(",{:.9f}", al.spread);
			for (const auto &al : alignments) summary << "," << (al.flat ? 1 : 0);
			for (const auto &al : alignments) summary << std::format(",{:.6f}", al.slope);
			summary << "\n" << std::flush;

			std::ofstream dump((Path::FromUtf8(out) / (tag + "_profiles.csv")).Native());
			dump << "axis,x_A,deltaV_eV,Vmodel_sx_eV\n";
			for (int a = 0; a < 3; ++a)
				for (std::size_t k = 0; k < profiles.axes[a].position.size(); ++k)
					dump << std::format("{},{:.9f},{:.9f},{:.9f}\n", a, profiles.axes[a].position[k],
						profiles.axes[a].deltaV[k], models[a][k]);
		}
	}
} // namespace DefectStudio::Tests
