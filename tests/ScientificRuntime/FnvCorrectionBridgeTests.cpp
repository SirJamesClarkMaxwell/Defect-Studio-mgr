#include <gtest/gtest.h>

#include <cstdlib>

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
} // namespace DefectStudio::Tests
